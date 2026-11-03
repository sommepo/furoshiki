// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors (from the PPSSPP Reader fork) (from the Eden Reader fork)
// SPDX-License-Identifier: GPL-3.0+

// The reader's bridge to its Kotlin layer (org.ppsspp.ppsspp.reader.ReaderNative).

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <jni.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <android/log.h>

#include "pcsx2/Config.h"
#include "pcsx2/VMManager.h"

#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#include "reader/core/crt_jni.h"
#include "reader/save_sync/host.h"

// The Java runtime, kept when the Kotlin side attaches.
static JavaVM *gJvm = nullptr;

namespace ReaderJni {

/// A Java byte array as a string of its bytes (UTF-8 from the Kotlin side).
std::string Bytes(JNIEnv *env, jbyteArray bytes) {
	if (bytes == nullptr) {
		return {};
	}
	const jsize length = env->GetArrayLength(bytes);
	std::string out(static_cast<size_t>(length), '\0');
	env->GetByteArrayRegion(bytes, 0, length, reinterpret_cast<jbyte *>(out.data()));
	return out;
}

/// A Java string from UTF-8, through a byte array (NewStringUTF wants modified UTF-8).
jstring Utf8String(JNIEnv *env, const std::string &text) {
	jbyteArray bytes = env->NewByteArray(static_cast<jsize>(text.size()));
	env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte *>(text.data()));
	jclass string_class = env->FindClass("java/lang/String");
	jmethodID make = env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V");
	jstring charset = env->NewStringUTF("UTF-8");
	auto *out = static_cast<jstring>(env->NewObject(string_class, make, bytes, charset));
	env->DeleteLocalRef(bytes);
	env->DeleteLocalRef(charset);
	env->DeleteLocalRef(string_class);
	return out;
}

/// A string as a JSON string, quotes and all.
std::string Quote(const std::string &text) {
	std::string out = "\"";
	for (const char c : text) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				char escaped[8];
				std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
				out += escaped;
			} else {
				out += c;
			}
		}
	}
	return out + "\"";
}

