// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The Android app's native side: a core loaded (cores/reader_core.h) and run through the session
// the desktop program runs it with (home/src/core_session.*), the game's sound, and the bridge
// to the Kotlin layer: GameNative (the app's own) and ReaderNative, which every reader app has
// (the rest of it is shared/reader/android/reader_jni_common.cpp).
//
// One core a process, and the process goes with the game: GameActivity and ScanService each
// run in a process of their own, so no two emulators ever meet.

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <jni.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core_session.h"
#include "reader/core/audio_tap.h"
#include "reader/core/console.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"

#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif

#define READER_CRT_NO_FRAME // the host in a core draws the picture, and no tube's frame round it
#include "reader/core/crt.h"
#include "reader/core/crt_jni.h"

#define GAME(name) Java_io_github_sommepo_furoshiki_GameNative_##name
#define READER(name) Java_io_github_sommepo_furoshiki_reader_ReaderNative_##name

void ReaderJniCommonLinked();

namespace {
std::mutex g_error_mutex;
std::string g_last_error;
} // namespace

namespace Home {

void CoreLog(uint32_t level, const std::string& text) {
    if (level == READER_CORE_LOG_ERROR) {
        std::scoped_lock lock{g_error_mutex};
        g_last_error = text;
    }
    const int priority = level >= READER_CORE_LOG_ERROR     ? ANDROID_LOG_ERROR
                         : level == READER_CORE_LOG_WARNING ? ANDROID_LOG_WARN
                                                            : ANDROID_LOG_INFO;
    __android_log_write(priority, "Furoshiki", text.c_str());
}

} // namespace Home

namespace {

JavaVM* g_jvm = nullptr;

// ---- The core ----

std::unique_ptr<Home::CoreSession> g_session;
ANativeWindow* g_window = nullptr;
ANativeWindow* g_second_window = nullptr;
int g_width = 0, g_height = 0;

bool LoadCore(const std::string& path, Home::Core& core, std::string& error) {
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        error = dlerror();
        return false;
    }
    const auto get = reinterpret_cast<reader_core_get_fn>(dlsym(library, READER_CORE_ENTRY));
    const reader_core_api* api = get != nullptr ? get(READER_CORE_API_VERSION) : nullptr;
    if (api == nullptr || api->info == nullptr || api->info() == nullptr) {
        error = path + " is not a core of this version of the interface";
        return false;
    }
    core = Home::Core{path, api, api->info()};
    return true;
}

// ---- The controller, as GameActivity last told it ----

std::atomic<uint64_t> g_buttons{0};
std::atomic<int32_t> g_sticks[4];
std::atomic<int32_t> g_triggers[2];

void FillPad(uint32_t port, reader_core_pad& state) {
    if (port != 0) {
        return;
    }
    state.connected = 1;
    state.buttons = g_buttons.load();
    if (!Reader::InputFilter::Instance().SticksHeld()) {
        for (int i = 0; i < 4; ++i) {
            state.sticks[i] = static_cast<int16_t>(g_sticks[i].load());
        }
    }
    state.triggers[0] = static_cast<uint8_t>(g_triggers[0].load());
    state.triggers[1] = static_cast<uint8_t>(g_triggers[1].load());
}

// ---- The game's sound ----

/// What a core made from a libretro build plays (reader_core_host.play). A frame that does not
/// fit is dropped rather than waited for: the core's thread keeps the game's time.
class Sound {
public:
    void Play(const int16_t* samples, size_t frames, uint32_t rate) {
        std::scoped_lock lock{mutex};
        if (stream == nullptr || rate != stream_rate) {
            Open(rate);
        }
        if (stream != nullptr && !paused) {
            AAudioStream_write(stream, samples, static_cast<int32_t>(frames), 0);
        }
    }
    void Pause(bool pause) {
        std::scoped_lock lock{mutex};
        paused = pause;
        if (stream != nullptr) {
            pause ? AAudioStream_requestPause(stream) : AAudioStream_requestStart(stream);
        }
    }

private:
    void Open(uint32_t rate) {
        if (stream != nullptr) {
            AAudioStream_close(stream);
            stream = nullptr;
        }
        AAudioStreamBuilder* builder = nullptr;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
            return;
        }
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(rate));
        AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_SONIFICATION);
        if (AAudioStreamBuilder_openStream(builder, &stream) == AAUDIO_OK) {
            stream_rate = rate;
            if (!paused) {
                AAudioStream_requestStart(stream);
            }
        } else {
            stream = nullptr;
        }
        AAudioStreamBuilder_delete(builder);
    }

    std::mutex mutex;
    AAudioStream* stream = nullptr;
    uint32_t stream_rate = 0;
    bool paused = false;
};

