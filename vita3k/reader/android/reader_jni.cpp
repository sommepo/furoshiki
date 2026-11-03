// SPDX-FileCopyrightText: Vita3K Reader fork contributors (from the Station, Flycast, PPSSPP and Eden Reader forks)
// SPDX-License-Identifier: GPL-2.0-or-later

// The reader's bridge to its Kotlin layer (org.vita3k.emulator.reader.ReaderNative),
// in the Android app's library (Vita3K).

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <jni.h>
#include <mutex>
#include <optional>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <util/log.h>

#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#include "reader/core/crt_jni.h"

namespace ReaderJni {
namespace {

JavaVM *s_jvm;

jclass s_bridge_class;
jmethodID s_on_text_event;
jmethodID s_on_layout;
jmethodID s_on_log;
jmethodID s_on_profile_status;
jmethodID s_find_profile;
jmethodID s_load_profile_data;
jmethodID s_on_game_started;
jmethodID s_on_game_stopped;
jmethodID s_on_menu;

jbyteArray ToByteArray(JNIEnv *env, const std::string &text) {
	const jbyteArray array = env->NewByteArray(static_cast<jsize>(text.size()));
	if (array) {
		env->SetByteArrayRegion(array, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte *>(text.data()));
	}
	return array;
}

std::optional<std::string> FromByteArray(JNIEnv *env, jbyteArray array) {
	if (!array) {
		return std::nullopt;
	}
	const jsize size = env->GetArrayLength(array);
	std::string out(static_cast<std::size_t>(size), '\0');
	env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte *>(out.data()));
	return out;
}

bool ClearException(JNIEnv *env, const char *what) {
	if (!env->ExceptionCheck()) {
		return false;
	}
	env->ExceptionDescribe();
	env->ExceptionClear();
	LOG_ERROR("[reader] Kotlin callback {} threw", what);
	return true;
}

/**
 * Everything the native side says to Kotlin is said on one thread of the bridge's own, which
 * stays attached to the Java runtime. The engine's callbacks arrive on the emulator's threads
 * (its loader thread among them, which comes and goes): those hand their work over and, where
 * they need an answer, wait for it.
 */
class Courier {
public:
	void Start() {
		std::scoped_lock lock{mutex_};
		if (worker_.joinable()) {
			return;
		}
		worker_ = std::thread([this] { Run(); });
	}

	/// Runs `job` on the courier's thread at some later point.
	void Post(std::function<void(JNIEnv *)> job) {
		{
			std::scoped_lock lock{mutex_};
			if (!worker_.joinable()) {
				return;
			}
			jobs_.push_back(std::move(job));
		}
		wake_.notify_one();
	}