namespace {

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
jmethodID s_on_next_choice;

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
	__android_log_print(ANDROID_LOG_ERROR, "ARMSX2Reader", "Kotlin callback %s threw", what);
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
		if (gJvm->AttachCurrentThread(&env_, &args) != JNI_OK) {
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
	Reader::Emu::SetGameLoadedCallback([](const std::string &game_id, const std::string &title) {
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
	Reader::Emu::SetNextChoiceCallback([](bool running, int end, std::uint64_t passed) {
		if (!s_on_next_choice) {
			return;
		}
		s_courier.Post([running, end, passed](JNIEnv *env) {
			env->CallStaticVoidMethod(s_bridge_class, s_on_next_choice, static_cast<jboolean>(running),
			                          static_cast<jint>(end), static_cast<jlong>(passed));
			ClearException(env, "onNextChoice");
		});
	});
	Reader::Emu::SetGameStoppedCallback([] {
		s_courier.Post([](JNIEnv *env) {
			env->CallStaticVoidMethod(s_bridge_class, s_on_game_stopped);
			ClearException(env, "onGameStopped");
		});
	});
}

} // namespace
} // namespace ReaderJni


extern "C" {

JNIEXPORT void JNICALL Java_com_armsx2_reader_ReaderNative_attach(JNIEnv *env, jobject self) {
	using namespace ReaderJni;
	if (s_bridge_class) {
		return;
	}
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
	s_on_next_choice = env->GetStaticMethodID(s_bridge_class, "onNextChoice", "(ZIJ)V");
	env->DeleteLocalRef(bridge);
	env->GetJavaVM(&gJvm);
	Install();
}

JNIEXPORT void JNICALL Java_com_armsx2_reader_ReaderNative_reloadProfile(JNIEnv *, jobject) {
	// At the next frame, on the emulator's thread.
	Reader::Engine::Instance().Post([] { ReaderJni::LoadProfile(Reader::Engine::Instance().GameId()); });
}

JNIEXPORT void JNICALL Java_com_armsx2_reader_ReaderNative_setAudioCapture(JNIEnv *, jobject, jboolean enabled, jint seconds) {
	Reader::AudioTap::Instance().SetEnabled(enabled != JNI_FALSE, static_cast<u32>(seconds > 0 ? seconds : 1));
}

JNIEXPORT jlong JNICALL Java_com_armsx2_reader_ReaderNative_audioPosition(JNIEnv *, jobject) {
	return static_cast<jlong>(Reader::AudioTap::Instance().Position());
}

JNIEXPORT jint JNICALL Java_com_armsx2_reader_ReaderNative_audioSampleRate(JNIEnv *, jobject) {
	return static_cast<jint>(Reader::AudioTap::SampleRate);
}

JNIEXPORT jshortArray JNICALL Java_com_armsx2_reader_ReaderNative_readAudio(JNIEnv *env, jobject, jlong start, jlong end, jlongArray actual_start) {
	u64 first = 0;
	const std::vector<s16> samples = Reader::AudioTap::Instance().Read(static_cast<u64>(start < 0 ? 0 : start), static_cast<u64>(end < 0 ? 0 : end), first);
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

JNIEXPORT jintArray JNICALL Java_com_armsx2_reader_ReaderNative_pictureRect(JNIEnv *env, jobject, jint width, jint height) {
	// Where the game's picture is on a surface of this size: the whole of it for "Stretch",
	// otherwise the largest area of the chosen shape in its middle. (needs proper testing:
	// the emulator's own zoom and offsets are not followed here)
	if (!VMManager::HasValidVM() || width <= 0 || height <= 0) {
		return nullptr;
	}
	double shape = 4.0 / 3.0;
	switch (EmuConfig.CurrentAspectRatio) {
	case AspectRatioType::Stretch:
		shape = static_cast<double>(width) / height;
		break;
	case AspectRatioType::R16_9:
		shape = 16.0 / 9.0;
		break;
	case AspectRatioType::R10_7:
		shape = 10.0 / 7.0;
		break;
	default:
		break;
	}
	double w = width;
	double h = w / shape;
	if (h > height) {
		h = height;
		w = h * shape;
	}
	const double x = (width - w) / 2;
	const double y = (height - h) / 2;
	const jint values[6] = {
		width, height, static_cast<jint>(x), static_cast<jint>(y), static_cast<jint>(x + w), static_cast<jint>(y + h),
	};
	const jintArray out = env->NewIntArray(6);
	if (out) {
		env->SetIntArrayRegion(out, 0, 6, values);
	}
	return out;
}

JNIEXPORT void JNICALL Java_com_armsx2_reader_ReaderNative_holdSticks(JNIEnv *, jobject, jboolean held) {
	Reader::InputFilter::Instance().HoldSticks(held != JNI_FALSE);
}

JNIEXPORT void JNICALL Java_com_armsx2_reader_ReaderNative_pressButton(JNIEnv *, jobject, jint button, jboolean down) {
	Reader::Emu::PressButton(button, down != JNI_FALSE);
}

// Which answer of the choice on screen the game's cursor is on, as the profile reports it
// (reader.cursor): {index (-1 for none yet), 1 when the answers are side by side}, or null when
// it says nothing. Touch play turns a tap on an answer into d-pad presses with it.
JNIEXPORT jintArray JNICALL Java_com_armsx2_reader_ReaderNative_answerCursor(JNIEnv *env, jobject) {
	const auto cursor = Reader::Engine::Instance().Cursor();
	if (!cursor)
		return nullptr;
	const jint values[2]{cursor->index, cursor->horizontal ? 1 : 0};
	const jintArray array = env->NewIntArray(2);
	if (array)
		env->SetIntArrayRegion(array, 0, 2, values);
	return array;
}

JNIEXPORT jboolean JNICALL Java_com_armsx2_reader_ReaderNative_inGame(JNIEnv *, jobject) {
	return VMManager::GetState() == VMState::Running ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL Java_com_armsx2_reader_ReaderNative_saveSyncService(JNIEnv *env, jobject) {
	// The reading statistics go to the save sync service while save sync is on.
#ifdef READER_NO_SAVE_SYNC
	return nullptr;
#else
	if (!SaveSync::Host::Started()) {
		return nullptr;
	}
	const SaveSync::Host::Settings settings = SaveSync::Host::Get();
	if (!settings.enabled || settings.addresses.empty() || settings.code.empty()) {
		return nullptr;
	}
	jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/String"), nullptr);
	env->SetObjectArrayElement(out, 0, ReaderJni::Utf8String(env, settings.addresses));
	env->SetObjectArrayElement(out, 1, ReaderJni::Utf8String(env, settings.code));
	return out;
#endif
}

} // extern "C"

// The CRT picture's settings page (reader/ui/CrtSettings.kt) reads the table of what can be
// set and writes the values, which the renderer reads.
READER_CRT_JNI(com_armsx2)
