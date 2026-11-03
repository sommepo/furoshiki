// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Vita3K as a core of Furoshiki, the one program (home/docs/one-program.md, cores/reader_core.h):
// the emulator without a frontend of its own. Furoshiki gives it the window to draw into, the
// controller and the settings, and reads the game; this file takes the place of Vita3K's SDL
// frontend (vita3k/sdl-frontend), whose AppSessionController and renderer::FrameHost it drives.
//
// Vita3K's own files change little (vita3k/AGENTS.md): the controller, the touch screen and the
// sound go in through what Vita3K already has for them (its keyboard's state, its mouse's, an
// AudioAdapter of this file's). Its files tell the reader of hooks and modules through
// reader/core/emulator.h.
//
// Threads: load_game starts a thread of this core's own, which sets the emulator up the first
// time, installs the game if it is a package, boots it and looks after it until stop (the
// controller, the frames, the window, a game asking to start another program). The renderer is
// made on that thread and has a thread of its own. Every guest thread is a host thread of its own,
// and the hooks come from them; the sound comes from a mixing thread of this file's. Each is "the
// core's own thread" to the program.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <miniz.h>

#include <app/functions.h>
#include <app/session_controller.h>
#include <archive.h>
#include <audio/state.h>
#include <config/functions.h>
#include <config/state.h>
#include <ctrl/ctrl.h>
#include <ctrl/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <interface.h>
#include <io/state.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <mem/state.h>
#include <modules/module_parent.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <renderer/frame_host.h>
#include <renderer/state.h>
#include <touch/state.h>
#include <util/log.h>

#include "reader/core/console.h"
#include "reader/core/emulator.h"
#include "reader/core/registers.h"

#include "reader_core.h"

#if defined(__ANDROID__)
#include <android/native_window.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Xlib by its few functions used here: its header's macros (None, Bool, Status...) are names
// Vita3K's headers use too.
#if defined(__linux__) && !defined(__ANDROID__)
extern "C" {
struct _XDisplay;
_XDisplay *XOpenDisplay(const char *name);
int XCloseDisplay(_XDisplay *display);
}
#endif