	/// Runs `job` on the courier's thread and returns when it is done.
	void Call(const std::function<void(JNIEnv *)> &job) {
		if (std::this_thread::get_id() == worker_.get_id()) {
			job(env_);
			return;
		}
		std::mutex done_mutex;
		std::condition_variable done_wake;
		bool done = false;
		Post([&](JNIEnv *env) {
			job(env);
			{
				std::scoped_lock lock{done_mutex};
				done = true;
			}
			done_wake.notify_one();
		});
		std::unique_lock lock{done_mutex};
		done_wake.wait(lock, [&] { return done; });
	}

private:
	void Run() {
		JavaVMAttachArgs args{};
		args.version = JNI_VERSION_1_6;
		args.name = "ReaderBridge";
		if (s_jvm->AttachCurrentThread(&env_, &args) != JNI_OK) {
			return;
		}
		std::unique_lock lock{mutex_};
		while (true) {
			wake_.wait(lock, [this] { return !jobs_.empty(); });
			auto job = std::move(jobs_.front());
			jobs_.pop_front();
			lock.unlock();
			job(env_);
			lock.lock();
		}
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<std::function<void(JNIEnv *)>> jobs_;
	std::thread worker_;
	JNIEnv *env_ = nullptr;
};

Courier s_courier;
/// Set while a profile is being loaded: only then may it ask for its data files.
thread_local bool t_loading_profile = false;

void SendBytes(jmethodID method, const char *what, const std::string &payload) {
	s_courier.Post([method, what, payload](JNIEnv *env) {
		const jbyteArray bytes = ToByteArray(env, payload);
		env->CallStaticVoidMethod(s_bridge_class, method, bytes);
		ClearException(env, what);
		env->DeleteLocalRef(bytes);
	});
}

void ReportStatus(bool loaded, std::size_t hooks, const std::string &message) {
	s_courier.Post([loaded, hooks, message](JNIEnv *env) {
		const jbyteArray text = ToByteArray(env, message);
		env->CallStaticVoidMethod(s_bridge_class, s_on_profile_status, static_cast<jboolean>(loaded), static_cast<jint>(hooks), text);
		ClearException(env, "onProfileStatus");
		env->DeleteLocalRef(text);
	});
}

/// Asks Kotlin for a file by a name: a profile's source, or one of its data files.
std::optional<std::string> AskForFile(jmethodID method, const char *what, const std::string &name) {
	std::optional<std::string> result;
	s_courier.Call([&](JNIEnv *env) {
		const jstring name_string = env->NewStringUTF(name.c_str());
		const auto data = static_cast<jbyteArray>(env->CallStaticObjectMethod(s_bridge_class, method, name_string));
		const bool threw = ClearException(env, what);
		env->DeleteLocalRef(name_string);
		if (!threw) {
			result = FromByteArray(env, data);
		}
		if (data) {
			env->DeleteLocalRef(data);
		}
	});
	return result;
}

/// Loads the running game's profile. On the emulator's thread.
void LoadProfile(const std::string &game_id) {
	auto &engine = Reader::Engine::Instance();
	if (!s_bridge_class || game_id.empty()) {
		return;
	}
	const auto source = AskForFile(s_find_profile, "findProfile", game_id);
	if (!source) {
		engine.UnloadProfile();
		ReportStatus(false, 0, "");
		return;
	}
	t_loading_profile = true;
	const bool loaded = engine.LoadProfile(game_id + ".lua", *source);
	t_loading_profile = false;
	if (loaded) {
		ReportStatus(true, engine.HookCount(), "");
	} else {
		ReportStatus(false, 0, engine.LastError());
	}
}

void Install() {
	auto &engine = Reader::Engine::Instance();
	s_courier.Start();
	engine.SetEventCallback([](const Reader::TextEvent &event) {
		SendBytes(s_on_text_event, "onTextEvent", event.ToJson());
	});
	engine.SetLayoutCallback([](const Reader::TextLayout &layout) {
		SendBytes(s_on_layout, "onLayout", layout.ToJson());
	});
	engine.SetLogCallback([](Reader::LogLevel level, const std::string &message) {
		s_courier.Post([level, message](JNIEnv *env) {
			const jbyteArray bytes = ToByteArray(env, message);
			env->CallStaticVoidMethod(s_bridge_class, s_on_log, static_cast<jint>(level), bytes);
			ClearException(env, "onLog");
			env->DeleteLocalRef(bytes);
		});
	});
	engine.SetDataProvider([](const std::string &name) -> std::optional<std::string> {
		if (!t_loading_profile) {
			return std::nullopt;
		}
		return AskForFile(s_load_profile_data, "loadProfileData", name);
	});
	Reader::Emu::SetGameLoadedCallback([](const std::string &game_id) {
		const std::string title = Reader::Emu::GameTitle();
		s_courier.Call([&](JNIEnv *env) {
			const jstring id = env->NewStringUTF(game_id.c_str());
			const jbyteArray name = ToByteArray(env, title);
			env->CallStaticVoidMethod(s_bridge_class, s_on_game_started, id, name);
			ClearException(env, "onGameStarted");
			env->DeleteLocalRef(id);
			env->DeleteLocalRef(name);
		});
		LoadProfile(game_id);
	});
	Reader::Emu::SetGameStoppedCallback([] {
		s_courier.Post([](JNIEnv *env) {
			env->CallStaticVoidMethod(s_bridge_class, s_on_game_stopped);
			ClearException(env, "onGameStopped");
		});
	});
	Reader::Emu::SetMenuCallback([](const std::string &what) {
		SendBytes(s_on_menu, "onMenu", what);
	});
}

} // namespace
} // namespace ReaderJni


