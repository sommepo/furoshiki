// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// PCSX2 Reader as a core of Reader Home, the one program (Reader Home's docs/one-program.md,
// cores/reader_core.h): PCSX2's emulator (the PCSX2 library, its GS renderers with the CRT
// picture) without a frontend of its own. Reader Home gives it the window to draw into, the
// controller and the settings, and reads the game: this file hands it what the fork's own
// reader/core/emulator.cpp hands the reader's engine in the app (the engine is in the program
// here), through reader_core_host.
//
// It is that file's twin: PCSX2's files call the same Reader::Emu functions
// (reader/core/emulator.h, at their "Reader fork" comments), and a core build links this file
// in emulator.cpp's place (reader/CMakeLists.txt, READER_CORE). It is also PCSX2's Host, as
// pcsx2-gsrunner/Main.cpp defines it for a program without Qt: settings in memory, nothing
// written but the games' cards and savestates, no input sources of its own. What the console
// is (its game IDs, buttons, timings) is read from the fork's own reader/core/console.h, so
// that the app and the core say the same.
//
// Threads: PCSX2's CPU thread is this core's own, one for each game: load_game starts it, it
// sets PCSX2 up (its memory, recompilers and GS thread), boots the game and runs it; stop ends
// the game and the thread with it, PCSX2 taken down as its own frontends do when they end, so
// that nothing of it runs while the program shows its page or when it exits. What the program
// asks from its own thread (pause, a savestate, a setting, a new window size) is queued and
// done on the CPU thread once a frame (Host::PumpMessagesOnCPUThread), or while the game is
// paused. PCSX2's GS thread draws into the program's window.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <shlobj.h>
#else
#include <dlfcn.h>
#endif

#include "fmt/format.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/WindowInfo.h"

#include "pcsx2/Achievements.h"
#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/GS.h"
#include "pcsx2/GS/GS.h"
#include "pcsx2/GameList.h"
#include "pcsx2/Host.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/Memory.h"
#include "pcsx2/MemoryTypes.h"
#include "pcsx2/SIO/Memcard/MemoryCardFile.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/ps2/BiosTools.h"

#include "reader/core/console.h"
#include "reader/core/crt.h"
#include "reader/core/emulator.h"
#include "reader/core/memory_card.h"
#include "reader_core.h"

// Down here: X11's names clash with everything.
#if defined(__linux__)
#include <X11/Xlib.h>
#undef None
#undef Status
#undef Bool
#undef Always
#undef Success
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

bool HostHas(std::size_t end_of_field) {
    return g_host != nullptr && g_host->struct_size >= end_of_field;
}
#define READER_HOST_HAS(field) HostHas(offsetof(reader_core_host, field) + sizeof(g_host->field))

// ---- The console as the reader knows it (the fork's console.h) ----

namespace Traits = Reader::ConsoleTraits;

const char* const FileTypes[] = {"iso", "bin", "img", "mdf", "nrg", "chd", "cso", "zso", "gz", "elf"};
// By PCSX2's own numbers of the first controller's inputs (PadDualshock2::Inputs), which
// console.h's buttons are.
const reader_core_button Buttons[] = {
    {"Up", 0},      {"Right", 1},  {"Down", 2},   {"Left", 3},   {"Triangle", 4}, {"Circle", 5},
    {"Cross", 6},   {"Square", 7}, {"Select", 8}, {"Start", 9},  {"L1", 10},      {"L2", 11},
    {"R1", 12},     {"R2", 13},    {"L3", 14},    {"R3", 15},
};
constexpr int ButtonCount = 16;

