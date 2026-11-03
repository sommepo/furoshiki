// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Station Reader as a core of Reader Home, the one program (Reader Home's docs/one-program.md,
// cores/reader_core.h): DuckStation's emulator (src/core, with frontend-common's OpenGL display
// and sound) without a frontend of its own. Reader Home gives it the window to draw into, the
// controller and the settings, and reads the game: this file hands it what the fork's own
// reader/core/emulator.cpp hands the reader's engine in the app (the engine is in the program
// here), through reader_core_host.
//
// It is that file's twin: DuckStation's files call the same Reader::Emu functions
// (reader/core/emulator.h, at their "Reader fork" comments), and a core build links this file
// in emulator.cpp's place (reader/core_library/CMakeLists.txt). What the console is (its
// game IDs, buttons, timings) is read from the fork's own reader/core/console.h, so that the
// app and the core say the same.
//
// Threads: the game runs on a thread of this core's own (load_game starts it); it makes the
// OpenGL context in Reader Home's window, boots the console, and runs a frame, draws it and
// waits for the next, until stop. What the program asks from its own thread (pause, a
// savestate, a setting, a new window size) is queued and done there between frames.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/audio_stream.h"
#include "common/cd_image.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/window_info.h"
#include "core/bus.h"
#include "core/controller.h"
#include "core/cpu_core.h"
#include "core/host_display.h"
#include "core/host_interface.h"
#include "core/memory_card.h"
#include "core/pad.h"
#include "core/settings.h"
#include "core/system.h"
#include "common/string_util.h"
#include "frontend-common/common_host_interface.h"
#include "frontend-common/cubeb_audio_stream.h"
#include "frontend-common/opengl_host_display.h"
#include "glad.h"

#include "reader/core/console.h"
#include "reader/core/crt.h"
#include "reader/core/disc_sets.h"
#include "reader/core/emulator.h"
#include "reader_core.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#undef None
#undef Status
#undef Bool
#undef Always
#endif