namespace {

using namespace std::chrono_literals;
namespace Traits = Reader::ConsoleTraits;
namespace stdfs = std::filesystem;

// ---- What the program gave ----

const reader_core_host *g_host = nullptr;
struct Paths {
    std::string data, saves, states, system;
} g_paths;

void Say(uint32_t level, const std::string &message) {
    if (g_host != nullptr && g_host->log != nullptr)
        g_host->log(g_host->user, level, message.c_str());
}

stdfs::path FromUtf8(const std::string &path) {
    return stdfs::path(std::u8string(reinterpret_cast<const char8_t *>(path.data()), path.size()));
}

bool HostHas(std::size_t end_of_field) {
    return g_host != nullptr && g_host->struct_size >= end_of_field;
}
#define HOST_HAS(field) \
    (HostHas(offsetof(reader_core_host, field) + sizeof(g_host->field)) && g_host->field != nullptr)

/// Tells the player: a log line is not seen, and a game that does not start shows nothing else.
void Notify(const std::string &title, const std::string &text) {
    if (HOST_HAS(notify))
        g_host->notify(g_host->user, title.c_str(), text.c_str());
}

// ---- The Vita as the reader knows it (the fork's console.h and registers.h) ----

// The console's buttons by their bits in the Vita's button word (SCE_CTRL_*). L and R are the
// Vita's own; a PlayStation TV's controller has others, which no game read here asks for.
const reader_core_button Buttons[] = {
    { "Select", 0 }, { "Start", 3 }, { "Up", 4 }, { "Right", 5 }, { "Down", 6 }, { "Left", 7 },
    { "L", 8 }, { "R", 9 }, { "Triangle", 12 }, { "Circle", 13 }, { "Cross", 14 }, { "Square", 15 },
};

constexpr uint32_t BitL = 1u << 8;
constexpr uint32_t BitR = 1u << 9;

// A game is a package (.vpk, or the .zip that is the same thing), or a dumped game's folder by
// its eboot.bin. Started once by the file, it stays installed in the core's Vita file system.
const char *const FileTypes[] = { "vpk", "zip", "bin" };

// The Cortex-A9's registers as this fork's registers.h reads them: r0 to r15 (sp, lr and pc too),
// s0 to s31 (single floats) and d0 to d15 (doubles, the s registers in pairs). A snapshot holds
// the 16, the 32 single floats' bits, then the 16 doubles' bits.
constexpr uint32_t ValueCount = 16 + 32 + 16;
std::vector<std::string> g_number_names;
std::vector<reader_core_register_name> g_register_names;

const char *const LanguageChoices[] = { "japanese", "english" };

const char *const ResolutionKey = READER_CORE_SETTING_RESOLUTION;
const char *const VsyncKey = READER_CORE_SETTING_VSYNC;
const char *const VolumeKey = READER_CORE_SETTING_VOLUME;
const char *const LanguageKey = "psvita.language";

const reader_core_setting Settings[] = {
    { ResolutionKey, nullptr, READER_CORE_SETTING_NUMBER, "1", 1, 8, 1, nullptr, 0 },
    { VsyncKey, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0 },
    { VolumeKey, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 1, nullptr, 0 },
    { LanguageKey, "System language", READER_CORE_SETTING_CHOICE, "japanese", 0, 0, 0, LanguageChoices, 2 },
};
const uint32_t SettingFlags[] = {
    READER_CORE_SETTING_AT_START, READER_CORE_SETTING_AT_START, 0, READER_CORE_SETTING_AT_START
};

reader_core_info g_info{};

reader_core_info MakeInfo() {
    g_number_names.clear();
    for (int i = 0; i < 16; ++i)
        g_number_names.push_back("r" + std::to_string(i));
    for (int i = 0; i < 32; ++i)
        g_number_names.push_back("s" + std::to_string(i));
    for (int i = 0; i < 16; ++i)
        g_number_names.push_back("d" + std::to_string(i));
    g_register_names.clear();
    for (uint32_t i = 0; i < 16; ++i)
        g_register_names.push_back({ g_number_names[i].c_str(), i, READER_CORE_REGISTER_INTEGER, 32 });
    g_register_names.push_back({ "sp", 13, READER_CORE_REGISTER_INTEGER, 32 });
    g_register_names.push_back({ "lr", 14, READER_CORE_REGISTER_INTEGER, 32 });
    g_register_names.push_back({ "pc", 15, READER_CORE_REGISTER_INTEGER, 32 });
    for (uint32_t i = 0; i < 32; ++i)
        g_register_names.push_back({ g_number_names[16 + i].c_str(), 16 + i, READER_CORE_REGISTER_FLOAT32, 32 });
    for (uint32_t i = 0; i < 16; ++i)
        g_register_names.push_back({ g_number_names[48 + i].c_str(), 48 + i, READER_CORE_REGISTER_FLOAT64, 64 });

    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "psvita";
    info.name = "Vita3K";
    info.version = "0.2.1";
    info.system = "PlayStation Vita";
    info.short_name = "VITA";
    info.app_key = Traits::AppKey;
    info.app_name = Traits::AppName;
    info.media_prefix = Traits::MediaPrefix;
    info.game_id_pattern = Traits::GameIdPattern;
    info.game_id_upper = Traits::GameIdUpper;
    info.game_page = Traits::GamePage;
    info.game_page_upper = Traits::GamePageUpper;
    info.game_page_without_hyphens = Traits::GamePageWithoutHyphens;
    info.script_index_url = Traits::ScriptIndexUrl;
    info.script_system = Traits::ScriptSystem;
    info.button_yes = Traits::ButtonYes;
    info.button_up = Traits::ButtonUp;
    info.button_down = Traits::ButtonDown;
    info.button_left = Traits::ButtonLeft;
    info.button_right = Traits::ButtonRight;
    info.button_press_ms = Traits::ButtonPressMs;
    info.pick_step_ms = Traits::PickStepMs;
    info.screen_width = Traits::ScreenWidth;
    info.screen_height = Traits::ScreenHeight;
    info.lower_screen_width = Traits::LowerScreenWidth;
    info.has_next_choice = Traits::HasNextChoice;
    info.file_types = FileTypes;
    info.file_type_count = static_cast<uint32_t>(std::size(FileTypes));
    info.buttons = Buttons;
    info.button_count = static_cast<uint32_t>(std::size(Buttons));
    info.cpu.family = "armv7";
    info.cpu.value_count = ValueCount;
    info.cpu.names = g_register_names.data();
    info.cpu.name_count = static_cast<uint32_t>(g_register_names.size());
    info.cpu.hook_alignment = 2;
    info.cpu.thumb_odd = 1;
    info.cpu.highest_address = Reader::Registers::HighestAddress;
    info.settings = Settings;
    info.setting_count = static_cast<uint32_t>(std::size(Settings));
    info.features = READER_CORE_HAS_POINTER | READER_CORE_HAS_SAVE_SYNC;
    info.port_count = 1;
    info.pointer_screens = 1;
    info.setting_flags = SettingFlags;
    return info;
}

// ---- The state of one running game ----

std::mutex g_settings_mutex;
std::map<std::string, std::string> g_settings;

std::mutex g_window_mutex;
reader_core_window g_window{};
std::atomic<int> g_window_width{ 960 }, g_window_height{ 544 };

std::unique_ptr<EmuEnvState> g_emu;
std::unique_ptr<app::AppSessionController> g_session;
bool g_set_up = false;

std::thread g_thread;
std::atomic<bool> g_stop{ false };
std::atomic<bool> g_paused{ false };
std::atomic<uint32_t> g_pressed{ 0 };
std::atomic<uint32_t> g_game_running{ 0 };
/// Why the game did not start, for the player. Only Run's thread uses it.
std::string g_failure;

// Taken shared to read the game's memory, alone to end it: the memory goes with the game.
std::shared_mutex g_memory_mutex;
bool g_memory_up = false;

struct GrabRequest {
    uint32_t what = 0;
    reader_core_grab_fn done = nullptr;
    void *context = nullptr;
};
std::mutex g_grab_mutex;
std::vector<GrabRequest> g_grabs;

// ---- Where the picture goes ----

#if defined(__ANDROID__)
/// The app's Surface as a Vulkan surface, in the place of SDL's window on Vita3K's own app.
class AndroidSurface final : public renderer::VulkanSurfaceProvider {
public:
    std::vector<const char *> instance_extensions() const override {
        return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
    }
    VkSurfaceKHR create_surface(VkInstance instance) const override {
        void *handle;
        {
            std::scoped_lock lock{ g_window_mutex };
            handle = g_window.handle;
        }
        if (handle == nullptr)
            return VK_NULL_HANDLE;
        const auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
            Loader()(instance, "vkCreateAndroidSurfaceKHR"));
        VkAndroidSurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
        info.window = static_cast<ANativeWindow *>(handle);
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (create == nullptr || create(instance, &info, nullptr, &surface) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return surface;
    }
    void destroy_surface(VkInstance instance, VkSurfaceKHR surface) const override {
        const auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(Loader()(instance, "vkDestroySurfaceKHR"));
        if (destroy != nullptr)
            destroy(instance, surface, nullptr);
    }

private:
    static PFN_vkGetInstanceProcAddr Loader() {
        static const auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL), "vkGetInstanceProcAddr"));
        return get;
    }
};
#endif

/// The window the program gave, for the renderer. Vulkan alone: the surface is made by the
/// renderer from the window's handle.
class WindowHost final : public renderer::FrameHost {
public:
    explicit WindowHost(renderer::DisplayHandle handle)
        : display(std::move(handle)) {}

    renderer::DisplayHandle handle() const override {
        return display;
    }
    int drawable_width() const override {
        return g_window_width.load(std::memory_order_relaxed);
    }
    int drawable_height() const override {
        return g_window_height.load(std::memory_order_relaxed);
    }
    std::vector<std::string> font_dirs() const override {
#if defined(__ANDROID__)
        return { "/system/fonts/" };
#elif defined(_WIN32)
        const char *windir = std::getenv("WINDIR");
        return { std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\" };
#else
        const char *home = std::getenv("HOME");
        std::vector<std::string> dirs{ "/usr/local/share/fonts/", "/usr/share/fonts/" };
        if (home != nullptr)
            dirs.insert(dirs.begin(), std::string(home) + "/.local/share/fonts/");
        return dirs;
#endif
    }

#if defined(__ANDROID__)
    const renderer::VulkanSurfaceProvider *vulkan_surface_provider() const override {
        return &surfaces;
    }
#endif

private:
    renderer::DisplayHandle display;
#if defined(__ANDROID__)
    AndroidSurface surfaces;
#endif
};

/// A display handle for the thread that calls: on X11 a connection of the core's own to the
/// display (Xlib's are not shared between threads), which `connection` keeps until closed.
std::optional<renderer::DisplayHandle> OpenWindow(void *&connection) {
    reader_core_window window;
    {
        std::scoped_lock lock{ g_window_mutex };
        window = g_window;
    }
    switch (window.kind) {
#if defined(_WIN32)
    case READER_CORE_WINDOW_WIN32:
        return renderer::Win32DisplayHandle{ window.handle };
#else
#if defined(__ANDROID__)
    case READER_CORE_WINDOW_ANDROID:
        return renderer::DisplayHandle{}; // AndroidSurface makes the renderer's surfaces
#elif defined(__linux__)
    case READER_CORE_WINDOW_X11: {
        _XDisplay *own = XOpenDisplay(nullptr);
        if (own == nullptr)
            return std::nullopt;
        connection = own;
        return renderer::X11DisplayHandle{ own, reinterpret_cast<std::uintptr_t>(window.handle), nullptr };
    }
#endif
#if !defined(__ANDROID__)
    case READER_CORE_WINDOW_WAYLAND:
        return renderer::WaylandDisplayHandle{ window.display, window.handle };
#endif
#endif
    default:
        return std::nullopt;
    }
}

void CloseWindow(void *connection) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (connection != nullptr)
        XCloseDisplay(static_cast<_XDisplay *>(connection));
#endif
}