// The Emotion Engine's registers as registers.h reads them: r0 to r31 by number and by their
// usual names, hi and lo (their low 32 bits), f0 to f31. A snapshot holds the 32, hi, lo, then
// the 32 floating-point ones. (This emulator carries out no hooks: its profiles read memory
// once a frame. The names are there so that a profile is checked as the app checks it.)
constexpr const char* RegisterNames[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
                                           "t3",   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
                                           "s6",   "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
constexpr uint32_t ValueCount = 66;
std::vector<std::string> g_number_names;
std::vector<reader_core_register_name> g_register_names;

// The settings this core understands (Reader Home's pages show them, its layers say their
// values): the common ones it maps onto PCSX2's, and the console's own under "ps2.".
#ifdef _WIN32
const char* const RendererChoices[] = {"d3d11", "d3d12", "vulkan", "opengl", "software"};
constexpr const char* DefaultRenderer = "d3d11";
#else
const char* const RendererChoices[] = {"vulkan", "opengl", "software"};
constexpr const char* DefaultRenderer = "vulkan";
#endif
const char* const AspectChoices[] = {"auto", "4:3", "16:9", "stretch"};
const char* const BlendingChoices[] = {"minimum", "basic", "medium", "high", "full", "maximum"};
const char* const FilterChoices[] = {"nearest", "bilinear-ps2", "bilinear-forced", "bilinear-forced-no-sprites"};
#define READER_CHOICES(list) list, static_cast<uint32_t>(std::size(list))
const reader_core_setting CoreSettings[] = {
    {READER_CORE_SETTING_RESOLUTION, nullptr, READER_CORE_SETTING_NUMBER, "1", 1, 8, 1, nullptr, 0},
    {READER_CORE_SETTING_RENDERER, nullptr, READER_CORE_SETTING_CHOICE, DefaultRenderer, 0, 0, 0, READER_CHOICES(RendererChoices)},
    {READER_CORE_SETTING_ASPECT, nullptr, READER_CORE_SETTING_CHOICE, "auto", 0, 0, 0, READER_CHOICES(AspectChoices)},
    {READER_CORE_SETTING_VSYNC, nullptr, READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_FRAME_LIMIT, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 5, nullptr, 0},
    {"ps2.fast_boot", "Skip the console's start-up screen", READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {"ps2.blending", "Blending accuracy", READER_CORE_SETTING_CHOICE, "basic", 0, 0, 0, READER_CHOICES(BlendingChoices)},
    {"ps2.texture_filter", "Texture smoothing", READER_CORE_SETTING_CHOICE, "bilinear-ps2", 0, 0, 0, READER_CHOICES(FilterChoices)},
    {"ps2.widescreen", "Widescreen patches (some games)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_CRT, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_SPEED, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 1000, 10, nullptr, 0},
};
#undef READER_CHOICES

reader_core_info MakeInfo() {
    g_number_names.clear();
    for (int i = 0; i < 32; ++i) {
        g_number_names.push_back("r" + std::to_string(i));
    }
    for (int i = 0; i < 32; ++i) {
        g_number_names.push_back("f" + std::to_string(i));
    }
    g_register_names.clear();
    for (uint32_t i = 0; i < 32; ++i) {
        g_register_names.push_back({g_number_names[i].c_str(), i, READER_CORE_REGISTER_INTEGER, 32});
        g_register_names.push_back({RegisterNames[i], i, READER_CORE_REGISTER_INTEGER, 32});
    }
    g_register_names.push_back({"hi", 32, READER_CORE_REGISTER_INTEGER, 32});
    g_register_names.push_back({"lo", 33, READER_CORE_REGISTER_INTEGER, 32});
    for (uint32_t i = 0; i < 32; ++i) {
        g_register_names.push_back({g_number_names[32 + i].c_str(), 34 + i, READER_CORE_REGISTER_FLOAT32, 32});
    }

    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "ps2";
    info.name = "PCSX2";
    info.version = "2.8.2 (PCSX2 Reader)";
    info.system = "PlayStation 2";
    info.short_name = "PS2";
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
    // Going on to the next choice is the app's (emulator.cpp); the program has no such run yet.
    info.has_next_choice = 0;
    info.file_types = FileTypes;
    info.file_type_count = static_cast<uint32_t>(std::size(FileTypes));
    info.buttons = Buttons;
    info.button_count = static_cast<uint32_t>(std::size(Buttons));
    info.cpu.family = "mips-r5900";
    info.cpu.value_count = ValueCount;
    info.cpu.names = g_register_names.data();
    info.cpu.name_count = static_cast<uint32_t>(g_register_names.size());
    info.cpu.hook_alignment = 4;
    info.cpu.thumb_odd = 0;
    info.cpu.highest_address = ~0ULL; // as registers.h: Lua's negative numbers are the top of it
    info.settings = CoreSettings;
    info.setting_count = static_cast<uint32_t>(std::size(CoreSettings));
    // Since 2026-10-24: the game's own card for save sync (asked for before it is opened, made
    // and formatted after if new, every write told), two DualShock 2s with rumble, a disc
    // changed by its file, savestates answered.
    info.features = READER_CORE_HAS_SAVE_SYNC | READER_CORE_HAS_DISCS | READER_CORE_HAS_RUMBLE | READER_CORE_HAS_STATES |
                    READER_CORE_HAS_ACHIEVEMENTS;
    info.port_count = 2;
    info.state_slots = 10;
    return info;
}

reader_core_info g_info;

// ---- PCSX2's settings, in memory: nothing is written ----

MemorySettingsInterface g_settings_layer;
/// PCSX2's secrets layer, in memory too: RetroAchievements' token, which PCSX2 keeps there.
MemorySettingsInterface g_secrets_layer;
/// The program's values by the interface's keys, as last handed over.
std::mutex g_values_mutex;
std::map<std::string, std::string> g_values;
std::atomic<int> g_volume{100};
bool g_frame_limit = true; ///< On the CPU thread
float g_speed = 100.0f;

std::string Value(const std::string& key, const char* fallback) {
    std::scoped_lock lock{g_values_mutex};
    const auto found = g_values.find(key);
    return found != g_values.end() ? found->second : std::string(fallback);
}

/// The program's values into PCSX2's settings (on the CPU thread, or before it runs).
void PutSettings() {
    const std::unique_lock lock = Host::GetSettingsLock();
    MemorySettingsInterface& si = g_settings_layer;
    const auto on = [](const std::string& value) { return value == "1"; };
    const std::string renderer = Value(READER_CORE_SETTING_RENDERER, DefaultRenderer);
    GSRendererType type = GSRendererType::VK;
    if (renderer == "opengl") {
        type = GSRendererType::OGL;
    } else if (renderer == "software") {
        type = GSRendererType::SW;
    } else if (renderer == "d3d11") {
        type = GSRendererType::DX11;
    } else if (renderer == "d3d12") {
        type = GSRendererType::DX12;
    }
    si.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(type));
    si.SetFloatValue("EmuCore/GS", "upscale_multiplier",
                     static_cast<float>(std::clamp(std::atoi(Value(READER_CORE_SETTING_RESOLUTION, "1").c_str()), 1, 8)));
    const std::string aspect = Value(READER_CORE_SETTING_ASPECT, "auto");
    si.SetStringValue("EmuCore/GS", "AspectRatio", aspect == "4:3"       ? "4:3"
                                                   : aspect == "16:9"    ? "16:9"
                                                   : aspect == "stretch" ? "Stretch"
                                                                         : "Auto 4:3/3:2");
    si.SetBoolValue("EmuCore/GS", "VsyncEnable", on(Value(READER_CORE_SETTING_VSYNC, "0")));
    g_volume = std::clamp(std::atoi(Value(READER_CORE_SETTING_VOLUME, "100").c_str()), 0, 100);
    const float speed = std::max(0.0f, static_cast<float>(std::atof(Value(READER_CORE_SETTING_SPEED, "100").c_str())));
    g_speed = speed > 0.0f ? speed : 100.0f;
    g_frame_limit = on(Value(READER_CORE_SETTING_FRAME_LIMIT, "1")) && speed > 0.0f;
    si.SetFloatValue("Framerate", "NominalScalar", std::clamp(g_speed / 100.0f, 0.05f, 10.0f));
    // RetroAchievements, with the user's token from the program (PCSX2 logs in with it itself);
    // its notifications are the only messages PCSX2 draws over the game here.
    const std::string ra_user = Value(READER_CORE_SETTING_ACHIEVEMENTS_USER, "");
    const std::string ra_token = Value(READER_CORE_SETTING_ACHIEVEMENTS_TOKEN, "");
    const bool achievements = !ra_user.empty() && !ra_token.empty();
    si.SetBoolValue("Achievements", "Enabled", achievements);
    si.SetStringValue("Achievements", "Username", ra_user.c_str());
    {
        // The token in the secrets layer: found in the base layer, PCSX2 moves it there itself.
        const std::unique_lock secrets_lock = Host::GetSecretsSettingsLock();
        g_secrets_layer.SetStringValue("Achievements", "Token", ra_token.c_str());
    }
    si.SetBoolValue("Achievements", "ChallengeMode", achievements && on(Value(READER_CORE_SETTING_ACHIEVEMENTS_HARDCORE, "0")));
    si.SetBoolValue("Achievements", "Notifications", true);
    si.SetBoolValue("Achievements", "LeaderboardNotifications", true);
    si.SetBoolValue("Achievements", "SoundEffects", false); // PCSX2's sounds go past the program's sound
    si.SetBoolValue("EmuCore", "EnableFastBoot", on(Value("ps2.fast_boot", "1")));
    si.SetBoolValue("EmuCore", "EnableWideScreenPatches", on(Value("ps2.widescreen", "0")));
    const std::string blending = Value("ps2.blending", "basic");
    int blending_unit = 1;
    for (int i = 0; i < static_cast<int>(std::size(BlendingChoices)); ++i) {
        if (blending == BlendingChoices[i]) {
            blending_unit = i;
        }
    }
    si.SetIntValue("EmuCore/GS", "accurate_blending_unit", blending_unit);
    const std::string filter = Value("ps2.texture_filter", "bilinear-ps2");
    si.SetIntValue("EmuCore/GS", "filter", filter == "nearest"                      ? 0
                                           : filter == "bilinear-forced"            ? 1
                                           : filter == "bilinear-forced-no-sprites" ? 3
                                                                                    : 2);
}

std::vector<std::string> CoreFolders();

/// Whether `folder` has a console's BIOS image in it.
bool HasBios(const std::string& folder) {
    FileSystem::FindResultsArray files;
    if (folder.empty() || !FileSystem::FindFiles(folder.c_str(), "*", FILESYSTEM_FIND_FILES, &files)) {
        return false;
    }
    u32 version, region;
    std::string description, zone;
    return std::any_of(files.begin(), files.end(), [&](const FILESYSTEM_FIND_DATA& file) {
        return file.Size >= 256 * 1024 && file.Size <= 8 * 1024 * 1024 &&
               IsBIOS(file.FileName.c_str(), version, description, region, zone);
    });
}

/// The settings of PCSX2 Reader, the PS2 app, where it keeps them: its data folder in the
/// user's documents (or ~/.config), or the app's own folder beside Reader Home's in an
/// install of the reader apps (C:\Readers\PCSX2Reader, with its portable.txt).
std::vector<std::string> AppSettingsFiles() {
    std::vector<std::string> roots;
#ifdef _WIN32
    PWSTR documents = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents != nullptr) {
        roots.push_back(Path::Combine(StringUtil::WideStringToUTF8String(documents), "PCSX2Reader"));
    }
    CoTaskMemFree(documents);
    for (const std::string& folder : CoreFolders()) { // <install>\ReaderHome\cores
        roots.push_back(Path::Combine(Path::GetDirectory(Path::GetDirectory(folder)), "PCSX2Reader"));
    }
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config != nullptr && *config != '\0') {
        roots.push_back(Path::Combine(config, "PCSX2Reader"));
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        roots.push_back(Path::Combine(Path::Combine(home, ".config"), "PCSX2Reader"));
    }
#endif
    std::vector<std::string> files;
    for (const std::string& root : roots) {
        files.push_back(Path::Combine(Path::Combine(root, "inis"), "PCSX2.ini"));
    }
    return files;
}

