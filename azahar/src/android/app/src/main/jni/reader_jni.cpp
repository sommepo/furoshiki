// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <optional>
#include <string>
#include <vector>
#include <fmt/format.h>
#include "common/logging/log.h"
#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#include "jni/id_cache.h"
#include "jni/reader_jni.h"
#ifndef READER_NO_SAVE_SYNC
#include "jni/save_sync_glue.h"
#endif
// Reader fork: the 3DS shows two screens, on one display on a phone: no tube frame here.
#define READER_CRT_NO_FRAME
#include "reader/core/crt_jni.h"

namespace ReaderJni {

namespace {

jclass s_bridge_class;
jmethodID s_on_text_event;
jmethodID s_on_layout;
jmethodID s_on_log;
jmethodID s_on_profile_status;
jmethodID s_find_profile;
jmethodID s_load_profile_data;

/// Strings cross JNI as UTF-8 byte arrays: NewStringUTF expects "modified UTF-8" and aborts on
/// four-byte sequences under CheckJNI.
jbyteArray ToByteArray(JNIEnv* env, const std::string& text) {
    const jbyteArray array = env->NewByteArray(static_cast<jsize>(text.size()));
    if (array) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(text.size()),
                                reinterpret_cast<const jbyte*>(text.data()));
    }
    return array;
}

std::optional<std::string> FromByteArray(JNIEnv* env, jbyteArray array) {
    if (!array) {
        return std::nullopt;
    }
    const jsize size = env->GetArrayLength(array);
    std::string out(static_cast<std::size_t>(size), '\0');
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(out.data()));
    return out;
}

/// A Kotlin exception must not stay pending while native code keeps running.
bool ClearException(JNIEnv* env, const char* what) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    LOG_ERROR(Frontend, "[reader] Kotlin callback {} threw", what);
    return true;
}

void ReportStatus(bool loaded, std::size_t hooks, const std::string& message) {
    JNIEnv* env = IDCache::GetEnvForThread();
    const jbyteArray text = ToByteArray(env, message);
    env->CallStaticVoidMethod(s_bridge_class, s_on_profile_status, static_cast<jboolean>(loaded),
                              static_cast<jint>(hooks), text);
    ClearException(env, "onProfileStatus");
    env->DeleteLocalRef(text);
}

/// Asks Kotlin for the running game's profile and loads it. Emulation thread only.
void LoadProfileFromFrontend() {
    auto& engine = Reader::Engine::Instance();
    const std::string title = engine.GameId();
    if (title.empty()) {
        return;
    }
    JNIEnv* env = IDCache::GetEnvForThread();
    const jstring title_string = env->NewStringUTF(title.c_str());
    const auto source_array = static_cast<jbyteArray>(
        env->CallStaticObjectMethod(s_bridge_class, s_find_profile, title_string));
    const bool threw = ClearException(env, "findProfile");
    env->DeleteLocalRef(title_string);

    const auto source = threw ? std::nullopt : FromByteArray(env, source_array);
    if (source_array) {
        env->DeleteLocalRef(source_array);
    }
    if (!source) {
        engine.UnloadProfile();
        ReportStatus(false, 0, "");
        return;
    }
    if (engine.LoadProfile(title + ".lua", *source)) {
        ReportStatus(true, engine.HookCount(), "");
    } else {
        ReportStatus(false, 0, engine.LastError());
    }
}

} // Anonymous namespace

void Init(JNIEnv* env) {
    const jclass bridge = env->FindClass("org/citra/citra_emu/reader/ReaderNative");
    s_bridge_class = static_cast<jclass>(env->NewGlobalRef(bridge));
    s_on_text_event = env->GetStaticMethodID(s_bridge_class, "onTextEvent", "([B)V");
    s_on_layout = env->GetStaticMethodID(s_bridge_class, "onLayout", "([B)V");
    s_on_log = env->GetStaticMethodID(s_bridge_class, "onLog", "(I[B)V");
    s_on_profile_status = env->GetStaticMethodID(s_bridge_class, "onProfileStatus", "(ZI[B)V");
    s_find_profile =
        env->GetStaticMethodID(s_bridge_class, "findProfile", "(Ljava/lang/String;)[B");
    s_load_profile_data =
        env->GetStaticMethodID(s_bridge_class, "loadProfileData", "(Ljava/lang/String;)[B");
    env->DeleteLocalRef(bridge);
#ifndef READER_NO_SAVE_SYNC
    SaveSyncGlue::Install();
#endif
}

void Shutdown(JNIEnv* env) {
    env->DeleteGlobalRef(s_bridge_class);
    s_bridge_class = nullptr;
}