// ---- The sound ----

/// The game's sound for the program. The Vita has several output ports, each with its own rate:
/// this adapter brings each to stereo at 48 kHz, and a thread of its own mixes them at the
/// console's pace and hands the result to the program (play) and to the reader (audio). A game
/// pushing sound is held back while its port has enough, which is what paces a game's sound
/// thread on the Vita too.
class ProgramAudio final : public AudioAdapter {
    struct Port final : AudioOutPort {
        std::vector<int16_t> samples; // stereo frames, oldest first
        std::size_t start = 0;
        double position = 0;
        int channels = 2;
        std::size_t most = 0;
        float gain = 1.0f;
    };

    static constexpr int Rate = static_cast<int>(Traits::AudioSampleRate);
    static constexpr int MixFrames = Rate / 100;

public:
    explicit ProgramAudio(AudioState &state)
        : AudioAdapter(state) {}

    ~ProgramAudio() override {
        {
            std::scoped_lock lock{ mutex };
            ending = true;
        }
        room.notify_all();
        if (mixer.joinable())
            mixer.join();
    }

    bool init() override {
        mixer = std::thread([this] { Mix(); });
        return true;
    }

    AudioOutPortPtr open_port(int nb_channels, int freq, int nb_sample) override {
        auto port = std::make_shared<Port>();
        port->channels = nb_channels;
        port->freq = freq;
        port->len_microseconds = (nb_sample * 1'000'000ULL) / freq;
        port->len_bytes = nb_sample * nb_channels * static_cast<int>(sizeof(int16_t));
        port->len = nb_sample;
        // Twice a buffer, and at least 40 ms: less leaves gaps when a game's thread is late.
        const auto frames = std::max<std::size_t>(nb_sample * 2ULL * Rate / freq, Rate / 25);
        port->most = frames * 2;
        std::scoped_lock lock{ mutex };
        ports.push_back(port);
        return port;
    }

    void audio_output(AudioOutPort &out_port, const void *buffer) override {
        auto &port = static_cast<Port &>(out_port);
        const auto *source = static_cast<const int16_t *>(buffer);
        const int frames = port.len_bytes / port.channels / static_cast<int>(sizeof(int16_t));
        const double step = static_cast<double>(port.freq) / Rate;
        const auto at = [&](int frame, int channel) -> int {
            frame = std::clamp(frame, 0, frames - 1);
            return source[frame * port.channels + (port.channels > 1 ? channel : 0)];
        };
        std::unique_lock lock{ mutex };
        // Linear: this is for reading along, not for listening closely, and costs next to nothing.
        double position = port.position;
        while (position < frames) {
            const int whole = static_cast<int>(position);
            const double part = position - whole;
            for (int channel = 0; channel < 2; ++channel) {
                const double value = at(whole, channel) * (1.0 - part) + at(whole + 1, channel) * part;
                port.samples.push_back(static_cast<int16_t>(value));
            }
            position += step;
        }
        port.position = position - frames;
        room.wait(lock, [&] {
            return ending || port.stopping || port.samples.size() - port.start <= port.most;
        });
    }

    void set_volume(AudioOutPort &out_port, float volume) override {
        std::scoped_lock lock{ mutex };
        static_cast<Port &>(out_port).gain = volume;
    }

    void switch_state(const bool pause) override {
        std::scoped_lock lock{ mutex };
        paused = pause;
    }

    int get_rest_sample(AudioOutPort &out_port) override {
        auto &port = static_cast<Port &>(out_port);
        std::scoped_lock lock{ mutex };
        const auto left = static_cast<std::size_t>((port.samples.size() - port.start) / 2);
        return static_cast<int>(left * port.freq / Rate);
    }

    void wake_all_ports() override {
        room.notify_all();
    }

private:
    void Mix() {
        std::vector<int> sum(MixFrames * 2);
        std::vector<int16_t> mixed(MixFrames * 2);
        auto next = std::chrono::steady_clock::now();
        while (true) {
            next += 10ms;
            std::this_thread::sleep_until(next);
            bool any = false;
            {
                std::scoped_lock lock{ mutex };
                if (ending)
                    return;
                if (paused)
                    continue;
                std::fill(sum.begin(), sum.end(), 0);
                std::erase_if(ports, [](const std::weak_ptr<Port> &port) { return port.expired(); });
                for (const auto &weak : ports) {
                    const auto port = weak.lock();
                    if (!port)
                        continue;
                    const std::size_t left = port->samples.size() - port->start;
                    const std::size_t take = std::min<std::size_t>(left, MixFrames * 2);
                    for (std::size_t i = 0; i < take; ++i)
                        sum[i] += static_cast<int>(port->samples[port->start + i] * port->gain);
                    port->start += take;
                    any = any || take > 0;
                    if (port->start > 1 << 16) {
                        port->samples.erase(port->samples.begin(), port->samples.begin() + static_cast<std::ptrdiff_t>(port->start));
                        port->start = 0;
                    }
                }
            }
            room.notify_all();
            if (!any)
                continue;
            for (std::size_t i = 0; i < sum.size(); ++i)
                mixed[i] = static_cast<int16_t>(std::clamp(sum[i], -32768, 32767));
            if (HOST_HAS(play))
                g_host->play(g_host->user, mixed.data(), MixFrames, Rate);
            if (g_host != nullptr && g_host->audio != nullptr)
                g_host->audio(g_host->user, mixed.data(), MixFrames, Rate);
        }
    }