/// Where the BIOS is: the console's system folder the program gives, when there is one there
/// (Reader Home gives PCSX2 Reader's BIOS folder while its own is empty: then the file the
/// app's settings name, as that folder may hold several); otherwise the one PCSX2 Reader's
/// settings name, wherever it is, so that a BIOS set up for the app is the core's too (on the
/// maintainer's PC a drive of its own, B:, which is there only in the user's session: asked
/// again at each game's start). CPU thread, or before it runs.
void PutBios() {
    const bool own = HasBios(g_paths.system);
    std::string folder = g_paths.system, file;
    for (const std::string& path : AppSettingsFiles()) {
        if (!FileSystem::FileExists(path.c_str())) {
            continue;
        }
        INISettingsInterface ini(path);
        if (!ini.Load()) {
            continue;
        }
        std::string bios = ini.GetStringValue("Folders", "Bios", "bios");
        if (!Path::IsAbsolute(bios)) {
            bios = Path::Combine(Path::GetDirectory(Path::GetDirectory(path)), bios);
        }
        const std::string name = ini.GetStringValue("Filenames", "BIOS", "");
        if (name.empty() || !FileSystem::FileExists(Path::Combine(bios, name).c_str())) {
            continue;
        }
        if (!own) {
            folder = bios;
            file = name;
            Say(READER_CORE_LOG_INFO, "PCSX2: no BIOS in " + g_paths.system + "; PCSX2 Reader's: " + Path::Combine(bios, name));
            break;
        }
        if (Path::RealPath(bios) == Path::RealPath(g_paths.system)) {
            file = name;
            break;
        }
    }
    const std::unique_lock lock = Host::GetSettingsLock();
    g_settings_layer.SetStringValue("Folders", "Bios", folder.c_str());
    g_settings_layer.SetStringValue("Filenames", "BIOS", file.c_str());
}

/// What is this core's own, whatever the program says: the folders it was given, no input
/// sources (the program's controller comes through the pad callback), no sound output (the
/// program plays it), no messages drawn over the game but RetroAchievements', a card in the first slot only.
void PutFixedSettings() {
    MemorySettingsInterface& si = g_settings_layer;
    si.SetStringValue("Folders", "MemoryCards", g_paths.saves.c_str());
    si.SetStringValue("Folders", "Savestates", g_paths.states.c_str());
    si.SetBoolValue("InputSources", "SDL", false);
    si.SetBoolValue("InputSources", "XInput", false);
    si.SetBoolValue("InputSources", "DInput", false);
    si.SetBoolValue("InputSources", "Keyboard", false);
    si.SetBoolValue("InputSources", "Mouse", false);
    Pad::ClearPortBindings(si, 0);
    Pad::ClearPortBindings(si, 1);
    si.SetStringValue("Pad1", "Type", "DualShock2");
    si.SetStringValue("Pad2", "Type", "DualShock2"); // the program's second controller
    si.ClearSection("Hotkeys");
    si.SetStringValue("SPU2/Output", "Backend", "Null");
    si.SetIntValue("EmuCore/GS", "OsdMessagesPos", static_cast<int>(OsdOverlayPos::None));
    si.SetIntValue("EmuCore/GS", "OsdPerformancePos", static_cast<int>(OsdOverlayPos::None));
    si.SetBoolValue("MemoryCards", "Slot2_Enable", false);
    si.SetBoolValue("EmuCore", "SaveStateOnShutdown", false);
    si.SetBoolValue("UI", "StartFullscreen", false);
    si.SetBoolValue("EmuCore", "EnableDiscordPresence", false);
    si.SetBoolValue("EmuCore", "EnablePINE", false);
    si.SetBoolValue("Logging", "EnableSystemConsole", false);
    si.SetBoolValue("Logging", "EnableFileLogging", false);
}

// ---- The CPU thread ----

std::thread g_thread;
std::atomic<std::thread::id> g_cpu_thread{};
std::mutex g_queue_mutex;
std::condition_variable g_queue_wake;
std::deque<std::function<void()>> g_queue;

std::mutex g_window_mutex;
reader_core_window g_window{};
/// The window changed since the GS last took it: its size, or the window itself.
std::atomic<bool> g_window_resized{false};
std::atomic<bool> g_window_replaced{false};
/// The GS thread's own connection to the X server (AcquireRenderWindow), or none.
void* g_x11_display = nullptr;
std::atomic<uint64_t> g_reader_buttons{0};

struct Grab {
    uint32_t what;
    reader_core_grab_fn done;
    void* context;
};
std::vector<Grab> g_grabs;
std::mutex g_grabs_mutex;