Sound g_sound;

// ---- What the native side says to Kotlin ----

jclass g_bridge = nullptr;
jmethodID g_on_text_event, g_on_layout, g_on_log, g_on_profile_status, g_find_profile, g_profile_source,
    g_load_profile_data, g_on_game_started, g_on_game_stopped;

jbyteArray ToBytes(JNIEnv* env, const std::string& text) {
    const jbyteArray array = env->NewByteArray(static_cast<jsize>(text.size()));
    if (array != nullptr) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
    }
    return array;
}

std::optional<std::string> FromBytes(JNIEnv* env, jbyteArray array) {
    if (array == nullptr) {
        return std::nullopt;
    }
    const jsize size = env->GetArrayLength(array);
    std::string out(static_cast<std::size_t>(size), '\0');
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(out.data()));
    return out;
}

std::string FromJava(JNIEnv* env, jstring text) {
    if (text == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string out = chars != nullptr ? chars : "";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(text, chars);
    }
    return out;
}

bool ClearException(JNIEnv* env, const char* what) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    Home::CoreLog(READER_CORE_LOG_ERROR, std::string("a Kotlin callback threw: ") + what);
    return true;
}

/**
 * Everything the native side says to Kotlin is said on one thread of the bridge's own, which
 * stays attached to the Java runtime: the engine's callbacks arrive on the emulator's threads,
 * which come and go. Those hand their work over and, where they need an answer, wait for it.
 */
class Courier {
public:
    void Start() {
        std::scoped_lock lock{mutex};
        if (!worker.joinable()) {
            worker = std::thread([this] { Run(); });
        }
    }