extern "C" {

JNIEXPORT void JNICALL Java_org_vita3k_emulator_reader_ReaderNative_attach(JNIEnv *env, jobject self) {
	using namespace ReaderJni;
	if (s_bridge_class) {
		return;
	}
	env->GetJavaVM(&s_jvm);
	const jclass bridge = env->GetObjectClass(self);
	s_bridge_class = static_cast<jclass>(env->NewGlobalRef(bridge));
	s_on_text_event = env->GetStaticMethodID(s_bridge_class, "onTextEvent", "([B)V");
	s_on_layout = env->GetStaticMethodID(s_bridge_class, "onLayout", "([B)V");
	s_on_log = env->GetStaticMethodID(s_bridge_class, "onLog", "(I[B)V");
	s_on_profile_status = env->GetStaticMethodID(s_bridge_class, "onProfileStatus", "(ZI[B)V");
	s_find_profile = env->GetStaticMethodID(s_bridge_class, "findProfile", "(Ljava/lang/String;)[B");
	s_load_profile_data = env->GetStaticMethodID(s_bridge_class, "loadProfileData", "(Ljava/lang/String;)[B");
	s_on_game_started = env->GetStaticMethodID(s_bridge_class, "onGameStarted", "(Ljava/lang/String;[B)V");
	s_on_game_stopped = env->GetStaticMethodID(s_bridge_class, "onGameStopped", "()V");
	s_on_menu = env->GetStaticMethodID(s_bridge_class, "onMenu", "([B)V");
	env->DeleteLocalRef(bridge);
	Install();
}

JNIEXPORT void JNICALL Java_org_vita3k_emulator_reader_ReaderNative_reloadProfile(JNIEnv *, jobject) {
	// At the next frame, on the emulator's thread.
	Reader::Engine::Instance().Post([] { ReaderJni::LoadProfile(Reader::Engine::Instance().GameId()); });
}

JNIEXPORT void JNICALL Java_org_vita3k_emulator_reader_ReaderNative_setAudioCapture(JNIEnv *, jobject, jboolean enabled, jint seconds) {
	Reader::AudioTap::Instance().SetEnabled(enabled != JNI_FALSE, static_cast<std::uint32_t>(seconds > 0 ? seconds : 1));
}

JNIEXPORT jlong JNICALL Java_org_vita3k_emulator_reader_ReaderNative_audioPosition(JNIEnv *, jobject) {
	return static_cast<jlong>(Reader::AudioTap::Instance().Position());
}

JNIEXPORT jint JNICALL Java_org_vita3k_emulator_reader_ReaderNative_audioSampleRate(JNIEnv *, jobject) {
	return static_cast<jint>(Reader::AudioTap::SampleRate);
}

JNIEXPORT jshortArray JNICALL Java_org_vita3k_emulator_reader_ReaderNative_readAudio(JNIEnv *env, jobject, jlong start, jlong end, jlongArray actual_start) {
	std::uint64_t first = 0;
	const std::vector<std::int16_t> samples = Reader::AudioTap::Instance().Read(static_cast<std::uint64_t>(start < 0 ? 0 : start), static_cast<std::uint64_t>(end < 0 ? 0 : end), first);
	if (actual_start && env->GetArrayLength(actual_start) > 0) {
		const jlong value = static_cast<jlong>(first);
		env->SetLongArrayRegion(actual_start, 0, 1, &value);
	}
	const jshortArray out = env->NewShortArray(static_cast<jsize>(samples.size()));
	if (out && !samples.empty()) {
		env->SetShortArrayRegion(out, 0, static_cast<jsize>(samples.size()), samples.data());
	}
	return out;
}

JNIEXPORT jintArray JNICALL Java_org_vita3k_emulator_reader_ReaderNative_pictureRect(JNIEnv *env, jobject, jint, jint) {
	// The surface's size the app passes is not needed here: Vita3K knows where it drew.
	int rect[6];
	if (!Reader::Emu::PictureRect(rect)) {
		return nullptr;
	}
	jint values[6];
	std::copy(rect, rect + 6, values);
	const jintArray out = env->NewIntArray(6);
	if (out) {
		env->SetIntArrayRegion(out, 0, 6, values);
	}
	return out;
}

JNIEXPORT void JNICALL Java_org_vita3k_emulator_reader_ReaderNative_holdSticks(JNIEnv *, jobject, jboolean held) {
	Reader::InputFilter::Instance().HoldSticks(held != JNI_FALSE);
	if (held != JNI_FALSE) {
		// The stick as the game last saw it would stay where it was: put it at rest.
		Reader::Emu::RestSticks();
	}
}

JNIEXPORT void JNICALL Java_org_vita3k_emulator_reader_ReaderNative_pressButton(JNIEnv *, jobject, jint bit, jboolean down) {
	// Nothing while no game runs (Reader::Emu::PressButton asks).
	Reader::Emu::PressButton(bit, down != JNI_FALSE);
}

/// Which answer of the choice on screen the game's cursor is on, as the profile reports it:
/// {index (-1 for none yet), 1 when the answers are side by side}, or null when it says nothing.
JNIEXPORT jintArray JNICALL Java_org_vita3k_emulator_reader_ReaderNative_answerCursor(JNIEnv *env, jobject) {
    const auto cursor = Reader::Engine::Instance().Cursor();
    if (!cursor) {
        return nullptr;
    }
    const jint values[2]{cursor->index, cursor->horizontal ? 1 : 0};
    const jintArray array = env->NewIntArray(2);
    if (array) {
        env->SetIntArrayRegion(array, 0, 2, values);
    }
    return array;
}

JNIEXPORT jboolean JNICALL Java_org_vita3k_emulator_reader_ReaderNative_inGame(JNIEnv *, jobject) {
	// Not while the emulator's pause menu has stopped the game.
	return Reader::Emu::Running() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL Java_org_vita3k_emulator_reader_ReaderNative_saveSyncService(JNIEnv *env, jobject) {
	return nullptr; // no save sync for the Vita yet
}

} // extern "C"

// The CRT picture's settings page (reader/ui/CrtSettings.kt) reads the table of what can be
// set and writes the values, which the renderer reads.
READER_CRT_JNI(org_vita3k_emulator)