void RunLater(std::function<void()> task) {
    {
        std::scoped_lock lock{g_queue_mutex};
        g_queue.push_back(std::move(task));
    }
    g_queue_wake.notify_all();
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

reader_core_window CurrentWindow() {
    std::scoped_lock lock{g_window_mutex};
    return g_window;
}

/// The game's picture (as cards take it: its own size) or the window's (letterboxed as it is
/// drawn there, without the CRT picture), handed over on the CPU thread.
void DoGrabs() {
    std::vector<Grab> grabs;
    {
        std::scoped_lock lock{g_grabs_mutex};
        grabs.swap(g_grabs);
    }
    for (const Grab& grab : grabs) {
        u32 width = 0, height = 0;
        std::vector<u32> pixels;
        uint32_t window_width = 0, window_height = 0;
        if (grab.what == READER_CORE_GRAB_WINDOW) {
            const reader_core_window window = CurrentWindow();
            window_width = window.width;
            window_height = window.height;
        }
        const bool ok = MTGS::IsOpen() &&
                        MTGS::SaveMemorySnapshot(window_width, window_height, true, false, &width, &height, &pixels) &&
                        width > 0 && height > 0 && pixels.size() >= static_cast<std::size_t>(width) * height;
        if (!ok) {
            grab.done(grab.context, nullptr, 0, 0, 0);
            continue;
        }
        // RGBX: the last byte of each is not the picture's.
        for (u32& pixel : pixels) {
            pixel |= 0xFF000000u;
        }
        grab.done(grab.context, reinterpret_cast<const uint8_t*>(pixels.data()), width, height, width * 4);
    }
}

void DropGrabs() {
    std::vector<Grab> grabs;
    {
        std::scoped_lock lock{g_grabs_mutex};
        grabs.swap(g_grabs);
    }
    for (const Grab& grab : grabs) {
        grab.done(grab.context, nullptr, 0, 0, 0);
    }
}

/// The program's controllers (and the reader's presses, on the first) onto the console's two.
void ApplyPad() {
    static uint64_t applied[2] = {~0ULL, ~0ULL};
    static int16_t applied_sticks[2][4] = {};
    // The sticks: each way of each is an input of its own, from 0 to 1.
    static constexpr u32 Ways[4][2] = {
        {PadDualshock2::Inputs::PAD_L_LEFT, PadDualshock2::Inputs::PAD_L_RIGHT},
        {PadDualshock2::Inputs::PAD_L_UP, PadDualshock2::Inputs::PAD_L_DOWN},
        {PadDualshock2::Inputs::PAD_R_LEFT, PadDualshock2::Inputs::PAD_R_RIGHT},
        {PadDualshock2::Inputs::PAD_R_UP, PadDualshock2::Inputs::PAD_R_DOWN},
    };
    for (u32 port = 0; port < 2; ++port) {
        reader_core_pad pad{};
        if (g_host->pad != nullptr) {
            g_host->pad(g_host->user, port, &pad);
        }
        const uint64_t buttons = pad.buttons | (port == 0 ? g_reader_buttons.load() : 0);
        for (int bit = 0; bit < ButtonCount; ++bit) {
            const bool down = ((buttons >> bit) & 1) != 0;
            if (applied[port] == ~0ULL || down != (((applied[port] >> bit) & 1) != 0)) {
                Pad::SetControllerState(port, static_cast<u32>(bit), down ? 1.0f : 0.0f);
            }
        }
        applied[port] = buttons;
        for (int axis = 0; axis < 4; ++axis) {
            if (pad.sticks[axis] == applied_sticks[port][axis]) {
                continue;
            }
            applied_sticks[port][axis] = pad.sticks[axis];
            const float value = static_cast<float>(pad.sticks[axis]) / 32767.0f;
            Pad::SetControllerState(port, Ways[axis][0], std::clamp(-value, 0.0f, 1.0f));
            Pad::SetControllerState(port, Ways[axis][1], std::clamp(value, 0.0f, 1.0f));
        }
    }
}

/// Tells the program where the picture is in its window, when that changed.
void TellPictureRect() {
    static std::tuple<int32_t, int32_t, int32_t, int32_t> told{-1, -1, -1, -1};
    if (!READER_HOST_HAS(picture_rect) || g_host->picture_rect == nullptr) {
        return;
    }
    float rect[4];
    GSGetLastDrawRect(rect);
    const std::tuple<int32_t, int32_t, int32_t, int32_t> now{
        static_cast<int32_t>(rect[0]), static_cast<int32_t>(rect[1]), static_cast<int32_t>(rect[2] - rect[0]),
        static_cast<int32_t>(rect[3] - rect[1])};
    if (std::get<2>(now) <= 0 || std::get<3>(now) <= 0 || now == told) {
        return;
    }
    told = now;
    const auto [x, y, width, height] = now;
    g_host->picture_rect(g_host->user, x, y, width, height);
}

/// The window's new size (or a new window) to the GS, which draws into it. CPU thread.
void UpdateWindow() {
    if (!MTGS::IsOpen()) {
        return;
    }
    if (g_window_replaced.exchange(false)) {
        g_window_resized = false;
        MTGS::UpdateDisplayWindow();
    } else if (g_window_resized.exchange(false)) {
        const reader_core_window window = CurrentWindow();
        MTGS::ResizeDisplayWindow(window.width, window.height, window.scale > 0 ? window.scale : 1.0f);
    } else {
        return;
    }
    if (VMManager::GetState() == VMState::Paused) {
        MTGS::PresentCurrentFrame();
    }
}

void ApplyLimiter() {
    if (VMManager::HasValidVM()) {
        VMManager::SetLimiterMode(g_frame_limit ? LimiterModeType::Nominal : LimiterModeType::Unlimited);
    }
}

void Boot(const std::string& path) {
    VMBootParameters boot;
    boot.filename = path;
    Error error;
    const VMBootResult result = VMManager::Initialize(boot, &error);
    if (result != VMBootResult::StartupSuccess) {
        Say(READER_CORE_LOG_ERROR, "PCSX2: the console did not start " + path +
                                       (error.IsValid() ? ": " + std::string(error.GetDescription()) : std::string()));
        if (g_host->game_stopped != nullptr) {
            g_host->game_stopped(g_host->user);
        }
        return;
    }
    VMManager::SetState(VMState::Running);
    ApplyLimiter();
    g_window_resized = true; // a window given before it was shown may have been small
}

void CpuThread(std::string path) {
    g_cpu_thread = std::this_thread::get_id();
    PutBios();
    {
        const std::unique_lock lock = Host::GetSettingsLock();
        EmuFolders::LoadConfig(g_settings_layer);
    }
    if (!VMManager::Internal::CPUThreadInitialize()) {
        Say(READER_CORE_LOG_ERROR, "PCSX2: its CPU thread could not be set up");
        VMManager::Internal::CPUThreadShutdown();
        if (g_host->game_stopped != nullptr) {
            g_host->game_stopped(g_host->user);
        }
        return;
    }
    VMManager::ApplySettings();
    Boot(path);
    // Until the game is gone (Stopping, which a stop asks for, is no valid VM to PCSX2 any
    // more, but it is still to be shut down).
    for (VMState state = VMManager::GetState(); state != VMState::Shutdown; state = VMManager::GetState()) {
        RunQueued();
        state = VMManager::GetState();
        if (state == VMState::Running) {
            VMManager::Execute(); // until the state changes (the queue is run once a frame)
        } else if (state == VMState::Stopping) {
            VMManager::Shutdown(false); // the reader hears of it (Reader::Emu::OnShutdown)
        } else if (state == VMState::Paused) {
            UpdateWindow();
            DoGrabs();
            std::unique_lock lock{g_queue_mutex};
            g_queue_wake.wait_for(lock, std::chrono::milliseconds(16), [] { return !g_queue.empty(); });
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    DropGrabs();
    VMManager::Internal::CPUThreadShutdown();
}

/// Asks the running game to stop; the CPU thread shuts it down after the frame.
void RequestStop() {
    if (VMManager::HasValidVM() && VMManager::GetState() != VMState::Stopping) {
        VMManager::SetState(VMState::Stopping);
    }
}

// ---- The game as the reader is told of it (reader/core/emulator.cpp's part) ----

std::string g_serial;
uint32_t g_crc = 0;
bool g_loaded = false;
std::mutex g_card_mutex;
std::string g_card_file; ///< The running game's own card, or none

void Unload() {
    if (!g_loaded) {
        return;
    }
    g_loaded = false;
    if (g_host->game_stopped != nullptr) {
        g_host->game_stopped(g_host->user);
    }
}

/// The folders the core's own files may be in: the one it was loaded from (a link to it in
/// Reader Home's cores folder, say), then the one the file itself is in. PCSX2's resources
/// (shaders, fonts, the game index) are in the "pcsx2" folder beside it, where Reader Home's
/// package puts them.
std::vector<std::string> CoreFolders() {
#ifdef _WIN32
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&CoreFolders), &module) &&
        GetModuleFileNameW(module, path, MAX_PATH) > 0) {
        return {std::string(Path::GetDirectory(StringUtil::WideStringToUTF8String(path)))};
    }
    return {};
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&CoreFolders), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return {std::string(Path::GetDirectory(info.dli_fname)),
            std::string(Path::GetDirectory(Path::RealPath(info.dli_fname)))};
#endif
}

} // namespace

// ---- What PCSX2's files call (reader/core/emulator.h) ----