    void Post(std::function<void(JNIEnv*)> job) {
        {
            std::scoped_lock lock{mutex};
            if (!worker.joinable()) {
                return;
            }
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
    }

    void Call(const std::function<void(JNIEnv*)>& job) {
        if (std::this_thread::get_id() == worker.get_id()) {
            job(env);
            return;
        }
        std::mutex done_mutex;
        std::condition_variable done_wake;
        bool done = false;
        Post([&](JNIEnv* attached) {
            job(attached);
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
        JavaVMAttachArgs arguments{};
        arguments.version = JNI_VERSION_1_6;
        arguments.name = "ReaderBridge";
        if (g_jvm->AttachCurrentThread(&env, &arguments) != JNI_OK) {
            return;
        }
        std::unique_lock lock{mutex};
        for (;;) {
            wake.wait(lock, [this] { return !jobs.empty(); });
            auto job = std::move(jobs.front());
            jobs.pop_front();
            lock.unlock();
            job(env);
            lock.lock();
        }
    }

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void(JNIEnv*)>> jobs;
    std::thread worker;
    JNIEnv* env = nullptr;
};

Courier g_courier;
/// Set while a profile is being loaded: only then may it ask for its data files.
thread_local bool t_loading_profile = false;

void SendBytes(jmethodID method, const char* what, const std::string& payload) {
    g_courier.Post([method, what, payload](JNIEnv* env) {
        const jbyteArray bytes = ToBytes(env, payload);
        env->CallStaticVoidMethod(g_bridge, method, bytes);
        ClearException(env, what);
        env->DeleteLocalRef(bytes);
    });
}

void ReportStatus(bool loaded, std::size_t hooks, const std::string& message) {
    g_courier.Post([loaded, hooks, message](JNIEnv* env) {
        const jbyteArray text = ToBytes(env, message);
        env->CallStaticVoidMethod(g_bridge, g_on_profile_status, static_cast<jboolean>(loaded), static_cast<jint>(hooks), text);
        ClearException(env, "onProfileStatus");
        env->DeleteLocalRef(text);
    });
}

/// Asks Kotlin for a file by a name: a profile's source, or one of its data files.
std::optional<std::string> AskForFile(jmethodID method, const char* what, const std::string& name) {
    std::optional<std::string> result;
    g_courier.Call([&](JNIEnv* env) {
        const jstring text = env->NewStringUTF(name.c_str());
        const auto data = static_cast<jbyteArray>(env->CallStaticObjectMethod(g_bridge, method, text));
        const bool threw = ClearException(env, what);
        env->DeleteLocalRef(text);
        if (!threw) {
            result = FromBytes(env, data);
        }
        if (data != nullptr) {
            env->DeleteLocalRef(data);
        }
    });
    return result;
}

/// Loads the running game's profile, on the core's thread, before any of the game's code runs.
void LoadProfile(const std::string& game_id) {
    auto& engine = Reader::Engine::Instance();
    if (g_bridge == nullptr || game_id.empty()) {
        return;
    }
    const auto source = AskForFile(g_find_profile, "findProfile", game_id);
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

void InstallEngineCallbacks() {
    auto& engine = Reader::Engine::Instance();
    g_courier.Start();
    engine.SetEventCallback([](const Reader::TextEvent& event) {
        SendBytes(g_on_text_event, "onTextEvent", event.ToJson());
    });
    engine.SetLayoutCallback([](const Reader::TextLayout& layout) {
        SendBytes(g_on_layout, "onLayout", layout.ToJson());
    });
    engine.SetLogCallback([](Reader::LogLevel level, const std::string& message) {
        g_courier.Post([level, message](JNIEnv* env) {
            const jbyteArray bytes = ToBytes(env, message);
            env->CallStaticVoidMethod(g_bridge, g_on_log, static_cast<jint>(level), bytes);
            ClearException(env, "onLog");
            env->DeleteLocalRef(bytes);
        });
    });
    engine.SetDataProvider([](const std::string& name) -> std::optional<std::string> {
        return t_loading_profile ? AskForFile(g_load_profile_data, "loadProfileData", name) : std::nullopt;
    });
    engine.SetHookPredicate([](const std::string& game_id) {
        const auto source = AskForFile(g_profile_source, "profileSource", game_id);
        return source && Reader::Engine::ProfileWantsHooks(*source);
    });
}

#ifndef READER_NO_SAVE_SYNC
/// The core's moments for save sync, as the desktop program's (MainWindow::StartSaveSync).
void TieSaveSync(Home::CoreSession& session) {
    const std::string key = session.Info().key;
    if ((session.Features() & READER_CORE_HAS_SAVE_SYNC) == 0 || !SaveSync::Host::HasNaming(key)) {
        return;
    }
    session.SetSavesOpeningCallback([key](const Home::CoreSession::SavesOpening& opening) {
        if (SaveSync::ConsoleHost* host = SaveSync::Host::Of(key)) {
            host->GameStarting(opening.game_id, opening.title);
        }
        return 0;
    });
    session.SetSavesWrittenCallback([key](const std::string&, const std::string&, uint32_t) {
        if (SaveSync::ConsoleHost* host = SaveSync::Host::Of(key)) {
            host->NoteSaved();
        }
    });
    session.SetStopListener([key] {
        if (SaveSync::ConsoleHost* host = SaveSync::Host::Of(key)) {
            host->GameStopped();
        }
    });
}
#endif

void TieSession() {
    Home::CoreSession& session = *g_session;
#ifndef READER_NO_SAVE_SYNC
    TieSaveSync(session);
#endif
    session.SetPadSource(&FillPad);
    session.SetPlaySink([](const int16_t* samples, size_t frames, uint32_t rate) { g_sound.Play(samples, frames, rate); });
    session.SetGameCallbacks(
        [](const std::string& game_id, const std::string& title) {
            if (g_bridge == nullptr) {
                return;
            }
            g_courier.Call([&](JNIEnv* env) {
                const jstring id = env->NewStringUTF(game_id.c_str());
                const jbyteArray name = ToBytes(env, title);
                env->CallStaticVoidMethod(g_bridge, g_on_game_started, id, name);
                ClearException(env, "onGameStarted");
                env->DeleteLocalRef(id);
                env->DeleteLocalRef(name);
            });
            LoadProfile(game_id);
        },
        [] {
            if (g_bridge != nullptr) {
                g_courier.Post([](JNIEnv* env) {
                    env->CallStaticVoidMethod(g_bridge, g_on_game_stopped);
                    ClearException(env, "onGameStopped");
                });
            }
        });
}

std::string JsonText(const char* text) {
    std::string out = "\"";
    for (const char* at = text != nullptr ? text : ""; *at != '\0'; ++at) {
        if (*at == '"' || *at == '\\') {
            out += '\\';
        }
        out += static_cast<unsigned char>(*at) < 0x20 ? ' ' : *at;
    }
    return out + "\"";
}

} // namespace

#ifndef READER_NO_SAVE_SYNC
void Reader::Emu::ConfigureSaveSync(const SaveSync::ConsoleHost::Settings& settings) {
    SaveSync::Host::Set(settings);
}
#endif

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// ---- GameNative ----

JNIEXPORT jstring JNICALL GAME(open)(JNIEnv* env, jobject, jstring core_path, jstring data, jstring saves, jstring states,
                                     jstring system) {
    Home::Core core;
    std::string error;
    if (!LoadCore(FromJava(env, core_path), core, error)) {
        return env->NewStringUTF(error.c_str());
    }
    g_session = std::make_unique<Home::CoreSession>(core);
    // A core keeps the strings of its folders, which it copies at init.
    const std::string data_text = FromJava(env, data), saves_text = FromJava(env, saves),
                      states_text = FromJava(env, states), system_text = FromJava(env, system);
    const reader_core_paths paths{sizeof(reader_core_paths), data_text.c_str(), saves_text.c_str(), states_text.c_str(),
                                  system_text.c_str()};
    TieSession();
    if (!g_session->Start(paths, error)) {
        g_session.reset();
        return env->NewStringUTF(error.c_str());
    }
    Reader::Emu::SetSession(g_session.get());
    return nullptr;
}

JNIEXPORT jstring JNICALL GAME(info)(JNIEnv* env, jobject) {
    if (!g_session) {
        return nullptr;
    }
    const reader_core_info& info = g_session->Info();
    std::string json = "{\"key\":" + JsonText(info.key) + ",\"name\":" + JsonText(info.name) +
                       ",\"system\":" + JsonText(info.system) + ",\"short\":" + JsonText(info.short_name) + ",\"types\":[";
    for (uint32_t i = 0; i < info.file_type_count; ++i) {
        json += (i != 0 ? "," : "") + JsonText(info.file_types[i]);
    }
    json += "],\"settings\":[";
    const auto number = [](double value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.6g", value);
        return std::string(text);
    };
    for (uint32_t i = 0; i < info.setting_count; ++i) {
        const reader_core_setting& setting = info.settings[i];
        json += (i != 0 ? ",{" : "{");
        json += "\"key\":" + JsonText(setting.key) + ",\"label\":" + JsonText(setting.label) +
                ",\"kind\":" + std::to_string(setting.kind) + ",\"default\":" + JsonText(setting.default_value) +
                ",\"low\":" + number(setting.low) + ",\"high\":" + number(setting.high) +
                ",\"step\":" + number(setting.step) + ",\"choices\":[";
        for (uint32_t choice = 0; choice < setting.choice_count; ++choice) {
            json += (choice != 0 ? "," : "") + JsonText(setting.choices[choice]);
        }
        json += "]}";
    }
    json += "]}";
    return env->NewStringUTF(json.c_str());
}

/// {id, title} of the game in `path`, or null when it is no game of this core's.
JNIEXPORT jobjectArray JNICALL GAME(identify)(JNIEnv* env, jobject, jstring path) {
    if (!g_session || !READER_CORE_HAS_FIELD(g_session->Api(), identify)) {
        return nullptr;
    }
    char id[64] = {}, title[256] = {};
    const std::string file = FromJava(env, path);
    if (g_session->Api().identify(file.c_str(), id, sizeof(id), title, sizeof(title)) != 0) {
        return nullptr;
    }
    const jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/String"), nullptr);
    env->SetObjectArrayElement(out, 0, env->NewStringUTF(id));
    env->SetObjectArrayElement(out, 1, env->NewStringUTF(title));
    return out;
}

/// The art the game's own file carries, written to `to`.
JNIEXPORT jboolean JNICALL GAME(gameArt)(JNIEnv* env, jobject, jstring path, jstring to) {
    if (!g_session || !READER_CORE_HAS_FIELD(g_session->Api(), game_art) || g_session->Api().game_art == nullptr) {
        return JNI_FALSE;
    }
    return g_session->Api().game_art(FromJava(env, path).c_str(), FromJava(env, to).c_str()) == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL GAME(setSurface)(JNIEnv* env, jobject, jobject surface, jint width, jint height, jfloat scale) {
    ANativeWindow* window = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
    g_width = width;
    g_height = height;
    if (g_session) {
        const reader_core_window given{sizeof(reader_core_window), READER_CORE_WINDOW_ANDROID, window, nullptr,
                                       static_cast<uint32_t>(width), static_cast<uint32_t>(height), scale};
        g_session->SetWindow(given);
    }
    // The core's EGL surface holds a reference of its own to the window it draws on now.
    if (g_window != nullptr) {
        ANativeWindow_release(g_window);
    }
    g_window = window;
}

JNIEXPORT jstring JNICALL GAME(lastError)(JNIEnv* env, jobject) {
    std::scoped_lock lock{g_error_mutex};
    return g_last_error.empty() ? nullptr : env->NewStringUTF(g_last_error.c_str());
}

JNIEXPORT void JNICALL GAME(setSecondSurface)(JNIEnv* env, jobject, jobject surface, jint width, jint height) {
    ANativeWindow* window = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (g_session) {
        const reader_core_window given{sizeof(reader_core_window), READER_CORE_WINDOW_ANDROID, window, nullptr,
                                       static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1.0f};
        g_session->SetSecondWindow(given);
    }
    if (g_second_window != nullptr) {
        ANativeWindow_release(g_second_window);
    }
    g_second_window = window;
}

JNIEXPORT jintArray JNICALL GAME(screenRects)(JNIEnv* env, jobject) {
    if (!g_session || !g_session->Running()) {
        return nullptr;
    }
    const Home::CoreSession::Rect upper = g_session->ScreenRect(0), lower = g_session->ScreenRect(1);
    const jint values[8] = {upper.x, upper.y, upper.width, upper.height, lower.x, lower.y, lower.width, lower.height};
    const jintArray out = env->NewIntArray(8);
    if (out != nullptr) {
        env->SetIntArrayRegion(out, 0, 8, values);
    }
    return out;
}

JNIEXPORT void JNICALL GAME(pointer)(JNIEnv*, jobject, jint screen, jfloat x, jfloat y, jboolean pressed) {
    if (g_session) {
        g_session->Pointer(static_cast<uint32_t>(screen), 0, pressed != JNI_FALSE ? READER_CORE_POINTER_PRESSED : 0u, x, y);
    }
}

JNIEXPORT jstring JNICALL GAME(load)(JNIEnv* env, jobject, jstring path) {
    std::string error = "no core";
    if (g_session && g_session->LoadGame(FromJava(env, path), error)) {
        return nullptr;
    }
    return env->NewStringUTF(error.c_str());
}

JNIEXPORT void JNICALL GAME(pause)(JNIEnv*, jobject, jboolean paused) {
    g_sound.Pause(paused != JNI_FALSE);
    if (g_session) {
        g_session->Pause(paused != JNI_FALSE);
    }
}

JNIEXPORT void JNICALL GAME(stop)(JNIEnv*, jobject) {
    if (g_session) {
        g_session->Stop();
    }
}

JNIEXPORT void JNICALL GAME(setPad)(JNIEnv*, jobject, jlong buttons, jint left_x, jint left_y, jint right_x, jint right_y,
                                    jint left_trigger, jint right_trigger) {
    g_buttons = static_cast<uint64_t>(buttons);
    g_sticks[0] = left_x;
    g_sticks[1] = left_y;
    g_sticks[2] = right_x;
    g_sticks[3] = right_y;
    g_triggers[0] = left_trigger;
    g_triggers[1] = right_trigger;
}

JNIEXPORT jboolean JNICALL GAME(saveState)(JNIEnv*, jobject, jint slot) {
    return g_session && g_session->SaveState(slot) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL GAME(loadState)(JNIEnv*, jobject, jint slot) {
    return g_session && g_session->LoadState(slot) ? JNI_TRUE : JNI_FALSE;
}

/// The CRT sliders live in this library (the settings page's), the shader's values in the core.
JNIEXPORT void JNICALL GAME(sendCrt)(JNIEnv*, jobject) {
    static std::string sent;
    std::string now = Reader::Crt::Serialize();
    if (g_session && now != sent) {
        sent = now;
        g_session->ApplySettings({{READER_CORE_SETTING_CRT, std::move(now)}});
    }
}

/// Save sync for the console of `key`, with or without its core in this process: the Save sync
/// page works with no game running. `savedata` as that console's core keeps it.
JNIEXPORT void JNICALL GAME(startSaveSync)(JNIEnv* env, jobject self, jstring key, jstring savedata, jstring data,
                                           jstring device) {
#ifndef READER_NO_SAVE_SYNC
    static jclass game_class = static_cast<jclass>(env->NewGlobalRef(env->GetObjectClass(self)));
    static jmethodID on_say = env->GetStaticMethodID(game_class, "onSay", "([B)V");
    g_courier.Start();
    SaveSync::Host::StartConsole(FromJava(env, key), FromJava(env, savedata), FromJava(env, data), {},
                                 [](const std::string& text) {
                                     g_courier.Post([text](JNIEnv* attached) {
                                         const jbyteArray bytes = ToBytes(attached, text);
                                         attached->CallStaticVoidMethod(game_class, on_say, bytes);
                                         ClearException(attached, "onSay");
                                         attached->DeleteLocalRef(bytes);
                                     });
                                 },
                                 FromJava(env, device));
#endif
}

JNIEXPORT void JNICALL GAME(applySetting)(JNIEnv* env, jobject, jstring key, jstring value) {
    if (g_session) {
        g_session->ApplySettings({{FromJava(env, key), FromJava(env, value)}});
    }
}

// ---- ReaderNative ----

JNIEXPORT void JNICALL READER(attach)(JNIEnv* env, jobject self) {
    ReaderJniCommonLinked();
    if (g_bridge != nullptr) {
        return;
    }
    const jclass bridge = env->GetObjectClass(self);
    g_bridge = static_cast<jclass>(env->NewGlobalRef(bridge));
    g_on_text_event = env->GetStaticMethodID(g_bridge, "onTextEvent", "([B)V");
    g_on_layout = env->GetStaticMethodID(g_bridge, "onLayout", "([B)V");
    g_on_log = env->GetStaticMethodID(g_bridge, "onLog", "(I[B)V");
    g_on_profile_status = env->GetStaticMethodID(g_bridge, "onProfileStatus", "(ZI[B)V");
    g_find_profile = env->GetStaticMethodID(g_bridge, "findProfile", "(Ljava/lang/String;)[B");
    g_profile_source = env->GetStaticMethodID(g_bridge, "profileSource", "(Ljava/lang/String;)[B");
    g_load_profile_data = env->GetStaticMethodID(g_bridge, "loadProfileData", "(Ljava/lang/String;)[B");
    g_on_game_started = env->GetStaticMethodID(g_bridge, "onGameStarted", "(Ljava/lang/String;[B)V");
    g_on_game_stopped = env->GetStaticMethodID(g_bridge, "onGameStopped", "()V");
    env->DeleteLocalRef(bridge);
    InstallEngineCallbacks();
}

JNIEXPORT void JNICALL READER(reloadProfile)(JNIEnv*, jobject) {
    // At the next frame, on the core's thread.
    Reader::Engine::Instance().Post([] { LoadProfile(Reader::Engine::Instance().GameId()); });
}

JNIEXPORT void JNICALL READER(setAudioCapture)(JNIEnv*, jobject, jboolean enabled, jint seconds) {
    Reader::AudioTap::Instance().SetEnabled(enabled != JNI_FALSE, static_cast<u32>(seconds > 0 ? seconds : 1));
}

JNIEXPORT jlong JNICALL READER(audioPosition)(JNIEnv*, jobject) {
    return static_cast<jlong>(Reader::AudioTap::Instance().Position());
}

JNIEXPORT jint JNICALL READER(audioSampleRate)(JNIEnv*, jobject) {
    return static_cast<jint>(Reader::AudioTap::SampleRate);
}

JNIEXPORT jshortArray JNICALL READER(readAudio)(JNIEnv* env, jobject, jlong start, jlong end, jlongArray actual_start) {
    u64 first = 0;
    const std::vector<s16> samples = Reader::AudioTap::Instance().Read(static_cast<u64>(start < 0 ? 0 : start),
                                                                       static_cast<u64>(end < 0 ? 0 : end), first);
    if (actual_start != nullptr && env->GetArrayLength(actual_start) > 0) {
        const jlong value = static_cast<jlong>(first);
        env->SetLongArrayRegion(actual_start, 0, 1, &value);
    }
    const jshortArray out = env->NewShortArray(static_cast<jsize>(samples.size()));
    if (out != nullptr && !samples.empty()) {
        env->SetShortArrayRegion(out, 0, static_cast<jsize>(samples.size()), samples.data());
    }
    return out;
}

JNIEXPORT jintArray JNICALL READER(pictureRect)(JNIEnv* env, jobject, jint, jint) {
    if (!g_session || !g_session->Running() || g_width <= 0 || g_height <= 0) {
        return nullptr;
    }
    const Home::CoreSession::Rect rect = g_session->PictureRect();
    if (rect.width <= 0 || rect.height <= 0) {
        return nullptr;
    }
    const jint values[6] = {g_width, g_height, rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    const jintArray out = env->NewIntArray(6);
    if (out != nullptr) {
        env->SetIntArrayRegion(out, 0, 6, values);
    }
    return out;
}

JNIEXPORT void JNICALL READER(holdSticks)(JNIEnv*, jobject, jboolean held) {
    Reader::InputFilter::Instance().HoldSticks(held != JNI_FALSE);
}

JNIEXPORT void JNICALL READER(pressButton)(JNIEnv*, jobject, jint bit, jboolean down) {
    Reader::Emu::PressButton(bit, down != JNI_FALSE);
}

JNIEXPORT jintArray JNICALL READER(answerCursor)(JNIEnv* env, jobject) {
    const auto cursor = Reader::Engine::Instance().Cursor();
    if (!cursor) {
        return nullptr;
    }
    const jint values[2]{cursor->index, cursor->horizontal ? 1 : 0};
    const jintArray out = env->NewIntArray(2);
    if (out != nullptr) {
        env->SetIntArrayRegion(out, 0, 2, values);
    }
    return out;
}

JNIEXPORT jboolean JNICALL READER(inGame)(JNIEnv*, jobject) {
    return g_session && g_session->Running() && !g_session->Paused() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL READER(saveSyncService)(JNIEnv* env, jobject) {
#ifndef READER_NO_SAVE_SYNC
    const SaveSync::Host::Settings settings = SaveSync::Host::Get();
    if (settings.enabled && !settings.addresses.empty() && !settings.code.empty()) {
        const jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/String"), nullptr);
        env->SetObjectArrayElement(out, 0, env->NewStringUTF(settings.addresses.c_str()));
        env->SetObjectArrayElement(out, 1, env->NewStringUTF(settings.code.c_str()));
        return out;
    }
#endif
    return nullptr;
}

} // extern "C"

// The CRT picture's settings page (reader/ui/CrtSettings.kt).
READER_CRT_JNI(io_github_sommepo_furoshiki)