    std::mutex mutex;
    std::condition_variable room;
    std::vector<std::weak_ptr<Port>> ports;
    std::thread mixer;
    bool ending = false;
    bool paused = false;
};

// ---- Hooks and modules: what Vita3K's files tell (reader/core/emulator.h) ----

bool IsHooked(uint32_t address) {
    return g_host != nullptr && g_host->is_hooked != nullptr && g_host->is_hooked(g_host->user, address) != 0;
}

void DispatchHook(const Reader::GuestRegisters &registers) {
    if (g_host == nullptr || g_host->hook == nullptr)
        return;
    uint64_t values[ValueCount];
    for (int i = 0; i < 16; ++i)
        values[i] = registers.r[i];
    for (int i = 0; i < 32; ++i)
        values[16 + i] = registers.s[i];
    for (int i = 0; i < 16; ++i)
        values[48 + i] = static_cast<uint64_t>(registers.s[2 * i]) | (static_cast<uint64_t>(registers.s[2 * i + 1]) << 32);
    reader_core_registers hook{};
    hook.pc = registers.r[15];
    hook.values = values;
    hook.count = ValueCount;
    g_host->hook(g_host->user, &hook);
}

void HookUnusable(uint32_t address, const char *why) {
    if (g_host != nullptr && g_host->hook_unusable != nullptr)
        g_host->hook_unusable(g_host->user, address, why);
}

void ModuleLoaded(const char *name, const char *path, uint32_t base, uint32_t size, uint32_t module_nid) {
    if (g_host == nullptr || g_host->module_loaded == nullptr)
        return;
    char build[12];
    std::snprintf(build, sizeof(build), "%08X", module_nid);
    // The program it started with is "main" to a profile (reader.base, reader.build_id).
    const bool is_main = path == "app0:" + g_emu->self_path;
    g_host->module_loaded(g_host->user, is_main ? "main" : name, base, size, build);
}

// ---- A package's or a dumped game's title ----

struct Title {
    std::string id, name;
};

/// TITLE_ID and TITLE from a param.sfo's bytes. A short reader of its own: identify runs on any
/// thread while a game runs, and needs none of the emulator set up.
std::optional<Title> ReadSfo(const uint8_t *data, std::size_t size) {
    const auto word = [&](std::size_t at) {
        uint32_t value;
        std::memcpy(&value, data + at, sizeof(value));
        return value;
    };
    const auto half = [&](std::size_t at) {
        uint16_t value;
        std::memcpy(&value, data + at, sizeof(value));
        return value;
    };
    if (size < 20 || std::memcmp(data, "\0PSF", 4) != 0)
        return std::nullopt;
    const std::size_t keys = word(8), values = word(12), count = word(16);
    if (keys >= size || values >= size || 20 + count * 16 > size)
        return std::nullopt;
    Title title;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t entry = 20 + i * 16;
        const std::size_t key_at = keys + half(entry);
        const std::size_t value_at = values + word(entry + 12);
        const std::size_t length = word(entry + 4);
        if (key_at >= size || value_at + length > size)
            return std::nullopt;
        const auto *key = reinterpret_cast<const char *>(data + key_at);
        const std::size_t key_length = strnlen(key, size - key_at);
        const std::string_view name{ key, key_length };
        if (name != "TITLE_ID" && name != "TITLE")
            continue;
        std::string text(reinterpret_cast<const char *>(data + value_at), length);
        text.resize(std::strlen(text.c_str()));
        (name == "TITLE_ID" ? title.id : title.name) = std::move(text);
    }
    static const std::regex id_form{ Traits::GameIdPattern };
    if (!std::regex_match(title.id, id_form))
        return std::nullopt;
    return title;
}

std::optional<Title> ReadSfoFile(const stdfs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    const std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(file), {} };
    return ReadSfo(bytes.data(), bytes.size());
}

std::optional<Title> ReadPackage(const std::string &path) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        Say(READER_CORE_LOG_WARNING, path + " cannot be read: " + mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        return std::nullopt;
    }
    std::optional<Title> title;
    const mz_uint files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < files && !title; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        const std::string_view entry{ name };
        if (!entry.ends_with("sce_sys/param.sfo"))
            continue;
        std::size_t size = 0;
        void *bytes = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (bytes != nullptr) {
            title = ReadSfo(static_cast<const uint8_t *>(bytes), size);
            mz_free(bytes);
        }
    }
    mz_zip_reader_end(&zip);
    return title;
}

/// What `game` is: a package, a dumped game's eboot.bin or its folder.
std::optional<Title> ReadTitle(const std::string &game) {
    const stdfs::path path = FromUtf8(game);
    std::error_code error;
    if (stdfs::is_directory(path, error))
        return ReadSfoFile(path / "sce_sys" / "param.sfo");
    if (!stdfs::is_regular_file(path, error))
        return std::nullopt;
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    if (extension == ".vpk" || extension == ".zip")
        return ReadPackage(game);
    if (path.filename() == "eboot.bin")
        return ReadSfoFile(path.parent_path() / "sce_sys" / "param.sfo");
    return std::nullopt;
}

int32_t Identify(const char *path, char *id, size_t id_size, char *title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0)
        return -1;
    try {
        const auto found = ReadTitle(path);
        if (!found)
            return -1;
        std::snprintf(id, id_size, "%s", found->id.c_str());
        if (title != nullptr && title_size > 0)
            std::snprintf(title, title_size, "%s", found->name.c_str());
        return 0;
    } catch (...) {
        return -1;
    }
}

// ---- Setting the emulator up ----

/// Where this module is: Vita3K's data and built-in shaders are beside it, in a folder "vita3k".
stdfs::path ModuleFolder() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ModuleFolder), &module);
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    return stdfs::path(path).parent_path();
#else
    Dl_info info{};
    dladdr(reinterpret_cast<void *>(&ModuleFolder), &info);
    return FromUtf8(info.dli_fname != nullptr ? info.dli_fname : "").parent_path();
#endif
}