namespace Reader::Emu {

void OnGameChanged(const std::string& serial, uint32_t crc, const std::string& title) {
    if (g_loaded && serial == g_serial && crc == g_crc) {
        return;
    }
    Unload();
    g_serial = serial;
    g_crc = crc;
    // The console's own menu has no serial, and a disc that is only being looked at has no
    // running program yet.
    if (serial.empty() || crc == 0) {
        return;
    }
    if (g_host->game_loading != nullptr) {
        g_host->game_loading(g_host->user);
    }
    char build[16];
    std::snprintf(build, sizeof(build), "%08X", crc);
    // A game's program is loaded where it was linked for: profiles count from 0.
    if (g_host->module_loaded != nullptr) {
        g_host->module_loaded(g_host->user, "main", 0, Ps2MemSize::ExposedRam, build);
    }
    g_loaded = true;
    // The profile goes in now.
    if (g_host->game_loaded != nullptr) {
        g_host->game_loaded(g_host->user, serial.c_str(), title.c_str());
    }
}

void OnShutdown() {
    Unload();
    g_serial.clear();
    g_crc = 0;
    std::scoped_lock lock{g_card_mutex};
    g_card_file.clear();
}

bool IsGameSerial(const std::string& serial) {
    return !serial.empty() && serial.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") == std::string::npos;
}

std::string CardOf(const std::string& serial) {
    return Path::Combine(Path::Combine(g_paths.saves, serial), "card.ps2");
}

void BeforeMemoryCards(const std::string& serial, const std::string& title) {
    // A game's own card in the first slot, as the app keeps it ("reader-saves/<serial>/card.ps2"
    // of its memory cards folder): here "<serial>/card.ps2" in the saves folder the program
    // gave, so that the app's folder of a game can be brought over as it is. The console's own
    // menu (no serial) has the settings' card.
    const bool game = IsGameSerial(serial);
    const std::string card = game ? CardOf(serial) : std::string{};
    {
        std::scoped_lock lock{g_card_mutex};
        g_card_file = card;
    }
    if (!game) {
        return;
    }
    // Save sync puts the newest card there first (the program fetches it meanwhile).
    if (READER_HOST_HAS(saves_opening)) {
        const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FILE, serial.c_str(),
                                              card.c_str()};
        const reader_core_saves saves{sizeof(reader_core_saves), serial.c_str(), title.c_str(), 0, &where, 1};
        g_host->saves_opening(g_host->user, &saves);
    }
    if (FileSystem::FileExists(card.c_str())) {
        return;
    }
    FileSystem::CreateDirectoryPath(Path::Combine(g_paths.saves, serial).c_str(), true);
    if (FileMcd_CreateNewCard(Path::Combine(serial, "card.ps2"), MemoryCardType::File, MemoryCardFileType::PS2_8MB) &&
        !Reader::FormatCard(card)) {
        Say(READER_CORE_LOG_WARNING, "PCSX2: could not format the new memory card " + card);
    }
    if (FileSystem::FileExists(card.c_str()) && READER_HOST_HAS(saves_written)) {
        g_host->saves_written(g_host->user, serial.c_str(), card.c_str(), READER_CORE_WRITE_EMULATOR);
    }
}

std::string MemoryCardOverride() {
    std::scoped_lock lock{g_card_mutex};
    return g_card_file;
}

void OnMemoryCardWritten(unsigned slot) {
    const std::string card = MemoryCardOverride();
    if (slot != 0 || card.empty()) {
        return;
    }
    if (g_host->saved != nullptr) {
        g_host->saved(g_host->user, card.c_str());
    }
    if (READER_HOST_HAS(saves_written)) {
        // The serial is the card's folder's name (CardOf).
        const std::string serial = std::string(Path::GetFileName(Path::GetDirectory(card)));
        g_host->saves_written(g_host->user, serial.c_str(), card.c_str(), READER_CORE_WRITE_GAME);
    }
}

void OnPadVibration(unsigned pad_index, float strong_motor, float weak_motor) {
    // PCSX2 counts its first two pads 0 and 1 (multitap ports after them: not offered here).
    static uint16_t told[2][2] = {};
    if (pad_index >= 2 || !READER_HOST_HAS(rumble)) {
        return;
    }
    const uint16_t strong = static_cast<uint16_t>(std::clamp(strong_motor, 0.0f, 1.0f) * 65535.0f);
    const uint16_t weak = static_cast<uint16_t>(std::clamp(weak_motor, 0.0f, 1.0f) * 65535.0f);
    if (strong != told[pad_index][0] || weak != told[pad_index][1]) {
        told[pad_index][0] = strong;
        told[pad_index][1] = weak;
        g_host->rumble(g_host->user, pad_index, strong, weak);
    }
}

void OnStateLoaded() {
    if (g_loaded && g_host->state_loaded != nullptr) {
        g_host->state_loaded(g_host->user);
    }
}

void OnFrame() {
    DoGrabs();
    TellPictureRect();
    if (!g_loaded || g_host->frame == nullptr) {
        return;
    }
    // A profile's places are in the 640 by 448 picture the app gives it (emulator.cpp).
    g_host->frame(g_host->user, 640, 448);
}

void OnPadInput(unsigned, unsigned, float) {
    // The app stops its run to the next choice at a press of the player's; the program has none.
}

void OnAudioFloat(const float* samples, std::size_t frames) {
    // The mixer's output, on the CPU thread, a chunk at a time: the reader's (a card's audio)
    // and the program's to play (this core has no sound output of its own).
    static thread_local std::vector<int16_t> scratch;
    scratch.resize(frames * 2);
    for (std::size_t i = 0; i < frames * 2; ++i) {
        scratch[i] = static_cast<int16_t>(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f);
    }
    if (g_host->audio != nullptr) {
        g_host->audio(g_host->user, scratch.data(), frames, Traits::AudioSampleRate);
    }
    if (READER_HOST_HAS(play) && g_host->play != nullptr) {
        const int volume = g_volume.load();
        if (volume < 100) {
            for (int16_t& sample : scratch) {
                sample = static_cast<int16_t>(sample * volume / 100);
            }
        }
        g_host->play(g_host->user, scratch.data(), frames, Traits::AudioSampleRate);
    }
}

} // namespace Reader::Emu

// ---- PCSX2's Host, without a frontend (as pcsx2-gsrunner's) ----

void Host::CommitBaseSettingChanges() {}

void Host::LoadSettings(SettingsInterface&, std::unique_lock<std::mutex>&) {}

void Host::CheckForSettingsChanges(const Pcsx2Config&) {}

bool Host::RequestResetSettings(bool, bool, bool, bool, bool) {
    return false;
}

void Host::SetDefaultUISettings(SettingsInterface&) {}