void OnGameStarted() {
    auto& engine = Reader::Engine::Instance();

    engine.SetEventCallback([](const Reader::TextEvent& event) {
        JNIEnv* env = IDCache::GetEnvForThread();
        const jbyteArray json = ToByteArray(env, event.ToJson());
        env->CallStaticVoidMethod(s_bridge_class, s_on_text_event, json);
        ClearException(env, "onTextEvent");
        env->DeleteLocalRef(json);
    });

    engine.SetLayoutCallback([](const Reader::TextLayout& layout) {
        JNIEnv* env = IDCache::GetEnvForThread();
        const jbyteArray json = ToByteArray(env, layout.ToJson());
        env->CallStaticVoidMethod(s_bridge_class, s_on_layout, json);
        ClearException(env, "onLayout");
        env->DeleteLocalRef(json);
    });

    engine.SetLogCallback([](Reader::LogLevel level, const std::string& message) {
        JNIEnv* env = IDCache::GetEnvForThread();
        const jbyteArray text = ToByteArray(env, message);
        env->CallStaticVoidMethod(s_bridge_class, s_on_log, static_cast<jint>(level), text);
        ClearException(env, "onLog");
        env->DeleteLocalRef(text);
    });

    engine.SetDataProvider([](const std::string& name) -> std::optional<std::string> {
        JNIEnv* env = IDCache::GetEnvForThread();
        const jstring name_string = env->NewStringUTF(name.c_str());
        const auto data = static_cast<jbyteArray>(
            env->CallStaticObjectMethod(s_bridge_class, s_load_profile_data, name_string));
        const bool threw = ClearException(env, "loadProfileData");
        env->DeleteLocalRef(name_string);
        auto result = threw ? std::nullopt : FromByteArray(env, data);
        if (data) {
            env->DeleteLocalRef(data);
        }
        return result;
    });

    LoadProfileFromFrontend();
}

void OnGameStopped() {
    auto& engine = Reader::Engine::Instance();
    engine.SetEventCallback(nullptr);
    engine.SetLayoutCallback(nullptr);
    engine.SetLogCallback(nullptr);
    engine.SetDataProvider(nullptr);
}

} // namespace ReaderJni

extern "C" {

/// The shared layer's first call: here the bridge was found when the library was loaded (Init).
JNIEXPORT void JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_attach(JNIEnv*, jobject) {}

/// Which answer of the choice on screen the game's cursor is on, as the profile reports it:
/// {index (-1 for none yet), 1 when the answers are side by side}, or null when it says nothing.
JNIEXPORT jintArray JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_answerCursor(JNIEnv* env,
                                                                                       jobject) {
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

/// The shared layer's picture of a one-screen console: not used here, where the two screens'
/// places come from ReaderGlue.screenRects (jni/native.cpp).
JNIEXPORT jintArray JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_pictureRect(JNIEnv*,
                                                                                      jobject, jint,
                                                                                      jint) {
    return nullptr;
}

/// While held, the circle pad steers the reader's cursor and the game sees it at rest.
JNIEXPORT void JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_holdSticks(JNIEnv*, jobject,
                                                                                jboolean held) {
    Reader::InputFilter::Instance().HoldSticks(held != JNI_FALSE);
    Reader::Emu::RestSticks();
}

/// Presses or releases one of the console's buttons for the player, by the app's numbers
/// (NativeLibrary.ButtonType: touch play's presses, the next line, an answer picked).
JNIEXPORT void JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_pressButton(JNIEnv*, jobject,
                                                                                 jint input,
                                                                                 jboolean down) {
    Reader::Emu::PressButton(static_cast<int>(input), down != JNI_FALSE);
}

/// True while the game itself runs: started and not paused (jni/native.cpp).
JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_inGame(JNIEnv*, jobject) {
    return ReaderJni::GameRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_reloadProfile(JNIEnv*,
                                                                                   jobject) {
    // Profiles may only be swapped on the emulation thread; this runs at the next frame.
    Reader::Engine::Instance().Post([] { ReaderJni::LoadProfileFromFrontend(); });
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_setAudioCapture(
    JNIEnv*, jobject, jboolean enabled, jint seconds) {
    Reader::AudioTap::Instance().SetEnabled(enabled != JNI_FALSE,
                                            static_cast<u32>(seconds > 0 ? seconds : 1));
}

JNIEXPORT jlong JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_audioPosition(JNIEnv*,
                                                                                    jobject) {
    return static_cast<jlong>(Reader::AudioTap::Instance().Position());
}

JNIEXPORT jint JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_audioSampleRate(JNIEnv*,
                                                                                     jobject) {
    return static_cast<jint>(Reader::AudioTap::SampleRate);
}

/// Returns interleaved stereo samples for [start, end); actual_start[0] receives the position of
/// the first returned frame.
JNIEXPORT jshortArray JNICALL Java_org_citra_citra_1emu_reader_ReaderNative_readAudio(
    JNIEnv* env, jobject, jlong start, jlong end, jlongArray actual_start) {
    u64 first = 0;
    const std::vector<s16> samples = Reader::AudioTap::Instance().Read(
        static_cast<u64>(start < 0 ? 0 : start), static_cast<u64>(end < 0 ? 0 : end), first);
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

} // extern "C"

// The CRT picture's settings page (reader/ui/CrtSettings.kt) reads the table of what can be
// set and writes the values, which the renderer reads.
READER_CRT_JNI(org_citra_citra_1emu)