/// The user's firmware files, put in the system folder, installed into the core's Vita file
/// system once each. Games need them; homebrew mostly does not, so none is no error.
void InstallFirmware(const stdfs::path &vita_fs) {
    std::error_code error;
    bool any = false;
    for (const auto &entry : stdfs::directory_iterator(FromUtf8(g_paths.system), error)) {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension != ".pup")
            continue;
        any = true;
        const stdfs::path done = vita_fs / ("firmware-" + entry.path().filename().string() + ".installed");
        if (stdfs::exists(done, error))
            continue;
        Say(READER_CORE_LOG_INFO, "installing the firmware " + entry.path().filename().string());
        const std::string version = install_pup(vita_fs, entry.path());
        if (version.empty()) {
            Say(READER_CORE_LOG_WARNING, "the firmware " + entry.path().filename().string() + " could not be installed");
            continue;
        }
        std::ofstream(done) << version << '\n';
    }
    if (!any && !stdfs::exists(vita_fs / "vs0", error)) {
        Say(READER_CORE_LOG_WARNING, "no firmware: Sony's firmware file (PSP2UPDAT.PUP or PSVUPDAT.PUP) belongs in " + g_paths.system +
                ". Games need it (homebrew may run without).");
    }
}

bool SetUp() {
    if (g_set_up)
        return true;
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#if defined(__ANDROID__)
    // SDL's start wants the Java of an app of its own, and its first thread asks that Java for
    // the program's name unless it has one. Only SDL's helpers and threads are used here.
    SDL_SetHint(SDL_HINT_APP_NAME, "Furoshiki");
#else
    if (!SDL_Init(0)) {
        Say(READER_CORE_LOG_ERROR, std::string("SDL: ") + SDL_GetError());
        return false;
    }
#endif

    const stdfs::path data = FromUtf8(g_paths.data);
#if defined(__ANDROID__)
    const stdfs::path assets = FromUtf8(g_paths.system) / "vita3k"; // an app's library folder has only libraries
#else
    const stdfs::path assets = ModuleFolder() / "vita3k";
#endif
    Root root;
    root.set_static_assets_path(assets);
    root.set_vita_fs_path(data / "fs");
    root.set_log_path(data);
    root.set_config_path(data);
    root.set_shared_path(data);
    root.set_cache_path(data / "cache");
    root.set_patch_path(data / "patch");
    std::error_code error;
    stdfs::create_directories(data / "fs", error);
    stdfs::create_directories(data / "patch", error);
    stdfs::create_directories(FromUtf8(g_paths.system), error);
    if (!stdfs::is_directory(assets / "data", error)) {
        Say(READER_CORE_LOG_ERROR, "Vita3K's data is not at " + assets.string());
        return false;
    }

    if (logging::init(root, false) != Success) {
        Say(READER_CORE_LOG_ERROR, "Vita3K's log could not be made");
        return false;
    }
    logging::set_log_callback([](std::string message, int level) {
        // spdlog's levels: 3 warning, 4 error, 5 critical. The rest stays in Vita3K's own log.
        if (level >= 3) {
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.pop_back();
            Say(level >= 4 ? READER_CORE_LOG_ERROR : READER_CORE_LOG_WARNING, "Vita3K: " + message);
        }
    });

    g_emu = std::make_unique<EmuEnvState>();
    Config cfg{};
    char program[] = "furoshiki";
    char *arguments[] = { program, nullptr };
    if (config::init_config(cfg, 1, arguments, root, false) != Success) {
        Say(READER_CORE_LOG_ERROR, "Vita3K's settings could not be made");
        return false;
    }
    cfg.backend_renderer = "Vulkan";
    cfg.audio_backend = "Program";
    cfg.frontend = Frontend::sdl;
    cfg.show_welcome = false;
    // A Japanese Vita: games choose their language and the button that says yes by these.
    cfg.sys_button = static_cast<int>(SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE);
    cfg.sys_date_format = static_cast<int>(SCE_SYSTEM_PARAM_DATE_FORMAT_YYYYMMDD);
    cfg.sys_time_format = static_cast<int>(SCE_SYSTEM_PARAM_TIME_FORMAT_24HOUR);

    if (!app::init(*g_emu, cfg, root)) {
        Say(READER_CORE_LOG_ERROR, "Vita3K could not start up");
        return false;
    }
    init_libraries(*g_emu);
    InstallFirmware(g_emu->vita_fs_path);
    if (!app::init_apps_list(*g_emu)) {
        Say(READER_CORE_LOG_ERROR, "Vita3K's list of apps could not be made");
        return false;
    }
    app::load_users(*g_emu);

    Reader::Emu::events.is_hooked = &IsHooked;
    Reader::Emu::events.hook = &DispatchHook;
    Reader::Emu::events.hook_unusable = &HookUnusable;
    Reader::Emu::events.module_loaded = &ModuleLoaded;

    g_session = std::make_unique<app::AppSessionController>(*g_emu);
    g_session->set_before_run_callback([] {
        // Every module of the game is in memory and none of its code has run.
        {
            std::unique_lock lock{ g_memory_mutex };
            g_memory_up = true;
        }
        if (g_host->game_loaded != nullptr)
            g_host->game_loaded(g_host->user, g_emu->io.title_id.c_str(), g_emu->current_app_title.c_str());
    });
    g_set_up = true;
    return true;
}

/// The folder of a game's saves on Vita3K's memory card: everything in it is the game's own.
stdfs::path SaveFolder(const std::string &id) {
    return g_emu->vita_fs_path / "ux0/user/00/savedata" / id;
}

/// What the saves are like now: each file's size and time.
std::map<std::string, std::pair<std::uintmax_t, stdfs::file_time_type>> SavesNow(const stdfs::path &folder) {
    std::map<std::string, std::pair<std::uintmax_t, stdfs::file_time_type>> out;
    std::error_code error;
    for (stdfs::recursive_directory_iterator it{ folder, error }, end; !error && it != end; it.increment(error)) {
        std::error_code inner;
        if (it->is_regular_file(inner))
            out[it->path().generic_string()] = { it->file_size(inner), it->last_write_time(inner) };
    }
    return out;
}

/// Tells the program of each change of a game's saves while it plays, and of any at the end: Vita3K
/// writes them through the game's file calls, which have no one place to listen at.
class SaveWatch {
public:
    SaveWatch(std::string id_, stdfs::path folder_)
        : id{ std::move(id_) }, folder{ std::move(folder_) }, seen{ SavesNow(folder) }, thread{ [this] { Run(); } } {}
    ~SaveWatch() {
        {
            std::scoped_lock lock{ mutex };
            stopping = true;
        }
        wake.notify_all();
        thread.join();
        Look();
    }

private:
    void Run() {
        std::unique_lock lock{ mutex };
        while (!wake.wait_for(lock, std::chrono::seconds(2), [this] { return stopping; })) {
            lock.unlock();
            Look();
            lock.lock();
        }
    }
    void Look() {
        auto now = SavesNow(folder);
        if (now != seen) {
            seen = std::move(now);
            if (HOST_HAS(saves_written))
                g_host->saves_written(g_host->user, id.c_str(), folder.string().c_str(), READER_CORE_WRITE_GAME);
        }
    }