bool Host::LocaleCircleConfirm() {
    return true; // a Japanese console's: ○ says yes
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback() {
    return ProgressCallback::CreateNullProgressCallback();
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message) {
    Say(READER_CORE_LOG_INFO, fmt::format("PCSX2: {}: {}", title, message));
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message) {
    Say(READER_CORE_LOG_ERROR, fmt::format("PCSX2: {}: {}", title, message));
}

void Host::OpenURL(const std::string_view) {}

bool Host::CopyTextToClipboard(const std::string_view) {
    return false;
}

std::string Host::GetTextFromClipboard() {
    return {};
}

void Host::BeginTextInput() {}

void Host::EndTextInput() {}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo() {
    return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view, const std::string_view) {}

void Host::OnInputDeviceDisconnected(const InputBindingKey, const std::string_view) {}

void Host::SetMouseMode(bool, bool) {}

void Host::SetMouseLock(bool) {}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool) {
    // On the GS thread: the program's window, through a connection of this thread's own to the
    // display (Xlib's are not to be shared between threads, and the program's is its toolkit's).
    const reader_core_window window = CurrentWindow();
    WindowInfo wi;
    wi.surface_width = window.width;
    wi.surface_height = window.height;
    wi.surface_scale = window.scale > 0 ? window.scale : 1.0f;
    switch (window.kind) {
    case READER_CORE_WINDOW_NONE:
        wi.type = WindowInfo::Type::Surfaceless; // no picture shown (tests)
        return wi;
#if defined(__linux__)
    case READER_CORE_WINDOW_X11: {
        if (g_x11_display == nullptr) {
            g_x11_display = XOpenDisplay(nullptr);
        }
        if (g_x11_display == nullptr) {
            Say(READER_CORE_LOG_ERROR, "PCSX2: no connection to the X display");
            return std::nullopt;
        }
        wi.type = WindowInfo::Type::X11;
        wi.display_connection = g_x11_display;
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
        Say(READER_CORE_LOG_ERROR, "PCSX2: no window it can draw into");
        return std::nullopt;
    }
}

void Host::ReleaseRenderWindow() {
#if defined(__linux__)
    if (g_x11_display != nullptr) {
        XCloseDisplay(static_cast<Display*>(g_x11_display));
        g_x11_display = nullptr;
    }
#endif
}

void Host::BeginPresentFrame() {}

void Host::RequestResizeHostDisplay(s32, s32) {}

void Host::OnVMStarting() {}

void Host::OnVMStarted() {}

void Host::OnVMDestroyed() {}

void Host::OnVMPaused() {}

void Host::OnVMResumed() {}

void Host::OnGameChanged(const std::string&, const std::string&, const std::string&, const std::string&, u32, u32) {}

void Host::OnPerformanceMetricsUpdated() {}

void Host::OnSaveStateLoading(const std::string_view) {}

void Host::OnSaveStateLoaded(const std::string_view, bool) {}

void Host::OnSaveStateSaved(const std::string_view) {}

void Host::RunOnCPUThread(std::function<void()> function, bool block) {
    if (std::this_thread::get_id() == g_cpu_thread) {
        function();
        return;
    }
    if (!block) {
        RunLater(std::move(function));
        return;
    }
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> waited = done->get_future();
    RunLater([function = std::move(function), done] {
        function();
        done->set_value();
    });
    waited.wait();
}

void Host::RefreshGameListAsync(bool) {}

void Host::CancelGameListRefresh() {}

bool Host::IsFullscreen() {
    return false;
}

void Host::SetFullscreen(bool) {}

void Host::OnCaptureStarted(const std::string&) {}

void Host::OnCaptureStopped() {}

void Host::RequestExitApplication(bool) {}

void Host::RequestExitBigPicture() {}

void Host::RequestVMShutdown(bool, bool, bool) {
    RequestStop();
}

void Host::OnAchievementsLoginSuccess(const char*, u32, u32, u32) {}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason) {}

void Host::OnAchievementsHardcoreModeChanged(bool) {}

void Host::OnAchievementsRefreshed() {}

bool Host::InBatchMode() {
    return false;
}

bool Host::InNoGUIMode() {
    return true;
}

bool Host::ShouldPreferHostFileSelector() {
    return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view, bool, FileSelectorCallback callback, FileSelectorFilters,
                                     std::string_view) {
    callback(std::string());
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs) {
    const int res = std::strncmp(lhs.data(), rhs.data(), std::min(lhs.size(), rhs.size()));
    if (res != 0) {
        return res;
    }
    return lhs.size() > rhs.size() ? 1 : (lhs.size() < rhs.size() ? -1 : 0);
}

void Host::PumpMessagesOnCPUThread() {
    // Once a frame on the CPU thread, between two stretches of the game: what the program
    // asked, the window, the controller.
    RunQueued();
    UpdateWindow();
    ApplyPad();
}