namespace {

// ---- What the program gave ----

const reader_core_host* g_host = nullptr;
struct Paths {
    std::string data, saves, states, system;
} g_paths;

void Say(uint32_t level, const std::string& message) {
    if (g_host != nullptr && g_host->log != nullptr) {
        g_host->log(g_host->user, level, message.c_str());
    }
}

// ---- The console as the reader knows it (the fork's console.h) ----

namespace Traits = Reader::ConsoleTraits;

const char* const FileTypes[] = {"cue", "bin", "img", "iso", "chd", "ecm", "mds", "pbp", "m3u", "exe", "psexe"};
const reader_core_button Buttons[] = {
    {"Select", 0}, {"L3", 1},       {"R3", 2},     {"Start", 3},   {"Up", 4},      {"Right", 5},
    {"Down", 6},   {"Left", 7},     {"L2", 8},     {"R2", 9},      {"L1", 10},     {"R1", 11},
    {"Triangle", 12}, {"Circle", 13}, {"Cross", 14}, {"Square", 15},
};

// The R3000A's registers as registers.h reads them: r0 to r31 by number and by their usual
// names, hi and lo. A snapshot holds the 32, then hi, then lo.
constexpr const char* RegisterNames[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
                                           "t3",   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
                                           "s6",   "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
constexpr uint32_t ValueCount = 34;
std::vector<std::string> g_number_names;
std::vector<reader_core_register_name> g_register_names;

// The settings this core understands (Reader Home's pages show them, its layers say their
// values): the common ones it maps onto DuckStation's, and the console's own under "ps1.".
// Each comes with where it goes in DuckStation's settings (ApplySetting).
const char* const RendererChoices[] = {"opengl", "software"};
const char* const AspectChoices[] = {"auto", "4:3", "16:9", "stretch"};
const char* const FilterChoices[] = {"nearest", "bilinear", "jinc2", "xbr"};
const char* const RegionChoices[] = {"auto", "ntsc-j", "ntsc-u", "pal"};
const char* const CdSpeedChoices[] = {"1", "2", "4", "8"};
#define READER_CHOICES(list) list, static_cast<uint32_t>(std::size(list))
const reader_core_setting CoreSettings[] = {
    {READER_CORE_SETTING_RESOLUTION, nullptr, READER_CORE_SETTING_NUMBER, "1", 1, 8, 1, nullptr, 0},
    {READER_CORE_SETTING_RENDERER, nullptr, READER_CORE_SETTING_CHOICE, "opengl", 0, 0, 0, READER_CHOICES(RendererChoices)},
    {READER_CORE_SETTING_ASPECT, nullptr, READER_CORE_SETTING_CHOICE, "auto", 0, 0, 0, READER_CHOICES(AspectChoices)},
    {READER_CORE_SETTING_VSYNC, nullptr, READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_FRAME_LIMIT, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 5, nullptr, 0},
    {"ps1.true_color", "True colour (no dithering)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {"ps1.texture_filter", "Texture smoothing", READER_CORE_SETTING_CHOICE, "nearest", 0, 0, 0, READER_CHOICES(FilterChoices)},
    {"ps1.pgxp", "Steadier 3D (PGXP)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {"ps1.widescreen", "Widescreen (a hack: some games)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {"ps1.fast_boot", "Skip the console's start-up screen", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {"ps1.region", "Console region", READER_CORE_SETTING_CHOICE, "auto", 0, 0, 0, READER_CHOICES(RegionChoices)},
    {"ps1.cd_speed", "CD reading speed (times the console's)", READER_CORE_SETTING_CHOICE, "1", 0, 0, 0, READER_CHOICES(CdSpeedChoices)},
    {READER_CORE_SETTING_CRT, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_SPEED, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 1000, 10, nullptr, 0},
};
#undef READER_CHOICES

// The kinds of controller a port may have (DuckStation's controller types), for the program's
// mapping page: the digital pad (the app's default) and the DualShock (sticks, rumble).
const reader_core_device Devices[] = {{"Digital pad", 1, 0b11}, {"DualShock", 2, 0b11}};

reader_core_info MakeInfo() {
    g_number_names.clear();
    for (int i = 0; i < 32; ++i) {
        g_number_names.push_back("r" + std::to_string(i));
    }
    g_register_names.clear();
    for (uint32_t i = 0; i < 32; ++i) {
        g_register_names.push_back({g_number_names[i].c_str(), i, READER_CORE_REGISTER_INTEGER, 32});
        g_register_names.push_back({RegisterNames[i], i, READER_CORE_REGISTER_INTEGER, 32});
    }
    g_register_names.push_back({"hi", 32, READER_CORE_REGISTER_INTEGER, 32});
    g_register_names.push_back({"lo", 33, READER_CORE_REGISTER_INTEGER, 32});

    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "ps1";
    info.name = "Station";
    info.version = "2021-06-07 (Station Reader)";
    info.system = "PlayStation";
    info.short_name = "PS1";
    info.app_key = Traits::AppKey;
    info.app_name = Traits::AppName;
    info.media_prefix = Traits::MediaPrefix;
    info.game_id_pattern = Traits::GameIdPattern;
    info.game_id_upper = Traits::GameIdUpper ? 1 : 0;
    info.game_page = Traits::GamePage;
    info.game_page_upper = Traits::GamePageUpper ? 1 : 0;
    info.game_page_without_hyphens = Traits::GamePageWithoutHyphens ? 1 : 0;
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
    info.has_next_choice = Traits::HasNextChoice ? 1 : 0;
    info.file_types = FileTypes;
    info.file_type_count = static_cast<uint32_t>(std::size(FileTypes));
    info.buttons = Buttons;
    info.button_count = static_cast<uint32_t>(std::size(Buttons));
    info.cpu.family = "mips-r3000a";
    info.cpu.value_count = ValueCount;
    info.cpu.names = g_register_names.data();
    info.cpu.name_count = static_cast<uint32_t>(g_register_names.size());
    info.cpu.hook_alignment = 4;
    info.cpu.thumb_odd = 0;
    info.cpu.highest_address = ~0ULL; // as registers.h: Lua's negative numbers are the top of it
    info.settings = CoreSettings;
    info.setting_count = static_cast<uint32_t>(std::size(CoreSettings));
    // Since 2026-10-24: a game's own card for save sync (asked for before it is opened, every
    // write told), the discs of a set (an .m3u), two ports with a DualShock's sticks and rumble
    // if chosen, savestates answered.
    info.features = READER_CORE_HAS_SAVE_SYNC | READER_CORE_HAS_DISCS | READER_CORE_HAS_RUMBLE | READER_CORE_HAS_STATES;
    info.port_count = 2;
    info.devices = Devices;
    info.device_count = static_cast<uint32_t>(std::size(Devices));
    info.state_slots = 10;
    return info;
}

reader_core_info g_info;

// ---- DuckStation's settings, in memory: nothing is written ----

class MemorySettings final : public SettingsInterface {
public:
    bool Save() override { return true; }
    void Clear() override { values.clear(); }
    int GetIntValue(const char* section, const char* key, int default_value) override {
        const auto found = Find(section, key);
        return found ? std::atoi(found->c_str()) : default_value;
    }
    float GetFloatValue(const char* section, const char* key, float default_value) override {
        const auto found = Find(section, key);
        return found ? static_cast<float>(std::atof(found->c_str())) : default_value;
    }
    bool GetBoolValue(const char* section, const char* key, bool default_value) override {
        const auto found = Find(section, key);
        return found ? (*found == "true" || *found == "1") : default_value;
    }
    std::string GetStringValue(const char* section, const char* key, const char* default_value) override {
        const auto found = Find(section, key);
        return found ? *found : std::string(default_value);
    }
    void SetIntValue(const char* section, const char* key, int value) override {
        values[Key(section, key)] = std::to_string(value);
    }
    void SetFloatValue(const char* section, const char* key, float value) override {
        values[Key(section, key)] = std::to_string(value);
    }
    void SetBoolValue(const char* section, const char* key, bool value) override {
        values[Key(section, key)] = value ? "true" : "false";
    }
    void SetStringValue(const char* section, const char* key, const char* value) override {
        values[Key(section, key)] = value;
    }
    std::vector<std::string> GetStringList(const char* section, const char* key) override {
        const auto found = lists.find(Key(section, key));
        return found == lists.end() ? std::vector<std::string>{} : found->second;
    }
    void SetStringList(const char* section, const char* key, const std::vector<std::string>& items) override {
        lists[Key(section, key)] = items;
    }
    bool RemoveFromStringList(const char* section, const char* key, const char* item) override {
        auto& list = lists[Key(section, key)];
        const auto at = std::find(list.begin(), list.end(), item);
        if (at == list.end()) {
            return false;
        }
        list.erase(at);
        return true;
    }
    bool AddToStringList(const char* section, const char* key, const char* item) override {
        auto& list = lists[Key(section, key)];
        if (std::find(list.begin(), list.end(), item) != list.end()) {
            return false;
        }
        list.emplace_back(item);
        return true;
    }
    void DeleteValue(const char* section, const char* key) override { values.erase(Key(section, key)); }
    void ClearSection(const char* section) override {
        const std::string prefix = std::string(section) + "/";
        for (auto it = values.begin(); it != values.end();) {
            it = it->first.rfind(prefix, 0) == 0 ? values.erase(it) : std::next(it);
        }
    }

private:
    static std::string Key(const char* section, const char* key) { return std::string(section) + "/" + key; }
    std::optional<std::string> Find(const char* section, const char* key) const {
        const auto found = values.find(Key(section, key));
        return found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
    }
    std::map<std::string, std::string> values;
    std::map<std::string, std::vector<std::string>> lists;
};

#if defined(__ANDROID__)
/// The sound handed to the app, which plays every core's: the desktop's cubeb has no place here.
/// (What the reader keeps of it goes by Reader::Emu::OnAudio, as on the desktop.)
class HostAudioStream final : public AudioStream {
protected:
    bool OpenDevice() override { return true; }
    void PauseDevice(bool) override {}
    void CloseDevice() override {}
    void FramesAvailable() override {
        const u32 frames = std::min<u32>(GetSamplesAvailable(), static_cast<u32>(m_out.size() / m_channels));
        ReadFrames(m_out.data(), frames, true);
        if (g_host != nullptr && g_host->struct_size >= offsetof(reader_core_host, play) + sizeof(g_host->play) &&
            g_host->play != nullptr) {
            g_host->play(g_host->user, m_out.data(), frames, m_output_sample_rate);
        }
    }

private:
    std::array<SampleType, 8192> m_out{};
};
#endif

// ---- DuckStation's host, without a frontend ----

class StationHost final : public HostInterface {
public:
    StationHost() {
        m_user_directory = g_paths.data;
        m_settings.SetStringValue("BIOS", "SearchDirectory", g_paths.system.c_str());
        SetDefaultSettings(m_settings);
        // The defaults that are this core's: the system folder Reader Home gives (where the
        // user puts their console's BIOS), OpenGL (the CRT picture is drawn there), each
        // game's own memory card, the window's own refresh.
        m_settings.SetStringValue("BIOS", "SearchDirectory", g_paths.system.c_str());
        m_settings.SetStringValue("GPU", "Renderer", Settings::GetRendererName(GPURenderer::HardwareOpenGL));
        m_settings.SetStringValue("MemoryCards", "Directory", g_paths.saves.c_str());
        m_settings.SetBoolValue("Display", "VSync", false);
        m_settings.SetBoolValue("Main", "StartPaused", false);
        m_settings.SetBoolValue("Main", "SaveStateOnExit", false);
        // Two controllers (the program's first two), each a digital pad until it chooses.
        m_settings.SetStringValue("Controller1", "Type", "DigitalController");
        m_settings.SetStringValue("Controller2", "Type", "DigitalController");
        LoadSettings(m_settings);
    }

    /// A port's kind of controller (reader_core_device.id: 1 digital, 2 DualShock); Apply puts it
    /// into the running console.
    void SetControllerType(uint32_t port, uint32_t device) {
        m_settings.SetStringValue(port == 0 ? "Controller1" : "Controller2", "Type",
                                  device == 2 ? "AnalogController" : "DigitalController");
    }

    std::string GetStringSettingValue(const char* section, const char* key, const char* default_value) override {
        return m_settings.GetStringValue(section, key, default_value);
    }
    std::vector<std::string> GetSettingStringList(const char* section, const char* key) override {
        return m_settings.GetStringList(section, key);
    }
    std::string GetBIOSDirectory() override { return g_paths.system; }
    std::string GetShaderCacheBasePath() const override { return g_paths.data + "/cache/"; }
    std::unique_ptr<ByteStream> OpenPackageFile(const char*, u32) override { return {}; }

    void ReportError(const char* message) override { Say(READER_CORE_LOG_ERROR, message); }
    void ReportMessage(const char* message) override { Say(READER_CORE_LOG_INFO, message); }
    bool ConfirmMessage(const char* message) override {
        Say(READER_CORE_LOG_WARNING, message);
        return true;
    }
    void AddOSDMessage(std::string message, float) override { Say(READER_CORE_LOG_INFO, message); }

    void GetGameInfo(const char* path, CDImage* image, std::string* code, std::string* title) override {
        // The disc's serial, as the app's game list has it (from the boot file's name).
        if (image != nullptr) {
            *code = System::GetGameCodeForImage(image, true);
        }
        std::string name = path != nullptr ? path : "";
        if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
        if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
            name = name.substr(0, dot);
        }
        *title = name;
    }

    bool AcquireHostDisplay() override { return m_display != nullptr; }
    void ReleaseHostDisplay() override {}
    std::unique_ptr<AudioStream> CreateAudioStream(AudioBackend backend) override {
        if (backend == AudioBackend::Null) {
            return AudioStream::CreateNullAudioStream();
        }
#if defined(__ANDROID__)
        return std::make_unique<HostAudioStream>();
#else
        return CubebAudioStream::Create();
#endif
    }

    /// The display in Reader Home's window, on this thread (the game's).
    bool CreateDisplay(const WindowInfo& wi) {
        auto display = std::make_unique<FrontendCommon::OpenGLHostDisplay>();
        if (!display->CreateRenderDevice(wi, {}, false, false) ||
            !display->InitializeRenderDevice(GetShaderCacheBasePath(), false, false)) {
            display->DestroyRenderDevice();
            return false;
        }
        display->SetVSync(false);
        m_display = std::move(display);
        return true;
    }
    void DestroyDisplay() {
        if (m_display) {
            m_display->DestroyRenderDevice();
            m_display.reset();
        }
    }
    /// Settings by the interface's keys, into DuckStation's own (in memory), then into the
    /// running console the way DuckStation's frontends change them.
    void Apply(const std::vector<std::pair<std::string, std::string>>& values) {
        for (const auto& [key, value] : values) {
            ApplySetting(key, value);
        }
        Settings old = g_settings;
        LoadSettings(m_settings);
        // How fast: the profile writer's speed, unless the frame limit is off (as fast as it goes).
        g_settings.emulation_speed = m_frame_limit ? m_speed / 100.0f : 0.0f;
        if (System::IsValid()) {
            FixIncompatibleSettings(false);
            CheckForSettingsChanges(old);
        }
        if (m_audio_stream) {
            m_audio_stream->SetOutputVolume(g_settings.audio_output_volume);
        }
        if (m_display) {
            m_display->SetVSync(g_settings.video_sync_enabled);
        }
    }
    bool Save(const std::string& path) { return SaveState(path.c_str()); }

private:
    void ApplySetting(const std::string& key, const std::string& value) {
        const auto on = [&value] { return value == "1"; };
        const auto number = [&value] { return std::atoi(value.c_str()); };
        if (key == READER_CORE_SETTING_RESOLUTION) {
            m_settings.SetIntValue("GPU", "ResolutionScale", std::clamp(number(), 1, 16));
        } else if (key == READER_CORE_SETTING_RENDERER) {
            m_settings.SetStringValue("GPU", "Renderer", Settings::GetRendererName(
                value == "software" ? GPURenderer::Software : GPURenderer::HardwareOpenGL));
        } else if (key == READER_CORE_SETTING_ASPECT) {
            m_settings.SetBoolValue("Display", "Stretch", value == "stretch");
            m_settings.SetStringValue("Display", "AspectRatio",
                                      value == "4:3" ? "4:3" : value == "16:9" ? "16:9" : "Auto (Game Native)");
        } else if (key == READER_CORE_SETTING_VSYNC) {
            m_settings.SetBoolValue("Display", "VSync", on());
        } else if (key == READER_CORE_SETTING_FRAME_LIMIT) {
            m_frame_limit = on();
        } else if (key == READER_CORE_SETTING_VOLUME) {
            m_settings.SetIntValue("Audio", "OutputVolume", std::clamp(number(), 0, 100));
        } else if (key == READER_CORE_SETTING_SPEED) {
            m_speed = std::max(0.0f, static_cast<float>(std::atof(value.c_str())));
            if (m_speed == 0.0f) { // as fast as it goes
                m_frame_limit = false;
                m_speed = 100.0f;
            } else {
                m_frame_limit = true;
            }
        } else if (key == "ps1.true_color") {
            m_settings.SetBoolValue("GPU", "TrueColor", on());
        } else if (key == "ps1.texture_filter") {
            m_settings.SetStringValue("GPU", "TextureFilter", value == "bilinear" ? "Bilinear"
                                                              : value == "jinc2"  ? "JINC2"
                                                              : value == "xbr"    ? "xBR"
                                                                                  : "Nearest");
        } else if (key == "ps1.pgxp") {
            m_settings.SetBoolValue("GPU", "PGXPEnable", on());
        } else if (key == "ps1.widescreen") {
            m_settings.SetBoolValue("GPU", "WidescreenHack", on());
        } else if (key == "ps1.fast_boot") {
            m_settings.SetBoolValue("BIOS", "PatchFastBoot", on());
        } else if (key == "ps1.region") {
            m_settings.SetStringValue("Console", "Region", value == "ntsc-j"   ? "NTSC-J"
                                                           : value == "ntsc-u" ? "NTSC-U"
                                                           : value == "pal"    ? "PAL"
                                                                               : "Auto");
        } else if (key == "ps1.cd_speed") {
            m_settings.SetIntValue("CDROM", "ReadSpeedup", std::clamp(number(), 1, 10));
        }
    }

    float m_speed = 100.0f;
    bool m_frame_limit = true;

public:
private:
    MemorySettings m_settings;
};

std::unique_ptr<StationHost> g_station;

// ---- The game's thread ----

std::thread g_thread;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_paused{false};
std::mutex g_queue_mutex;
std::deque<std::function<void()>> g_queue;
std::mutex g_window_mutex;
reader_core_window g_window{};
std::atomic<uint64_t> g_reader_buttons{0};
uint64_t g_frames_run = 0; ///< On the game's thread
/// The window's size changed since the display last took it.
std::atomic<bool> g_window_stale{true};
/// The game thread's own connection to the X server (MakeWindowInfo), or none.
void* g_x11_display = nullptr;
#if defined(__ANDROID__)
void* g_surface = nullptr; ///< The Android surface the display draws on, on the game's thread
#endif
std::atomic<bool> g_any_hooks{false};
std::atomic<bool> g_dispatcher_stale{false};
std::thread::id g_game_thread;

struct Grab {
    uint32_t what;
    reader_core_grab_fn done;
    void* context;
};
std::vector<Grab> g_grabs;
std::mutex g_grabs_mutex;

void RunLater(std::function<void()> task) {
    std::scoped_lock lock{g_queue_mutex};
    g_queue.push_back(std::move(task));
}

void RunQueued() {
    for (;;) {
        std::function<void()> task;
        {
            std::scoped_lock lock{g_queue_mutex};
            if (g_queue.empty()) {
                return;
            }
            task = std::move(g_queue.front());
            g_queue.pop_front();
        }
        task();
    }
}

std::vector<Grab> TakeGrabs(uint32_t what) {
    std::scoped_lock lock{g_grabs_mutex};
    std::vector<Grab> out;
    for (auto it = g_grabs.begin(); it != g_grabs.end();) {
        if (it->what == what) {
            out.push_back(*it);
            it = g_grabs.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

/// The window's finished picture, just before it is shown (the OpenGL display's hook).
void BeforePresent(int width, int height) {
    const std::vector<Grab> grabs = TakeGrabs(READER_CORE_GRAB_WINDOW);
    if (grabs.empty()) {
        return;
    }
    std::vector<uint8_t> rows(static_cast<std::size_t>(width) * height * 4);
    if (width > 0 && height > 0) {
        // The window's back buffer, whatever the console's renderer left bound for reading.
        GLint bound = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(bound));
    }
    // OpenGL's rows go upwards.
    std::vector<uint8_t> picture(rows.size());
    for (int y = 0; y < height; ++y) {
        std::memcpy(picture.data() + static_cast<std::size_t>(y) * width * 4,
                    rows.data() + static_cast<std::size_t>(height - 1 - y) * width * 4, static_cast<std::size_t>(width) * 4);
    }
    for (const Grab& grab : grabs) {
        grab.done(grab.context, width > 0 ? picture.data() : nullptr, static_cast<uint32_t>(std::max(width, 0)),
                  static_cast<uint32_t>(std::max(height, 0)), static_cast<uint32_t>(std::max(width, 0)) * 4);
    }
}

/// The game's picture as it was last drawn, in its own size (cards).
void GrabPictures() {
    const std::vector<Grab> grabs = TakeGrabs(READER_CORE_GRAB_PICTURE);
    if (grabs.empty()) {
        return;
    }
    HostDisplay* display = g_station->GetDisplay();
    std::vector<u32> pixels;
    uint32_t width = 0, height = 0;
    if (display != nullptr && System::IsValid()) {
        width = static_cast<uint32_t>(std::max(display->GetDisplayWidth(), 0));
        height = static_cast<uint32_t>(std::max(display->GetDisplayHeight(), 0));
        if (width == 0 || height == 0 || !display->WriteDisplayTextureToBuffer(&pixels, width, height) ||
            pixels.size() < static_cast<std::size_t>(width) * height) {
            width = height = 0;
        }
    }
    for (const Grab& grab : grabs) {
        grab.done(grab.context, width > 0 ? reinterpret_cast<const uint8_t*>(pixels.data()) : nullptr, width, height,
                  width * 4);
    }
}

bool HostHas(std::size_t end_of_field);
#define STATION_HOST_HAS(field) (HostHas(offsetof(reader_core_host, field) + sizeof(g_host->field)) && g_host->field != nullptr)

/// The program's controllers (and the reader's presses, on the first) onto the console's two:
/// the buttons, a DualShock's sticks; and a DualShock's motors told back as rumble.
void ApplyPad() {
    static uint64_t applied[2] = {~0ULL, ~0ULL};
    static Controller* applied_to[2] = {};
    static uint16_t told[2][2] = {};
    for (uint32_t port = 0; port < 2; ++port) {
        reader_core_pad pad{};
        if (g_host->pad != nullptr) {
            g_host->pad(g_host->user, port, &pad);
        }
        const uint64_t buttons = pad.buttons | (port == 0 ? g_reader_buttons.load() : 0);
        Controller* controller = g_pad.GetController(port);
        if (controller == nullptr) {
            continue;
        }
        if (controller != applied_to[port]) {
            applied_to[port] = controller; // a new controller (another kind chosen): every button anew
            applied[port] = ~0ULL;
        }
        if (buttons != applied[port]) {
            for (int bit = 0; bit < 16; ++bit) {
                const bool down = ((buttons >> bit) & 1) != 0;
                if (applied[port] == ~0ULL || down != (((applied[port] >> bit) & 1) != 0)) {
                    controller->SetButtonState(bit, down);
                }
            }
            applied[port] = buttons;
        }
        if (controller->GetType() == ControllerType::AnalogController) {
            for (int axis = 0; axis < 4; ++axis) {
                controller->SetAxisState(axis, static_cast<float>(pad.sticks[axis]) / 32767.0f);
            }
        }
        if (STATION_HOST_HAS(rumble)) {
            uint16_t motors[2] = {};
            for (u32 motor = 0; motor < std::min<u32>(2, controller->GetVibrationMotorCount()); ++motor) {
                motors[motor] = static_cast<uint16_t>(std::clamp(controller->GetVibrationMotorStrength(motor), 0.0f, 1.0f) * 65535.0f);
            }
            if (motors[0] != told[port][0] || motors[1] != told[port][1]) {
                told[port][0] = motors[0];
                told[port][1] = motors[1];
                g_host->rumble(g_host->user, port, motors[0], motors[1]);
            }
        }
    }
}

// ---- Discs: a set's discs (an .m3u), as the game's thread last saw them ----

std::atomic<uint32_t> g_disc_count{0};
std::atomic<int32_t> g_disc_current{-1};
std::mutex g_disc_labels_mutex;
std::vector<std::string> g_disc_labels;

void RefreshDiscs() {
    std::vector<std::string> labels;
    int32_t current = -1;
    if (System::IsValid() && System::HasMedia()) {
        if (System::HasMediaSubImages()) {
            for (u32 i = 0; i < System::GetMediaSubImageCount(); ++i) {
                std::string title = System::GetMediaSubImageTitle(i);
                labels.push_back(title.empty() ? "Disc " + std::to_string(i + 1) : title);
            }
            current = static_cast<int32_t>(System::GetMediaSubImageIndex());
        } else {
            labels.push_back("Disc 1");
            current = 0;
        }
    }
    g_disc_count = static_cast<uint32_t>(labels.size());
    g_disc_current = current;
    std::scoped_lock lock{g_disc_labels_mutex};
    g_disc_labels = std::move(labels);
}

/// Tells the program where the picture is in its window, when that changed.
void TellPictureRect() {
    static std::tuple<s32, s32, s32, s32> told{-1, -1, -1, -1};
    HostDisplay* display = g_station->GetDisplay();
    if (display == nullptr || g_host->picture_rect == nullptr ||
        g_host->struct_size < offsetof(reader_core_host, picture_rect) + sizeof(g_host->picture_rect)) {
        return;
    }
    const auto rect = display->CalculateDrawRect(display->GetWindowWidth(), display->GetWindowHeight(), 0);
    if (rect != told) {
        told = rect;
        const auto [x, y, width, height] = rect;
        g_host->picture_rect(g_host->user, x, y, width, height);
    }
}

std::optional<WindowInfo> MakeWindowInfo() {
    reader_core_window window;
    {
        std::scoped_lock lock{g_window_mutex};
        window = g_window;
    }
    WindowInfo wi;
    wi.surface_width = window.width;
    wi.surface_height = window.height;
    wi.surface_scale = window.scale > 0 ? window.scale : 1.0f;
    switch (window.kind) {
#if defined(__ANDROID__)
    case READER_CORE_WINDOW_ANDROID:
        wi.type = window.handle != nullptr ? WindowInfo::Type::Android : WindowInfo::Type::Surfaceless;
        wi.window_handle = window.handle;
        return wi;
#elif defined(__linux__)
    case READER_CORE_WINDOW_X11: {
        // A connection of this thread's own to the same display: Xlib's are not to be shared
        // between threads, and the program's is its toolkit's.
        Display* display = XOpenDisplay(nullptr);
        if (display == nullptr) {
            return std::nullopt;
        }
        wi.type = WindowInfo::Type::X11;
        wi.display_connection = display;
        g_x11_display = display;
        wi.window_handle = window.handle;
        return wi;
    }
#endif
#if defined(_WIN32)
    case READER_CORE_WINDOW_WIN32:
        wi.type = WindowInfo::Type::Win32;
        wi.window_handle = window.handle;
        return wi;
#endif
    default:
        return std::nullopt;
    }
}

void GameThread(std::string path) {
    g_game_thread = std::this_thread::get_id();
    const std::optional<WindowInfo> wi = MakeWindowInfo();
    if (!wi || !g_station->CreateDisplay(*wi)) {
        Say(READER_CORE_LOG_ERROR, "Station: no OpenGL picture could be made in the window given");
        if (g_host->game_stopped != nullptr) {
            g_host->game_stopped(g_host->user);
        }
        return;
    }
    g_reader_before_present = &BeforePresent;
    SystemBootParameters boot(path);
    if (!g_station->BootSystem(boot)) {
        Say(READER_CORE_LOG_ERROR, "Station: the console did not start " + path);
        g_station->DestroyDisplay();
        if (g_host->game_stopped != nullptr) {
            g_host->game_stopped(g_host->user);
        }
        return;
    }
    g_window_stale = true;
#if defined(__ANDROID__)
    g_surface = wi->window_handle;
#endif
    while (!g_quit.load()) {
        RunQueued();
        // The display's own window inside the program's takes the program's window's size
        // (it was made at whatever size that one had then).
        if (g_window_stale.exchange(false)) {
            reader_core_window window;
            {
                std::scoped_lock lock{g_window_mutex};
                window = g_window;
            }
#if defined(__ANDROID__)
            // The app's surface goes when it is sent to the back and a new one comes when it
            // returns: the context stays, with all that was made in it.
            if (window.handle != g_surface) {
                g_surface = window.handle;
                if (const std::optional<WindowInfo> changed = MakeWindowInfo()) {
                    g_station->GetDisplay()->ChangeRenderWindow(*changed);
                }
                continue;
            }
#endif
            g_station->GetDisplay()->ResizeRenderWindow(static_cast<s32>(window.width), static_cast<s32>(window.height));
#if defined(__linux__) && !defined(__ANDROID__)
            // The resize is a request on this thread's own connection: sent now, so that the
            // picture's surface has the new size at the next frame.
            if (g_x11_display != nullptr) {
                XSync(static_cast<Display*>(g_x11_display), 0);
            }
#endif
        }
        if (g_dispatcher_stale.exchange(false) && System::IsValid()) {
            CPU::UpdateDebugDispatcherFlag();
        }
        if (System::IsShutdown()) {
            break;
        }
        const bool running = System::IsRunning();
        if (running) {
            ApplyPad();
            System::RunFrame();
            if (++g_frames_run % 60 == 0) {
                RefreshDiscs(); // the game may change its own disc
            }
        }
        GrabPictures();
        g_station->GetDisplay()->Render();
        TellPictureRect();
        if (running) {
            System::UpdatePerformanceCounters();
            if (g_settings.emulation_speed > 0.0f) {
                System::Throttle();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    if (!System::IsShutdown()) {
        g_station->DestroySystem(); // the reader hears of it (Reader::Emu::OnShutdown)
    }
    // Pictures asked for and never drawn: none.
    for (uint32_t what : {READER_CORE_GRAB_PICTURE, READER_CORE_GRAB_WINDOW}) {
        for (const Grab& grab : TakeGrabs(what)) {
            grab.done(grab.context, nullptr, 0, 0, 0);
        }
    }
    g_reader_before_present = nullptr;
    g_station->DestroyDisplay();
#if defined(__linux__) && !defined(__ANDROID__)
    if (g_x11_display != nullptr) {
        XCloseDisplay(static_cast<Display*>(g_x11_display));
        g_x11_display = nullptr;
    }
#endif
}

// ---- The game as the reader is told of it (reader/core/emulator.cpp's part) ----

struct StartingGame {
    std::string game_id;
    std::string title;
    u32 base = 0x80010000;
    u32 size = 0;
    std::string build = "00000000";
};
std::optional<StartingGame> g_pending_game;
std::string g_running_game;
s32 g_picture_width = 0, g_picture_height = 0;

u32 Crc32(const u8* data, std::size_t size) {
    u32 crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

u32 Le32(const std::vector<u8>& data, std::size_t at) {
    return data.size() < at + 4 ? 0
                                : static_cast<u32>(data[at]) | static_cast<u32>(data[at + 1]) << 8 |
                                      static_cast<u32>(data[at + 2]) << 16 | static_cast<u32>(data[at + 3]) << 24;
}

bool HostHas(std::size_t end_of_field) {
    return g_host != nullptr && g_host->struct_size >= end_of_field;
}

void UpdateAnyHooks() {
    if (HostHas(offsetof(reader_core_host, hook_count) + sizeof(g_host->hook_count)) && g_host->hook_count != nullptr) {
        g_any_hooks = g_host->hook_count(g_host->user) != 0;
    } else {
        g_any_hooks = true; // a program that cannot say: every instruction is asked about
    }
}

void StartGame(const StartingGame& game) {
    if (g_host->game_loading != nullptr) {
        g_host->game_loading(g_host->user);
    }
    if (g_host->module_loaded != nullptr) {
        g_host->module_loaded(g_host->user, "main", game.base, game.size, game.build.c_str());
    }
    g_running_game = game.game_id;
    g_picture_width = g_picture_height = 0;
    // The profile goes in now, its hooks with it.
    if (g_host->game_loaded != nullptr) {
        g_host->game_loaded(g_host->user, game.game_id.c_str(), game.title.c_str());
    }
    UpdateAnyHooks();
    if (System::IsValid()) {
        CPU::UpdateDebugDispatcherFlag();
    }
}

void GameStopped() {
    g_pending_game.reset();
    if (g_running_game.empty()) {
        return;
    }
    g_running_game.clear();
    g_any_hooks = false;
    if (g_host->game_stopped != nullptr) {
        g_host->game_stopped(g_host->user);
    }
}

} // namespace

// ---- What the OpenGL display takes from DuckStation's frontend ----

// The display names its full-screen modes with the frontend's helper (opengl_host_display.cpp):
// said here, so that the frontend (common_host_interface.cpp and its full-screen interface,
// with the app's PlayStation home in it) stays out of the core.
std::string CommonHostInterface::GetFullscreenModeString(u32 width, u32 height, float refresh_rate) {
    return StringUtil::StdStringFromFormat("%u x %u @ %f hz", width, height, refresh_rate);
}

// ---- What DuckStation's files call (reader/core/emulator.h) ----

namespace Reader::Emu {

void OnRunningGameChanged(const std::string& code, const std::string& title, void* image) {
    if (!g_running_game.empty() && g_running_game == code) {
        return;
    }
    GameStopped();
    if (code.empty()) {
        return;
    }
    StartingGame game{code, title};
    std::string executable_name;
    std::vector<u8> executable;
    auto* disc = static_cast<CDImage*>(image);
    if (disc != nullptr && System::ReadExecutableFromImage(disc, &executable_name, &executable) &&
        executable.size() > 0x800) {
        char build[16];
        std::snprintf(build, sizeof(build), "%08X", Crc32(executable.data(), executable.size()));
        game.build = build;
        game.base = Le32(executable, 0x18);
        game.size = Le32(executable, 0x1C);
    }
    // A boot names its disc before it makes the console's memory: such a game starts on the
    // first frame (a profile's hooks need that memory).
    if (Bus::g_ram == nullptr) {
        g_pending_game = std::move(game);
        return;
    }
    g_pending_game.reset();
    StartGame(game);
}

void OnShutdown() {
    // The cards are written now, while the game is still there for save sync: DuckStation would
    // write a changed one only as it closes the cards, after the game had stopped for the program.
    for (u32 slot = 0; slot < 2; ++slot) {
        if (MemoryCard* card = g_pad.GetMemoryCard(slot); card != nullptr) {
            card->SaveNowIfChanged();
        }
    }
    GameStopped();
}

void OnFrame() {
    if (g_pending_game && Bus::g_ram != nullptr) {
        const StartingGame game = std::move(*g_pending_game);
        g_pending_game.reset();
        StartGame(game);
    }
    if (g_host->frame == nullptr) {
        return;
    }
    // The picture's size as the console shows it now (320 by 240, 224, 480 and so on): what a
    // profile's places are in.
    if (const HostDisplay* display = g_host_interface ? g_host_interface->GetDisplay() : nullptr) {
        const s32 width = display->GetDisplayWidth();
        const s32 height = display->GetDisplayHeight();
        if (width > 0 && height > 0) {
            g_picture_width = width;
            g_picture_height = height;
        }
    }
    g_host->frame(g_host->user, static_cast<uint32_t>(g_picture_width > 0 ? g_picture_width : 320),
                  static_cast<uint32_t>(g_picture_height > 0 ? g_picture_height : 240));
}

void OnStateLoaded() {
    if (g_host->state_loaded != nullptr) {
        g_host->state_loaded(g_host->user);
    }
}

bool AnyHooks() {
    return g_any_hooks.load(std::memory_order_relaxed);
}

void HookAt(uint32_t pc) {
    if (g_host->is_hooked == nullptr || g_host->hook == nullptr || g_host->is_hooked(g_host->user, pc) == 0) {
        return;
    }
    uint64_t values[ValueCount];
    for (std::size_t i = 0; i < 32; ++i) {
        values[i] = CPU::g_state.regs.r[i];
    }
    // A load the instruction before began reaches its register after this one: the function a
    // hook is at sees it from its next instruction on, and so does the profile.
    if (CPU::g_state.load_delay_reg != CPU::Reg::count) {
        values[static_cast<std::size_t>(CPU::g_state.load_delay_reg)] = CPU::g_state.load_delay_value;
    }
    values[32] = CPU::g_state.regs.hi;
    values[33] = CPU::g_state.regs.lo;
    const reader_core_registers registers{pc, values, ValueCount};
    g_host->hook(g_host->user, &registers);
}

void BeforeMemoryCard(int slot, const std::string& path) {
    // The game's own card is in Furoshiki's saves folder (the memory card folder given); save
    // sync is the program's: it fetches the newest card now, before the card is opened, and
    // hears of each write (OnMemoryCardWritten).
    if (slot != 0 || !STATION_HOST_HAS(saves_opening)) {
        return;
    }
    const std::string& code = System::GetRunningCode();
    // The card is named by its serial: a later disc's is its first disc's (system.cpp).
    const std::string card = Reader::CardSerialOf(code);
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FILE, card.c_str(),
                                          path.c_str()};
    const reader_core_saves saves{sizeof(reader_core_saves), code.c_str(), System::GetRunningTitle().c_str(), 0,
                                  &where, 1};
    g_host->saves_opening(g_host->user, &saves);
}

void OnMemoryCardWritten(const std::string& path) {
    if (g_host->saved != nullptr) {
        g_host->saved(g_host->user, path.c_str());
    }
    if (STATION_HOST_HAS(saves_written)) {
        g_host->saves_written(g_host->user, System::GetRunningCode().c_str(), path.c_str(), READER_CORE_WRITE_GAME);
    }
}

std::string MemoryCardPathOf(const std::string& game_id) {
    // A game's own card in the first slot where the app keeps it ("saves/<serial>/<serial>_1.mcd"
    // of its folder): here in the saves folder the program gave, so that a card can be brought
    // over from the app as it is. A serial that cannot name a folder: DuckStation's own place.
    if (game_id.empty() || g_paths.saves.empty() ||
        game_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos) {
        return {};
    }
    const std::string folder = g_paths.saves + "/" + game_id;
    FileSystem::CreateDirectory(folder.c_str(), true);
    return folder + "/" + game_id + "_1.mcd";
}

void OnAudio(const int16_t* samples, std::size_t frames) {
    if (g_host->audio != nullptr) {
        g_host->audio(g_host->user, samples, frames, HostInterface::AUDIO_SAMPLE_RATE);
    }
}

} // namespace Reader::Emu

// ---- The core's table (cores/reader_core.h) ----

namespace {

constexpr u32 SegmentMask = 0x1FFFFFFF;
constexpr u32 ScratchpadBase = 0x1F800000;

bool InKuseg(u64 address) {
    return address <= 0xFFFFFFFFULL && address < 0xC0000000ULL;
}

const reader_core_info* Info() {
    return &g_info;
}

int32_t Init(const reader_core_host* host, const reader_core_paths* paths) {
    if (host == nullptr || paths == nullptr || host->struct_size < offsetof(reader_core_host, pad) + sizeof(host->pad)) {
        return -1;
    }
    g_host = host;
    const auto path = [](const char* given) { return std::string(given != nullptr ? given : ""); };
    g_paths = Paths{path(paths->data), path(paths->saves), path(paths->states), path(paths->system)};
    for (const std::string& folder : {g_paths.data, g_paths.data + "/cache", g_paths.saves, g_paths.states, g_paths.system}) {
        if (!folder.empty()) {
            FileSystem::CreateDirectory(folder.c_str(), true);
        }
    }
    // DuckStation's log goes to the program's, from Info up.
    Log::SetConsoleOutputParams(false);
    Log::RegisterCallback(
        [](void*, const char* channel, const char*, LOGLEVEL level, const char* message) {
            if (level > LOGLEVEL_INFO) {
                return;
            }
            const uint32_t kind = level <= LOGLEVEL_ERROR ? READER_CORE_LOG_ERROR
                                  : level <= LOGLEVEL_WARNING ? READER_CORE_LOG_WARNING
                                                              : READER_CORE_LOG_INFO;
            Say(kind, std::string(channel) + ": " + message);
        },
        nullptr);
    Log::SetFilterLevel(LOGLEVEL_INFO);
    // The console's picture has 240 lines (480 when interlaced): what the CRT picture's
    // "Lines: as the console" means.
    Reader::Crt::SetNativeLines(240);
    g_station = std::make_unique<StationHost>();
    return 0;
}

void Stop() {
    if (g_thread.joinable()) {
        g_quit = true;
        g_thread.join();
    }
    g_quit = false;
    g_paused = false;
    std::scoped_lock lock{g_queue_mutex};
    g_queue.clear();
}

void Shutdown() {
    Stop();
}

int32_t SetWindow(const reader_core_window* window) {
    if (window == nullptr || window->struct_size < offsetof(reader_core_window, scale) + sizeof(window->scale)) {
        return -1;
    }
    {
        std::scoped_lock lock{g_window_mutex};
        g_window = *window;
    }
    g_window_stale = true;
    return 0;
}

void ApplySettings(const reader_core_setting_value* values, uint32_t count) {
    std::optional<std::string> crt;
    std::vector<std::pair<std::string, std::string>> settings;
    for (uint32_t i = 0; i < count; ++i) {
        if (values[i].key == nullptr || values[i].value == nullptr) {
            continue;
        }
        const std::string key = values[i].key, value = values[i].value;
        if (key == READER_CORE_SETTING_CRT) {
            crt = value;
        } else {
            settings.emplace_back(key, value);
        }
    }
    auto apply = [crt = std::move(crt), settings = std::move(settings)] {
        if (crt) {
            Reader::Crt::Deserialize(*crt);
        }
        if (!settings.empty() && g_station != nullptr) {
            g_station->Apply(settings);
        }
    };
    // Between frames, on the game's thread, while a game runs.
    if (g_thread.joinable()) {
        RunLater(std::move(apply));
    } else {
        apply();
    }
}

int32_t LoadGame(const char* game) {
    if (g_station == nullptr || game == nullptr || *game == '\0') {
        return -1;
    }
    Stop();
    g_thread = std::thread(GameThread, std::string(game));
    return 0;
}

void Pause(int32_t paused) {
    g_paused = paused != 0;
    RunLater([paused] {
        if (System::IsValid() && System::IsPaused() != (paused != 0)) {
            g_station->PauseSystem(paused != 0);
        }
    });
}

std::string StatePath(int32_t slot) {
    return g_paths.states + "/" + (g_running_game.empty() ? std::string("console") : g_running_game) + "_" +
           std::to_string(slot) + ".sav";
}

void StateDone(uint32_t what, int32_t slot, bool done, const char* why) {
    if (STATION_HOST_HAS(state_done)) {
        g_host->state_done(g_host->user, what, slot, done ? 0 : 1, done ? "" : why);
    }
}

int32_t SaveStateSlot(int32_t slot) {
    if (!g_thread.joinable()) {
        return -1;
    }
    RunLater([slot] {
        const bool done = System::IsValid() && g_station->Save(StatePath(slot));
        StateDone(0, slot, done, "the emulator wrote no state");
    });
    return 0;
}

int32_t LoadStateSlot(int32_t slot) {
    if (!g_thread.joinable()) {
        return -1;
    }
    RunLater([slot] {
        const std::string path = StatePath(slot);
        const bool there = FileSystem::FileExists(path.c_str());
        const bool done = System::IsValid() && there && g_station->LoadState(path.c_str());
        StateDone(1, slot, done, there ? "the emulator did not take it" : "nothing in that slot");
    });
    return 0;
}

int32_t StateInfo(int32_t slot, reader_core_state_info* info) {
    if (info == nullptr) {
        return -1;
    }
    FILESYSTEM_STAT_DATA stat{};
    const bool there = FileSystem::StatFile(StatePath(slot).c_str(), &stat);
    info->exists = there ? 1 : 0;
    info->saved_ms = there ? static_cast<int64_t>(stat.ModificationTime.AsUnixTimestamp()) * 1000 : 0;
    return 0;
}

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    // A later disc of a game on several discs keeps its saves on the first disc's card.
    const std::string card = game_id != nullptr ? Reader::CardSerialOf(game_id) : std::string{};
    const std::string path = !card.empty() ? Reader::Emu::MemoryCardPathOf(card) : std::string{};
    if (path.empty() || each == nullptr) {
        return -1;
    }
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FILE, card.c_str(),
                                          path.c_str()};
    each(context, &where);
    return 1;
}

uint32_t DiscCount() {
    return g_disc_count.load();
}

int32_t DiscCurrent() {
    return g_disc_current.load();
}

int32_t DiscLabel(uint32_t index, char* label, size_t size) {
    std::scoped_lock lock{g_disc_labels_mutex};
    if (index >= g_disc_labels.size() || label == nullptr || size == 0) {
        return -1;
    }
    std::snprintf(label, size, "%s", g_disc_labels[index].c_str());
    return 0;
}

int32_t DiscInsert(int32_t index) {
    if (!g_thread.joinable() || index < -1) {
        return -1;
    }
    RunLater([index] {
        bool done = false;
        if (System::IsValid()) {
            if (index < 0) {
                System::RemoveMedia();
                done = true;
            } else if (System::HasMediaSubImages()) {
                done = System::SwitchMediaSubImage(static_cast<u32>(index));
            }
        }
        RefreshDiscs();
        if (STATION_HOST_HAS(disc_changed)) {
            g_host->disc_changed(g_host->user, index, done ? 0 : 1);
        }
    });
    return 0;
}

int32_t DiscAdd(const char* path) {
    // A disc by its file, in place of the one in (a PlayStation has no list of its own).
    if (!g_thread.joinable() || path == nullptr) {
        return -1;
    }
    RunLater([file = std::string(path)] {
        const bool done = System::IsValid() && System::InsertMedia(file.c_str());
        RefreshDiscs();
        if (STATION_HOST_HAS(disc_changed)) {
            g_host->disc_changed(g_host->user, done ? g_disc_current.load() : -1, done ? 0 : 1);
        }
    });
    return 0;
}

int32_t SetPortDevice(uint32_t port, uint32_t device) {
    if (port >= 2 || (device != 1 && device != 2) || g_station == nullptr) {
        return -1;
    }
    auto apply = [port, device] {
        g_station->SetControllerType(port, device);
        g_station->Apply({});
    };
    if (g_thread.joinable()) {
        RunLater(std::move(apply));
    } else {
        apply();
    }
    return 0;
}

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    if (size == 0 || Bus::g_ram == nullptr || !InKuseg(address)) {
        return 0;
    }
    const u32 physical = static_cast<u32>(address) & SegmentMask;
    if (physical < Bus::g_ram_size) {
        const std::size_t valid = std::min<std::size_t>(size, Bus::g_ram_size - physical);
        std::memcpy(buffer, Bus::g_ram + physical, valid);
        return valid;
    }
    if (physical >= ScratchpadBase && physical < ScratchpadBase + CPU::DCACHE_SIZE) {
        const u32 offset = physical - ScratchpadBase;
        const std::size_t valid = std::min<std::size_t>(size, CPU::DCACHE_SIZE - offset);
        std::memcpy(buffer, CPU::g_state.dcache.data() + offset, valid);
        return valid;
    }
    return 0;
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    if (Bus::g_ram == nullptr || !InKuseg(address)) {
        return -1;
    }
    // Only the main memory and the scratchpad, all of it or nothing: never the console's
    // registers (0x1F801000 on), never a part of the bytes.
    const u32 physical = static_cast<u32>(address) & SegmentMask;
    const bool in_ram = physical < Bus::g_ram_size && size <= Bus::g_ram_size - physical;
    const bool in_scratchpad = physical >= ScratchpadBase && physical - ScratchpadBase < CPU::DCACHE_SIZE &&
                               size <= CPU::DCACHE_SIZE - (physical - ScratchpadBase);
    if (!in_ram && !in_scratchpad) {
        return -1;
    }
    // Through the CPU's own way of writing, which drops code translated from what changes.
    const auto* bytes = static_cast<const u8*>(buffer);
    for (std::size_t i = 0; i < size; ++i) {
        if (!CPU::SafeWriteMemoryByte(static_cast<u32>(address + i), bytes[i])) {
            return -1;
        }
    }
    return 0;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    if (Bus::g_ram == nullptr) {
        return 0;
    }
    // The kernel's own area, then the rest of the main memory, then the scratchpad.
    const reader_core_region all[] = {
        {0x80000000, 0x00010000, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE, 1},
        {0x80010000, Bus::g_ram_size - 0x00010000,
         READER_CORE_REGION_READ | READER_CORE_REGION_WRITE | READER_CORE_REGION_EXECUTE, 2},
        {0x1F800000, CPU::DCACHE_SIZE, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE, 3},
    };
    for (uint32_t i = 0; i < capacity && i < std::size(all); ++i) {
        regions[i] = all[i];
    }
    return static_cast<uint32_t>(std::size(all));
}

void HooksChanged() {
    // The interpreter runs while there are hooks (cpu_core.cpp asks AnyHooks); the CPU's
    // dispatcher is switched on the game's thread.
    UpdateAnyHooks();
    if (std::this_thread::get_id() == g_game_thread && System::IsValid()) {
        CPU::UpdateDebugDispatcherFlag();
    } else {
        g_dispatcher_stale = true;
    }
}

void PressButton(int32_t bit, int32_t down) {
    if (bit < 0 || bit > 15) {
        return;
    }
    const uint64_t mask = uint64_t{1} << bit;
    if (down != 0) {
        g_reader_buttons |= mask;
    } else {
        g_reader_buttons &= ~mask;
    }
}

int32_t GrabPicture(uint32_t what, reader_core_grab_fn done, void* context) {
    if (done == nullptr || what > READER_CORE_GRAB_WINDOW || !g_thread.joinable()) {
        return -1;
    }
    std::scoped_lock lock{g_grabs_mutex};
    g_grabs.push_back(Grab{what, done, context});
    return 0;
}

int32_t IdentifyDisc(const char* path, char* id, size_t id_size, char* title, size_t title_size);

/// What a file is; a file that cannot be read as a disc (an error thrown on the way) is no game
/// here. Reader Home asks of every file of the core's types in the games' folders, in the
/// background, and an error let out ended the program.
int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    try {
        return IdentifyDisc(path, id, id_size, title, title_size);
    } catch (...) {
        return -1;
    }
}

int32_t IdentifyDisc(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0) {
        return -1;
    }
    // The disc's serial from its boot file, as a game started is named (GetGameInfo); a disc
    // without one is no game a profile can be for.
    std::unique_ptr<CDImage> image = CDImage::Open(path, nullptr);
    if (!image) {
        return -1;
    }
    const std::string code = System::GetGameCodeForImage(image.get(), false);
    if (code.empty()) {
        return -1;
    }
    std::snprintf(id, id_size, "%s", code.c_str());
    if (title != nullptr && title_size > 0) {
        title[0] = '\0'; // the disc has no title of its own; the program goes by the file's name
    }
    return 0;
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
    api.stop = &Stop;
    api.save_state = &SaveStateSlot;
    api.load_state = &LoadStateSlot;
    api.read_memory = &ReadMemory;
    api.write_memory = &WriteMemory;
    api.memory_regions = &MemoryRegions;
    api.hooks_changed = &HooksChanged;
    api.running_game_code = nullptr; // a PlayStation runs only the game's code where hooks go
    api.press_button = &PressButton;
    api.grab = &GrabPicture;
    api.identify = &Identify;
    api.save_locations = &SaveLocations;
    api.disc_count = &DiscCount;
    api.disc_current = &DiscCurrent;
    api.disc_label = &DiscLabel;
    api.disc_insert = &DiscInsert;
    api.disc_add = &DiscAdd;
    api.set_port_device = &SetPortDevice;
    api.state_info = &StateInfo;
    return api;
}

reader_core_api g_api;

} // namespace

extern "C" READER_CORE_EXPORT const reader_core_api* READER_CORE_CALL reader_core_get(uint32_t api_version) {
    if (api_version != READER_CORE_API_VERSION) {
        return nullptr;
    }
    if (g_api.struct_size == 0) {
        g_info = MakeInfo();
        g_api = MakeApi();
    }
    return &g_api;
}