    std::string id;
    stdfs::path folder;
    std::map<std::string, std::pair<std::uintmax_t, stdfs::file_time_type>> seen;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::thread thread;
};

/// Before the game can read its saves: the program fetches the newest, and waits for it here.
void BeforeSaves(const std::string &id) {
    if (!HOST_HAS(saves_opening))
        return;
    const std::string path = SaveFolder(id).string();
    const reader_core_save_location where{ sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, id.c_str(), path.c_str() };
    const reader_core_saves saves{ sizeof(reader_core_saves), id.c_str(), "", 0, &where, 1 };
    g_host->saves_opening(g_host->user, &saves);
}

/// Takes the program's settings into Vita3K's, for the game about to start.
void ApplyToConfig() {
    std::map<std::string, std::string> settings;
    {
        std::scoped_lock lock{ g_settings_mutex };
        settings = g_settings;
    }
    const auto value = [&](const char *key, const char *otherwise) {
        const auto found = settings.find(key);
        return found != settings.end() ? found->second : std::string(otherwise);
    };
    auto &cfg = g_emu->cfg;
    cfg.resolution_multiplier = std::clamp(std::stof(value(ResolutionKey, "1")), 1.0f, 8.0f);
    cfg.v_sync = value(VsyncKey, "1") != "0";
    cfg.audio_volume = std::clamp(std::stoi(value(VolumeKey, "100")), 0, 100);
    cfg.sys_lang = static_cast<int>(value(LanguageKey, "japanese") == "english" ? SCE_SYSTEM_PARAM_LANG_ENGLISH_US : SCE_SYSTEM_PARAM_LANG_JAPANESE);
}

/// Whether this is Sony's kind of game (PCSA00000 and the like) and the firmware is missing: such
/// a game crashes the whole program without it, so it is refused with a word on what is missing.
/// Homebrew has IDs of its own and mostly runs on Vita3K's own system functions.
bool NeedsFirmware(const std::string &id) {
    std::error_code error;
    if (!id.starts_with("PCS") || stdfs::exists(g_emu->vita_fs_path / "vs0/sys/external/libc.suprx", error))
        return false;
    Say(READER_CORE_LOG_ERROR, id + " needs the Vita's firmware: put Sony's firmware file (PSP2UPDAT.PUP or PSVUPDAT.PUP) in " + g_paths.system);
    return true;
}

/// Whether `id` is whole in ux0. A dump's program is copied encrypted and is a SELF only when the
/// last step of an install has put the decrypted game in place, so a file that is there says
/// nothing: an install cut short, or still going on in a program being closed, leaves one.
bool Installed(const std::string &id) {
    std::ifstream program(g_emu->vita_fs_path / "ux0/app" / id / "eboot.bin", std::ios::binary);
    char magic[4] = {};
    return program.read(magic, sizeof(magic)) && std::memcmp(magic, "SCE", sizeof(magic)) == 0;
}