s32 Host::Internal::GetTranslatedStringImpl(const std::string_view, const std::string_view msg, char* tbuf,
                                            size_t tbuf_space) {
    if (msg.size() > tbuf_space) {
        return -1;
    }
    if (msg.empty()) {
        return 0;
    }
    std::memcpy(tbuf, msg.data(), msg.size());
    return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char*, const char* msg, const char*, int count) {
    const std::string count_str = std::to_string(count);
    std::string ret(msg);
    for (;;) {
        const std::string::size_type pos = ret.find("%n");
        if (pos == std::string::npos) {
            break;
        }
        ret.replace(pos, 2, count_str);
    }
    return ret;
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view) {
    return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32) {
    return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32) {
    return nullptr;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()

// ---- The core's table (cores/reader_core.h) ----

namespace {

constexpr u32 MainMemoryMask = 0x1FFFFFFF;
constexpr u32 ScratchpadBase = 0x70000000;

/// An address's offset into the main memory, which 0x00000000, 0x80000000 and 0xA0000000 all
/// show whole; nothing for any other.
std::optional<u32> MainMemoryOffset(uint64_t address) {
    if (address > 0xFFFFFFFFULL) {
        return std::nullopt;
    }
    const u32 start = static_cast<u32>(address);
    if (start >= 0xC0000000 || (start >= 0x20000000 && start < 0x80000000)) {
        return std::nullopt;
    }
    const u32 offset = start & MainMemoryMask;
    if (offset >= Ps2MemSize::ExposedRam) {
        return std::nullopt;
    }
    return offset;
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
    for (const std::string& folder : {g_paths.data, g_paths.saves, g_paths.states, g_paths.system}) {
        if (!folder.empty()) {
            FileSystem::CreateDirectoryPath(folder.c_str(), true);
        }
    }
    // PCSX2's log goes to the program's, from Info up.
    Log::SetHostOutputLevel(LOGLEVEL_INFO, [](LOGLEVEL level, ConsoleColors, std::string_view message) {
        const uint32_t kind = level <= LOGLEVEL_ERROR     ? READER_CORE_LOG_ERROR
                              : level <= LOGLEVEL_WARNING ? READER_CORE_LOG_WARNING
                                                          : READER_CORE_LOG_INFO;
        std::string text(message);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        if (!text.empty()) {
            Say(kind, "PCSX2: " + text);
        }
    });

    // The folders: the program's data folder is PCSX2's own (caches, its inis folder, which
    // nothing writes to), the resources beside this core.
    std::vector<std::string> places;
    for (const std::string& folder : CoreFolders()) {
        places.push_back(Path::Combine(Path::Combine(folder, "pcsx2"), "resources"));
    }
    places.push_back(Path::Combine(Path::Combine(g_paths.system, "PCSX2"), "resources"));
    const auto found = std::find_if(places.begin(), places.end(), [](const std::string& place) {
        return FileSystem::FileExists(Path::Combine(place, "GameIndex.yaml").c_str());
    });
    if (found == places.end()) {
        Say(READER_CORE_LOG_ERROR, "PCSX2: its resources are neither beside the core (pcsx2/resources) nor in "
                                   "the console's system folder (PCSX2/resources)");
        return -1;
    }
    EmuFolders::Resources = *found;
    EmuFolders::AppRoot = std::string(Path::GetDirectory(Path::GetDirectory(*found)));
#ifdef _WIN32
    // PCSX2 loads Vulkan's shader compiler by its name alone, which Windows looks for beside
    // the program, not beside this core: loaded from here first, it is the one found then.
    for (const std::string& folder : CoreFolders()) {
        const std::wstring shaderc = StringUtil::UTF8StringToWideString(Path::Combine(folder, "shaderc_shared.dll"));
        if (LoadLibraryExW(shaderc.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) != nullptr) {
            break;
        }
    }
#endif
    EmuFolders::DataRoot = g_paths.data;
    EmuFolders::Settings = Path::Combine(g_paths.data, "inis");

    const char* error = nullptr;
    if (!VMManager::PerformEarlyHardwareChecks(&error)) {
        Say(READER_CORE_LOG_ERROR, std::string("PCSX2: ") + (error != nullptr ? error : "this processor cannot run it"));
        return -1;
    }

    // The font of PCSX2's own messages over the game (none are drawn, but its interface is set up).
    const std::string roboto =
        EmuFolders::GetOverridableResourcePath("fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
    // (Mapped for as long as the program runs, as pcsx2-gsrunner does.)
    if (const std::span<const u8> font_data = FileSystem::MapBinaryFileForRead(roboto.c_str()); !font_data.empty()) {
        std::vector<ImGuiManager::FontInfo> fonts;
        ImGuiManager::FontInfo font{};
        font.data = font_data;
        fonts.push_back(font);
        ImGuiManager::SetFonts(std::move(fonts));
    }

    Host::Internal::SetBaseSettingsLayer(&g_settings_layer);
    Host::Internal::SetSecretsSettingsLayer(&g_secrets_layer);
    VMManager::SetDefaultSettings(g_settings_layer, true, true, true, true, true);
    PutFixedSettings();
    PutSettings();
    PutBios();
    VMManager::Internal::LoadStartupSettings();

    // The console's picture has 448 lines (GSRenderer.cpp says so again each frame): what the
    // CRT picture's "Lines: as the console" means.
    Reader::Crt::SetNativeLines(448);
    return 0;
}

void Stop() {
    if (!g_thread.joinable()) {
        return;
    }
    // Asked on the CPU thread once a frame (or at once while the game is paused); the thread
    // ends once PCSX2 is taken down.
    RunLater([] { RequestStop(); });
    g_thread.join();
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
    bool replaced;
    {
        std::scoped_lock lock{g_window_mutex};
        replaced = window->kind != g_window.kind || window->handle != g_window.handle;
        g_window = *window;
    }
    (replaced ? g_window_replaced : g_window_resized) = true;
    g_queue_wake.notify_all();
    return 0;
}

void ApplySettings(const reader_core_setting_value* values, uint32_t count) {
    std::optional<std::string> crt;
    {
        std::scoped_lock lock{g_values_mutex};
        for (uint32_t i = 0; i < count; ++i) {
            if (values[i].key == nullptr || values[i].value == nullptr) {
                continue;
            }
            if (std::strcmp(values[i].key, READER_CORE_SETTING_CRT) == 0) {
                crt = values[i].value;
            } else {
                g_values[values[i].key] = values[i].value;
            }
        }
    }
    if (crt) {
        Reader::Crt::Deserialize(*crt);
    }
    auto apply = [] {
        PutSettings();
        if (VMManager::HasValidVM()) {
            VMManager::ApplySettings();
            ApplyLimiter();
        }
    };
    // On the CPU thread once it runs: PCSX2 takes its settings there.
    if (g_thread.joinable()) {
        RunLater(std::move(apply));
    } else {
        apply();
    }
}

void NoteDisc(const std::string& file);

int32_t LoadGame(const char* game) {
    if (g_host == nullptr || game == nullptr || *game == '\0') {
        return -1;
    }
    Stop();
    NoteDisc(game);
    {
        std::scoped_lock lock{g_queue_mutex};
        g_queue.clear();
    }
    g_thread = std::thread(CpuThread, std::string(game));
    return 0;
}

void Pause(int32_t paused) {
    if (!g_thread.joinable()) {
        return;
    }
    RunLater([paused] {
        if (VMManager::HasValidVM() && VMManager::GetState() != VMState::Stopping &&
            (VMManager::GetState() == VMState::Paused) != (paused != 0)) {
            VMManager::SetPaused(paused != 0);
        }
    });
}

int32_t SaveStateSlot(int32_t slot) {
    if (!g_thread.joinable()) {
        return -1;
    }
    RunLater([slot] {
        std::string failed = "no game";
        if (VMManager::HasValidVM()) {
            failed.clear();
            VMManager::SaveStateToSlot(slot, false, [&failed](const std::string& message) {
                failed = message.empty() ? "the state was not written" : message;
                Say(READER_CORE_LOG_ERROR, "PCSX2: savestate: " + message);
            });
        }
        if (READER_HOST_HAS(state_done)) {
            g_host->state_done(g_host->user, 0, slot, failed.empty() ? 0 : 1, failed.c_str());
        }
    });
    return 0;
}

int32_t LoadStateSlot(int32_t slot) {
    if (!g_thread.joinable()) {
        return -1;
    }
    RunLater([slot] {
        std::string failed = "no game";
        if (VMManager::HasValidVM()) {
            Error error;
            failed.clear();
            if (!VMManager::LoadStateFromSlot(slot, false, &error)) {
                failed = std::string(error.GetDescription());
                if (failed.empty()) {
                    failed = "nothing in that slot";
                }
                Say(READER_CORE_LOG_WARNING, "PCSX2: savestate " + std::to_string(slot) + " not loaded: " + failed);
            }
        }
        if (READER_HOST_HAS(state_done)) {
            g_host->state_done(g_host->user, 1, slot, failed.empty() ? 0 : 1, failed.c_str());
        }
    });
    return 0;
}

int32_t StateInfo(int32_t slot, reader_core_state_info* info) {
    if (info == nullptr) {
        return -1;
    }
    info->exists = 0;
    info->saved_ms = 0;
    if (!VMManager::HasValidVM()) {
        return 0;
    }
    const std::string path = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), slot);
    FILESYSTEM_STAT_DATA stat{};
    if (!path.empty() && FileSystem::StatFile(path.c_str(), &stat)) {
        info->exists = 1;
        info->saved_ms = static_cast<int64_t>(stat.ModificationTime) * 1000;
    }
    return 0;
}

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    const std::string serial = game_id != nullptr ? game_id : "";
    if (!Reader::Emu::IsGameSerial(serial) || each == nullptr || g_paths.saves.empty()) {
        return -1;
    }
    const std::string card = Reader::Emu::CardOf(serial);
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FILE, serial.c_str(),
                                          card.c_str()};
    each(context, &where);
    return 1;
}

// Discs: the one in, changed by its file (a PlayStation 2 has no list of discs).
std::atomic<bool> g_disc_in{false};
std::mutex g_disc_mutex;
std::string g_disc_file;

void NoteDisc(const std::string& file) {
    std::scoped_lock lock{g_disc_mutex};
    g_disc_file = file;
    g_disc_in = true;
}

uint32_t DiscCount() {
    return g_disc_in.load() ? 1 : 0;
}

int32_t DiscCurrent() {
    return g_disc_in.load() ? 0 : -1;
}

int32_t DiscLabel(uint32_t index, char* label, size_t size) {
    std::scoped_lock lock{g_disc_mutex};
    if (index != 0 || !g_disc_in.load() || label == nullptr || size == 0) {
        return -1;
    }
    std::snprintf(label, size, "%s", std::string(Path::GetFileTitle(g_disc_file)).c_str());
    return 0;
}

void DiscChanged(int32_t index, bool done) {
    if (READER_HOST_HAS(disc_changed)) {
        g_host->disc_changed(g_host->user, index, done ? 0 : 1);
    }
}

int32_t DiscInsert(int32_t index) {
    if (!g_thread.joinable() || index < -1 || index > 0) {
        return -1;
    }
    RunLater([index] {
        bool done = false;
        if (VMManager::HasValidVM()) {
            if (index < 0) {
                done = VMManager::ChangeDisc(CDVD_SourceType::NoDisc, {});
                g_disc_in = !done;
            } else {
                std::string file;
                {
                    std::scoped_lock lock{g_disc_mutex};
                    file = g_disc_file;
                }
                done = !file.empty() && VMManager::ChangeDisc(CDVD_SourceType::Iso, file);
                g_disc_in = done;
            }
        }
        DiscChanged(index, done);
    });
    return 0;
}