/// The title ID to start `game` by, with the game installed if it was a file not installed yet.
std::optional<std::string> Install(const std::string &game) {
    auto &emu = *g_emu;
    std::error_code error;
    static const std::regex id_form{ Traits::GameIdPattern };
    if (std::regex_match(game, id_form))
        return NeedsFirmware(game) ? std::nullopt : std::optional<std::string>(game);
    const auto title = ReadTitle(game);
    if (!title || NeedsFirmware(title->id))
        return std::nullopt;
    if (Installed(title->id))
        return title->id;

    Say(READER_CORE_LOG_INFO, "installing " + title->id + " (" + title->name + ")");
    // Minutes from a network folder, with nothing on the screen but this.
    const std::jthread notice([&](std::stop_token stop) {
        for (int seconds = 0; !stop.stop_requested(); ++seconds) {
            if (seconds % 3 == 0)
                Notify(title->name, "Installing, the first start only: this takes a few minutes");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    const stdfs::path path = FromUtf8(game);
    bool installed = false;
    if (stdfs::is_directory(path, error) || path.filename() == "eboot.bin") {
        installed = install_contents(emu, stdfs::is_directory(path, error) ? path : path.parent_path()) > 0;
    } else {
        for (const ContentInfo &content : install_archive(emu, path))
            installed = installed || (content.state && content.title_id == title->id);
    }
    if (!installed)
        return std::nullopt;
    app::init_apps_list(emu);
    return title->id;
}

// ---- A running game ----

void Feed() {
    auto &emu = *g_emu;
    reader_core_pad pad{};
    if (g_host->pad != nullptr)
        g_host->pad(g_host->user, 0, &pad);
    const uint32_t word = static_cast<uint32_t>(pad.buttons) | g_pressed.load(std::memory_order_relaxed);
    // ctrl_get asks in two ways: L and R by these bits, or by L1 and R1 in the extended form.
    uint32_t extended = word & ~(BitL | BitR);
    if ((word & BitL) != 0)
        extended |= SCE_CTRL_L1;
    if ((word & BitR) != 0)
        extended |= SCE_CTRL_R1;
    std::scoped_lock lock{ emu.ctrl.mutex };
    auto &keyboard = emu.ctrl.keyboard_state;
    keyboard.buttons = word;
    keyboard.buttons_ext = extended;
    for (int i = 0; i < 4; ++i)
        keyboard.axes[i] = pad.sticks[i] / 32768.0f;
}

void TellPicture(std::array<int, 4> &last) {
    const auto &display = g_emu->display;
    const std::array<int, 4> now{ static_cast<int>(display.viewport_x), static_cast<int>(display.viewport_y),
        static_cast<int>(display.viewport_w), static_cast<int>(display.viewport_h) };
    if (now == last || now[2] <= 0 || now[3] <= 0)
        return;
    last = now;
    if (HOST_HAS(picture_rect))
        g_host->picture_rect(g_host->user, now[0], now[1], now[2], now[3]);
}

void AnswerGrabs() {
    std::vector<GrabRequest> grabs;
    {
        std::scoped_lock lock{ g_grab_mutex };
        grabs.swap(g_grabs);
    }
    if (grabs.empty())
        return;
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> frame = g_emu->renderer->dump_frame(g_emu->display, width, height);
    const bool whole = !frame.empty() && frame.size() == static_cast<std::size_t>(width) * height;
    for (uint32_t &pixel : frame)
        pixel |= 0xFF000000u;
    for (const GrabRequest &grab : grabs) {
        grab.done(grab.context, whole ? reinterpret_cast<const uint8_t *>(frame.data()) : nullptr,
            whole ? width : 0, whole ? height : 0, whole ? width * 4 : 0);
    }
}

/// Looks after the running game until it ends; a request for another program, if it asks for
/// one.
std::optional<AppLaunchRequest> Watch() {
    auto &emu = *g_emu;
    auto &session = *g_session;
    std::array<int, 4> picture{ -1, -1, -1, -1 };
    size_t frames = 0;
    bool paused = false;
    auto next_feed = std::chrono::steady_clock::now();
    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_feed) {
            Feed();
            next_feed = now + 8ms;
        }
        if (const bool wanted = g_paused.load(std::memory_order_relaxed); wanted != paused) {
            if (session.set_pause_reason(app::AppSessionPauseReason::User, wanted))
                paused = wanted;
        }
        const size_t counted = emu.frame_count;
        if (counted != frames) {
            frames = counted;
            TellPicture(picture);
            if (g_host->frame != nullptr)
                g_host->frame(g_host->user, static_cast<uint32_t>(Traits::ScreenWidth), static_cast<uint32_t>(Traits::ScreenHeight));
            AnswerGrabs();
        }
        if (auto request = emu.take_app_launch_request())
            return request;
        if (session.stop_requested() || !session.is_running()) {
            session.reset_stop_request();
            return std::nullopt;
        }
        std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
}

/// Ends the game's session: the memory is the reader's no longer before it goes.
void EndSession(app::AppSessionStopReason reason) {
    {
        std::unique_lock lock{ g_memory_mutex };
        g_memory_up = false;
    }
    g_session->stop(reason);
}

/// Boots one program of the game. Returns the request for the next one, if there is one.
std::optional<AppLaunchRequest> Boot(const AppLaunchRequest &request, WindowHost &host) {
    auto &emu = *g_emu;
    auto &session = *g_session;
    emu.audio.adapter = std::make_unique<ProgramAudio>(emu.audio);
    emu.audio.adapter->init();
    emu.audio.audio_backend = emu.cfg.audio_backend;

    const auto failed = [&](const char *what) -> std::optional<AppLaunchRequest> {
        Say(READER_CORE_LOG_ERROR, what);
        g_failure = what;
        EndSession(app::AppSessionStopReason::LaunchFailure);
        emu.audio.adapter.reset();
        emu.audio.audio_backend.clear();
        return std::nullopt;
    };
    if (!session.begin_launch(request, request.reason != AppLaunchReason::LoadExec))
        return failed("the game is not installed");
    if (!session.initialize_renderer(host))
        return failed("the renderer could not be made");
    if (!session.initialize_runtime())
        return failed("the emulator could not be started");
    if (!session.load_and_run())
        return failed("the game could not be loaded (is the firmware installed?)");

    g_game_running = 1;
    auto next = emu.take_app_launch_request();
    if (!next)
        next = Watch();
    g_game_running = 0;
    const bool relaunch = next && next->reason != AppLaunchReason::ProcessExit;
    EndSession(relaunch ? app::AppSessionStopReason::Relaunch : app::AppSessionStopReason::UserRequest);
    emu.audio.adapter.reset();
    emu.audio.audio_backend.clear();
    return relaunch ? next : std::nullopt;
}

void Run(std::string game) {
    void *connection = nullptr;
    const auto ended = [&] {
        if (g_host->game_stopped != nullptr)
            g_host->game_stopped(g_host->user);
    };
    if (g_host->game_loading != nullptr)
        g_host->game_loading(g_host->user);
    const auto handle = OpenWindow(connection);
    if (!handle) {
        Say(READER_CORE_LOG_ERROR, "no window to draw the game in");
        ended();
        return;
    }
    try {
        if (!SetUp()) {
            Say(READER_CORE_LOG_ERROR, "the emulator could not be set up");
        } else if (const auto title = Install(game)) {
            ApplyToConfig();
            // Vita3K keeps a title's licence for as long as the process lives, and an empty one
            // if it had none: a game installed since would never start.
            g_emu->license.rif.erase(*title);
            WindowHost host{ *handle };
            BeforeSaves(*title);
            SaveWatch watch{ *title, SaveFolder(*title) };
            std::optional<AppLaunchRequest> request = AppLaunchRequest{ .app_path = *title };
            while (request && !g_stop)
                request = Boot(*request, host);
        } else {
            Say(READER_CORE_LOG_ERROR, game + " could not be installed");
            g_failure = "the game could not be installed";
        }
    } catch (const std::exception &error) {
        Say(READER_CORE_LOG_ERROR, std::string("the game ended: ") + error.what());
        g_failure = "the game ended: an error of the emulator's";
    }
    CloseWindow(connection);
    ended();
    // After the game has ended, so that it stands on the page the program goes back to.
    if (!g_failure.empty())
        Notify("PS Vita", g_failure);
    g_failure.clear();
}

// ---- The core's table ----

const reader_core_info *Info() {
    return &g_info;
}

int32_t Init(const reader_core_host *host, const reader_core_paths *paths) {
    if (host == nullptr || paths == nullptr || paths->data == nullptr)
        return 1;
    g_host = host;
    g_paths.data = paths->data;
    g_paths.saves = paths->saves != nullptr ? paths->saves : "";
    g_paths.states = paths->states != nullptr ? paths->states : "";
    g_paths.system = paths->system != nullptr ? paths->system : "";
    return 0;
}

void StopGame() {
    g_stop = true;
    if (g_thread.joinable())
        g_thread.join();
    g_stop = false;
}

void Shutdown() {
    StopGame();
}

int32_t SetWindow(const reader_core_window *window) {
    if (window == nullptr)
        return 1;
    std::scoped_lock lock{ g_window_mutex };
    g_window = *window;
    g_window_width = static_cast<int>(window->width);
    g_window_height = static_cast<int>(window->height);
    return 0;
}

void ApplySettings(const reader_core_setting_value *values, uint32_t count) {
    std::scoped_lock lock{ g_settings_mutex };
    for (uint32_t i = 0; i < count; ++i) {
        g_settings[values[i].key] = values[i].value;
        if (std::strcmp(values[i].key, VolumeKey) == 0 && g_game_running.load() != 0)
            g_emu->audio.set_global_volume(std::clamp(std::atof(values[i].value), 0.0, 100.0) / 100.0f);
    }
}

int32_t LoadGame(const char *game) {
    if (g_host == nullptr || game == nullptr)
        return 1;
    static const std::regex id_form{ Traits::GameIdPattern };
    if (!std::regex_match(game, id_form) && !ReadTitle(game))
        return 1; // not a game of this console's, or not there
    StopGame();
    g_paused = false;
    g_pressed = 0;
    g_thread = std::thread(Run, std::string(game));
    return 0;
}

void Pause(int32_t paused) {
    g_paused = paused != 0;
}

int32_t NoStates(int32_t) {
    return 1; // Vita3K has no savestates
}

size_t ReadMemory(uint64_t address, void *buffer, size_t size) {
    std::shared_lock lock{ g_memory_mutex };
    if (!g_memory_up || address > 0xFFFFFFFFull)
        return 0;
    const MemState &mem = g_emu->mem;
    auto *out = static_cast<uint8_t *>(buffer);
    size_t done = 0;
    while (done < size) {
        const uint64_t at = address + done;
        if (at > 0xFFFFFFFFull || !is_valid_addr(mem, static_cast<Address>(at)))
            break;
        const size_t in_page = KiB(4) - (at & (KiB(4) - 1));
        const size_t take = std::min(in_page, size - done);
        std::memcpy(out + done, Ptr<uint8_t>(static_cast<Address>(at)).get(mem), take);
        done += take;
    }
    return done;
}

int32_t WriteMemory(uint64_t address, const void *buffer, size_t size) {
    std::shared_lock lock{ g_memory_mutex };
    if (!g_memory_up || address + size > 0x100000000ull)
        return 1;
    const MemState &mem = g_emu->mem;
    for (uint64_t at = address & ~static_cast<uint64_t>(KiB(4) - 1); at < address + size; at += KiB(4)) {
        if (!is_valid_addr(mem, static_cast<Address>(at)))
            return 1;
    }
    const auto *in = static_cast<const uint8_t *>(buffer);
    size_t done = 0;
    while (done < size) {
        const uint64_t at = address + done;
        const size_t in_page = KiB(4) - (at & (KiB(4) - 1));
        const size_t take = std::min(in_page, size - done);
        std::memcpy(Ptr<uint8_t>(static_cast<Address>(at)).get(mem), in + done, take);
        done += take;
    }
    return 0;
}

uint32_t MemoryRegions(reader_core_region *regions, uint32_t capacity) {
    std::shared_lock lock{ g_memory_mutex };
    if (!g_memory_up)
        return 0;
    const MemState &mem = g_emu->mem;
    uint32_t count = 0;
    uint64_t start = 0;
    bool open = false;
    constexpr uint64_t Page = KiB(4);
    constexpr uint64_t End = 0x100000000ull;
    const auto close = [&](uint64_t end) {
        if (count < capacity) {
            regions[count] = reader_core_region{ start, end - start,
                READER_CORE_REGION_READ | READER_CORE_REGION_WRITE | READER_CORE_REGION_EXECUTE, 2 };
        }
        ++count;
    };
    for (uint64_t page = 0; page < End; page += Page) {
        const bool valid = is_valid_addr(mem, static_cast<Address>(page));
        if (valid && !open) {
            start = page;
            open = true;
        } else if (!valid && open) {
            close(page);
            open = false;
        }
    }
    if (open)
        close(End);
    return count;
}

void HooksChanged() {
    std::shared_lock lock{ g_memory_mutex };
    if (g_memory_up)
        g_emu->kernel.invalidate_jit_cache(0, 0xFFFFFFFFu);
}

int32_t SaveLocations(const char *game_id, reader_core_location_fn found, void *context) {
    if (game_id == nullptr || found == nullptr || g_emu == nullptr)
        return 1;
    const std::string path = SaveFolder(game_id).string();
    const reader_core_save_location where{ sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, game_id, path.c_str() };
    found(context, &where);
    return 0;
}

void PressButton(int32_t bit, int32_t down) {
    if (bit < 0 || bit >= 32)
        return;
    if (down != 0)
        g_pressed.fetch_or(1u << bit);
    else
        g_pressed.fetch_and(~(1u << bit));
}

int32_t Grab(uint32_t what, reader_core_grab_fn done, void *context) {
    if (done == nullptr || g_game_running.load() == 0)
        return 1;
    std::scoped_lock lock{ g_grab_mutex };
    g_grabs.push_back(GrabRequest{ what, done, context });
    return 0;
}

void Pointer(const reader_core_pointer *pointer) {
    if (pointer == nullptr || g_game_running.load() == 0)
        return;
    auto &touch = g_emu->touch;
    const auto &display = g_emu->display;
    touch.mouse_x = display.viewport_x + pointer->x * display.viewport_w;
    touch.mouse_y = display.viewport_y + pointer->y * display.viewport_h;
    touch.renderer_focused = true;
    touch.mouse_button_left = (pointer->flags & READER_CORE_POINTER_PRESSED) != 0;
    touch.mouse_button_right = (pointer->flags & READER_CORE_POINTER_SECONDARY) != 0;
}

reader_core_api MakeApi() {
    reader_core_api api{};
    api.struct_size = sizeof(api);
    api.api_version = READER_CORE_API_VERSION;
    api.info = &Info;
    api.init = &Init;
    api.shutdown = &Shutdown;
    api.set_window = &SetWindow;
    api.apply_settings = &ApplySettings;
    api.load_game = &LoadGame;
    api.pause = &Pause;
    api.stop = &StopGame;
    api.save_state = &NoStates;
    api.load_state = &NoStates;
    api.read_memory = &ReadMemory;
    api.write_memory = &WriteMemory;
    api.memory_regions = &MemoryRegions;
    api.hooks_changed = &HooksChanged;
    api.running_game_code = nullptr; // system modules are at addresses of their own
    api.press_button = &PressButton;
    api.grab = &Grab;
    api.identify = &Identify;
    api.pointer = &Pointer;
    api.save_locations = &SaveLocations;
    return api;
}

reader_core_api g_api;

} // namespace

#if defined(__ANDROID__)
// What Vita3K's Android files ask of its app, which is not here: sound reaches the program and
// the reader through ProgramAudio, and no keyboard of Android's comes up for the game.
namespace Reader::Emu {
void OnAudio(const void *, const int16_t *, std::size_t, uint32_t, int) {}
} // namespace Reader::Emu

namespace ime {
void set_keyboard_active(bool) {}
} // namespace ime
#endif

extern "C" READER_CORE_EXPORT const reader_core_api *READER_CORE_CALL reader_core_get(uint32_t api_version) {
    if (api_version != READER_CORE_API_VERSION)
        return nullptr;
    if (g_api.struct_size == 0) {
        g_info = MakeInfo();
        g_api = MakeApi();
    }
    return &g_api;
}