int32_t DiscAdd(const char* path) {
    if (!g_thread.joinable() || path == nullptr || *path == '\0') {
        return -1;
    }
    RunLater([file = std::string(path)] {
        const bool done = VMManager::HasValidVM() && VMManager::ChangeDisc(CDVD_SourceType::Iso, file);
        if (done) {
            std::scoped_lock lock{g_disc_mutex};
            g_disc_file = file;
            g_disc_in = true;
        }
        DiscChanged(done ? 0 : -1, done);
    });
    return 0;
}

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    if (size == 0 || !eeMem) {
        return 0;
    }
    if (address >= ScratchpadBase && address < ScratchpadBase + Ps2MemSize::Scratch) {
        const u32 offset = static_cast<u32>(address) - ScratchpadBase;
        const std::size_t valid = std::min<std::size_t>(size, Ps2MemSize::Scratch - offset);
        std::memcpy(buffer, eeMem->Scratch + offset, valid);
        return valid;
    }
    const std::optional<u32> offset = MainMemoryOffset(address);
    if (!offset) {
        return 0;
    }
    const std::size_t valid = std::min<std::size_t>(size, Ps2MemSize::ExposedRam - *offset);
    std::memcpy(buffer, eeMem->Main + *offset, valid);
    return valid;
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    // Data only, in main memory above the kernel's first megabyte: what a profile marking
    // progress writes (a game's own variables). Code written here would not reach what the
    // recompilers already made of it.
    const std::optional<u32> offset = MainMemoryOffset(address);
    if (!eeMem || size == 0 || !offset || *offset < 0x00100000 || size > Ps2MemSize::ExposedRam - *offset) {
        return -1;
    }
    std::memcpy(eeMem->Main + *offset, buffer, size);
    return 0;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    if (!eeMem) {
        return 0;
    }
    const reader_core_region all[] = {
        {0x00100000, Ps2MemSize::ExposedRam - 0x00100000,
         READER_CORE_REGION_READ | READER_CORE_REGION_WRITE | READER_CORE_REGION_EXECUTE, 2},
        {ScratchpadBase, Ps2MemSize::Scratch, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE, 2},
    };
    for (uint32_t i = 0; i < capacity && i < std::size(all); ++i) {
        regions[i] = all[i];
    }
    return static_cast<uint32_t>(std::size(all));
}

void HooksChanged() {
    // Hooks in the game's code are not carried out by this emulator's recompilers (as in the
    // app): a profile here reads memory once a frame.
}

void PressButton(int32_t bit, int32_t down) {
    if (bit < 0 || bit >= ButtonCount) {
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
    {
        std::scoped_lock lock{g_grabs_mutex};
        g_grabs.push_back(Grab{what, done, context});
    }
    g_queue_wake.notify_all();
    return 0;
}

// ---- What a file is, without running it ----

/// Reads `count` bytes of a disc image's 2048-byte sector `lsn`: a plain image (.iso) or one of
/// whole 2352-byte sectors (.bin, .img: a CD's mode 1 or mode 2 form 1 data at 16 or 24).
bool ReadSector(std::FILE* file, bool raw, u32 lsn, u8* out) {
    if (!raw) {
        return FileSystem::FSeek64(file, static_cast<s64>(lsn) * 2048, SEEK_SET) == 0 &&
               std::fread(out, 1, 2048, file) == 2048;
    }
    u8 sector[2352];
    if (FileSystem::FSeek64(file, static_cast<s64>(lsn) * 2352, SEEK_SET) != 0 ||
        std::fread(sector, 1, sizeof(sector), file) != sizeof(sector)) {
        return false;
    }
    std::memcpy(out, sector + (sector[15] == 2 ? 24 : 16), 2048);
    return true;
}

/// The disc's serial as its SYSTEM.CNF names its program ("BOOT2 = cdrom0:\SLPM_663.02;1" is
/// SLPM-66302, as PCSX2's cdvdGetDiscInfo makes it): a PlayStation 2 disc's alone (a
/// PlayStation's says BOOT, a PSP's has no SYSTEM.CNF).
std::string SerialOf(const std::string& path) {
    auto file = FileSystem::OpenManagedCFile(path.c_str(), "rb");
    if (!file) {
        return {};
    }
    u8 sector[2048];
    bool raw = false;
    if (!ReadSector(file.get(), false, 16, sector) || std::memcmp(sector + 1, "CD001", 5) != 0) {
        raw = true;
        if (!ReadSector(file.get(), true, 16, sector) || std::memcmp(sector + 1, "CD001", 5) != 0) {
            return {};
        }
    }
    const auto le32 = [](const u8* at) {
        return static_cast<u32>(at[0]) | static_cast<u32>(at[1]) << 8 | static_cast<u32>(at[2]) << 16 |
               static_cast<u32>(at[3]) << 24;
    };
    // The root folder's record in the primary volume descriptor.
    const u32 root = le32(sector + 156 + 2);
    const u32 root_size = std::min<u32>(le32(sector + 156 + 10), 64 * 2048);
    std::optional<std::pair<u32, u32>> cnf;
    for (u32 done = 0; done < root_size && !cnf; done += 2048) {
        if (!ReadSector(file.get(), raw, root + done / 2048, sector)) {
            return {};
        }
        for (u32 at = 0; at < 2048 && sector[at] != 0;) {
            const u8 length = sector[at];
            const u8 name_length = sector[at + 32];
            if (at + 33 + name_length > 2048) {
                break;
            }
            const std::string name(reinterpret_cast<const char*>(sector + at + 33), name_length);
            if (name == "SYSTEM.CNF;1" || name == "SYSTEM.CNF") {
                cnf = std::make_pair(le32(sector + at + 2), le32(sector + at + 10));
                break;
            }
            at += length;
        }
    }
    if (!cnf || cnf->second == 0 || !ReadSector(file.get(), raw, cnf->first, sector)) {
        return {};
    }
    const std::string text(reinterpret_cast<const char*>(sector), std::min<u32>(cnf->second, 2048));
    const std::size_t boot = text.find("BOOT2");
    if (boot == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find_first_of("\r\n", boot);
    std::string line = text.substr(boot, end == std::string::npos ? std::string::npos : end - boot);
    const std::size_t name_at = line.find_last_of("\\:/");
    if (name_at == std::string::npos) {
        return {};
    }
    std::string name = line.substr(name_at + 1);
    if (const std::size_t version = name.find(';'); version != std::string::npos) {
        name.resize(version);
    }
    std::string serial;
    for (const char c : name) {
        if (c == '_' || c == '-') {
            serial += '-';
        } else if (c != '.' && c != ' ') {
            serial += static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        }
    }
    return serial;
}

int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0) {
        return -1;
    }
    // Plain and raw disc images are read here (any thread, while a game runs too: PCSX2's own
    // reading goes through the one disc drive the running game has). A disc without a
    // PlayStation 2 program is no game a profile can be for.
    const std::string serial = SerialOf(path);
    if (serial.empty()) {
        return -1;
    }
    std::snprintf(id, id_size, "%s", serial.c_str());
    if (title != nullptr && title_size > 0) {
        title[0] = '\0'; // the program goes by the profile's title or the file's name
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
    api.running_game_code = nullptr; // no hooks are carried out here
    api.press_button = &PressButton;
    api.grab = &GrabPicture;
    api.identify = &Identify;
    api.save_locations = &SaveLocations;
    api.disc_count = &DiscCount;
    api.disc_current = &DiscCurrent;
    api.disc_label = &DiscLabel;
    api.disc_insert = &DiscInsert;
    api.disc_add = &DiscAdd;
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
