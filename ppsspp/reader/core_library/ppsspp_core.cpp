// SPDX-FileCopyrightText: PPSSPP Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// PPSSPP Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
// the repository, Reader Home's docs/one-program.md): PPSSPP's libretro build, with the
// libretro host every libretro-made core shares (cores/libretro/libretro_core.*) as its
// frontend. This file is the fork's part: reader/core/emulator.cpp's twin for the core
// (PPSSPP's files call the same Reader::Emu functions, its JITs ask about hooks the same way;
// the answers come from Reader Home through its host table), the PSP's description (from
// this fork's console.h and registers.h), the reader's way into its memory, how a disc is
// told apart, and the program's settings as PPSSPP's core options.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include "Common/File/FileUtil.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/VFS/VFS.h"
#include "Core/Config.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/Loaders.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MemMap.h"
#include "Core/System.h"

#include "libretro.h"
#include "libretro_core.h"
#include "reader/core/console.h"
#include "reader/core/emulator.h"

namespace {

namespace Traits = Reader::ConsoleTraits;
using ReaderLibretro::Host;

// ---- The PSP as the reader knows it ----

/// The console's buttons by libretro's joypad bits (PPSSPP's libretro build: circle is A,
/// cross B, triangle X, square Y).
const reader_core_button Buttons[] = {
    {"Circle", RETRO_DEVICE_ID_JOYPAD_A},   {"Cross", RETRO_DEVICE_ID_JOYPAD_B},
    {"Triangle", RETRO_DEVICE_ID_JOYPAD_X}, {"Square", RETRO_DEVICE_ID_JOYPAD_Y},
    {"L1", RETRO_DEVICE_ID_JOYPAD_L},       {"R1", RETRO_DEVICE_ID_JOYPAD_R},
    {"Start", RETRO_DEVICE_ID_JOYPAD_START}, {"Select", RETRO_DEVICE_ID_JOYPAD_SELECT},
    {"Up", RETRO_DEVICE_ID_JOYPAD_UP},      {"Down", RETRO_DEVICE_ID_JOYPAD_DOWN},
    {"Left", RETRO_DEVICE_ID_JOYPAD_LEFT},  {"Right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
};

/// A button of this fork's console.h (the PSP's own bit, as the app's reader presses it) as
/// libretro's.
int RetroBit(int psp_bit) {
    switch (psp_bit) {
    case 13: return RETRO_DEVICE_ID_JOYPAD_A; // circle
    case 14: return RETRO_DEVICE_ID_JOYPAD_B; // cross
    case 12: return RETRO_DEVICE_ID_JOYPAD_X; // triangle
    case 15: return RETRO_DEVICE_ID_JOYPAD_Y; // square
    case 8: return RETRO_DEVICE_ID_JOYPAD_L;
    case 9: return RETRO_DEVICE_ID_JOYPAD_R;
    case 3: return RETRO_DEVICE_ID_JOYPAD_START;
    case 0: return RETRO_DEVICE_ID_JOYPAD_SELECT;
    case 4: return RETRO_DEVICE_ID_JOYPAD_UP;
    case 5: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    case 6: return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case 7: return RETRO_DEVICE_ID_JOYPAD_LEFT;
    default: return -1;
    }
}

const char* const FileTypes[] = {"iso", "cso", "chd", "pbp", "elf", "prx"};

// The Allegrex's registers as registers.h reads them: r0 to r31 by number and by their usual
// names, f0 to f31 (single floats), hi and lo. A snapshot holds the 32, the 32, hi, lo.
constexpr const char* RegisterNames[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
                                           "t3",   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
                                           "s6",   "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
constexpr uint32_t ValueCount = 66;
std::vector<std::string> g_number_names;
std::vector<reader_core_register_name> g_register_names;

const char* const LanguageChoices[] = {"japanese", "english", "automatic"};
const char* const ConfirmChoices[] = {"circle", "cross"};
const char* const FilterChoices[] = {"auto", "nearest", "linear"};
const char* const CpuChoices[] = {"jit", "ir", "interpreter"};
#define READER_CHOICES(list) list, static_cast<uint32_t>(std::size(list))
const reader_core_setting Settings[] = {
    {READER_CORE_SETTING_RESOLUTION, nullptr, READER_CORE_SETTING_NUMBER, "1", 1, 4, 1, nullptr, 0},
    {READER_CORE_SETTING_FRAME_LIMIT, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 5, nullptr, 0},
    {"psp.language", "The console's language", READER_CORE_SETTING_CHOICE, "japanese", 0, 0, 0, READER_CHOICES(LanguageChoices)},
    {"psp.confirm", "The button that says yes", READER_CORE_SETTING_CHOICE, "circle", 0, 0, 0, READER_CHOICES(ConfirmChoices)},
    {"psp.texture_filter", "Texture smoothing", READER_CORE_SETTING_CHOICE, "auto", 0, 0, 0, READER_CHOICES(FilterChoices)},
    {"psp.cpu", "The processor's emulation", READER_CORE_SETTING_CHOICE, "jit", 0, 0, 0, READER_CHOICES(CpuChoices)},
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
        g_register_names.push_back({g_number_names[32 + i].c_str(), 32 + i, READER_CORE_REGISTER_FLOAT32, 32});
    }
    g_register_names.push_back({"hi", 64, READER_CORE_REGISTER_INTEGER, 32});
    g_register_names.push_back({"lo", 65, READER_CORE_REGISTER_INTEGER, 32});

    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "psp";
    info.name = "PPSSPP";
    info.version = "1.20.4 (PPSSPP Reader)";
    info.system = "PlayStation Portable"; // as PPSSPP Reader names it in Reader Home's files
    info.short_name = "PSP";
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
    info.button_yes = RetroBit(Traits::ButtonYes);
    info.button_up = RetroBit(Traits::ButtonUp);
    info.button_down = RetroBit(Traits::ButtonDown);
    info.button_left = RetroBit(Traits::ButtonLeft);
    info.button_right = RetroBit(Traits::ButtonRight);
    info.button_press_ms = Traits::ButtonPressMs;
    info.pick_step_ms = Traits::PickStepMs;
    info.screen_width = Traits::ScreenWidth;
    info.screen_height = Traits::ScreenHeight;
    info.lower_screen_width = Traits::LowerScreenWidth;
    info.has_next_choice = 0;
    info.file_types = FileTypes;
    info.file_type_count = static_cast<uint32_t>(std::size(FileTypes));
    info.buttons = Buttons;
    info.button_count = static_cast<uint32_t>(std::size(Buttons));
    info.cpu.family = "mips-allegrex";
    info.cpu.value_count = ValueCount;
    info.cpu.names = g_register_names.data();
    info.cpu.name_count = static_cast<uint32_t>(g_register_names.size());
    info.cpu.hook_alignment = 4;
    info.cpu.highest_address = 0xFFFFFFFFULL;
    info.settings = Settings;
    info.setting_count = static_cast<uint32_t>(std::size(Settings));
    // Since 2026-10-24: CWCheat codes through the libretro build's cheats (the host hands them
    // over); one controller; save sync's moments (the game's SAVEDATA folders, as PPSSPP Reader
    // keeps them: the saves folder is the memory stick).
    info.features = READER_CORE_HAS_CODES | READER_CORE_HAS_SAVE_SYNC;
    info.port_count = 1;
    return info;
}

// ---- Memory, as the app's reader reads it (reader/core/emulator.cpp) ----

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    if (address > 0xFFFFFFFFULL || !Memory::base || size == 0) {
        return 0;
    }
    const u32 start = static_cast<u32>(address);
    const u32 want = static_cast<u32>(std::min<size_t>(size, 0x7FFFFFFF));
    const u32 valid = Memory::ClampValidSizeAt(start, want);
    if (valid == 0) {
        return 0;
    }
    std::memcpy(buffer, Memory::GetPointerUnchecked(start), valid);
    return valid;
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    if (address > 0xFFFFFFFFULL || !Memory::base || size == 0 || size > 0x7FFFFFFF) {
        return -1;
    }
    const u32 start = static_cast<u32>(address);
    if (Memory::ClampValidSizeAt(start, static_cast<u32>(size)) != size) {
        return -1; // all of it or nothing
    }
    std::memcpy(Memory::GetPointerWriteUnchecked(start), buffer, size);
    return 0;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    if (!Memory::base) {
        return 0;
    }
    const u32 user_end = PSP_GetUserMemoryEnd();
    constexpr uint32_t rw = READER_CORE_REGION_READ | READER_CORE_REGION_WRITE;
    const reader_core_region all[] = {
        {0x00010000, 0x00004000, rw, 4},
        {0x04000000, 0x00200000, rw, 3},
        {0x08000000, 0x00800000, rw | READER_CORE_REGION_EXECUTE, 1},
        {0x08800000, user_end - 0x08800000, rw | READER_CORE_REGION_EXECUTE, 2},
    };
    for (uint32_t i = 0; i < capacity && i < std::size(all); ++i) {
        regions[i] = all[i];
    }
    return static_cast<uint32_t>(std::size(all));
}

void UpdateAnyHooks();

void HooksChanged() {
    UpdateAnyHooks();
    // Safe from inside translated code too: the emulator defers the clearing then.
    if (PSP_IsInited() || PSP_GetBootState() == BootState::Booting) {
        mipsr4k.ClearJitCache();
    }
}

// ---- What a disc is, without running it (Core/Util/GameManager.cpp's way) ----

int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0) {
        return -1;
    }
    std::unique_ptr<FileLoader> loader(ConstructFileLoader(Path(path)));
    if (!loader || !loader->Exists()) {
        return -1;
    }
    std::string error;
    ParamSFOData sfo;
    bool read = false;
    switch (Identify_File(loader.get(), &error)) {
    case IdentifiedFileType::PSP_PBP: {
        PBPReader pbp(loader.get());
        std::vector<u8> data;
        read = pbp.GetSubFile(PBP_PARAM_SFO, &data) && sfo.ReadSFO(data);
        break;
    }
    case IdentifiedFileType::PSP_ISO:
    case IdentifiedFileType::PSP_ISO_NP: {
        SequentialHandleAllocator handles;
        std::shared_ptr<BlockDevice> device(ConstructBlockDevice(loader.get(), &error));
        if (!device) {
            return -1;
        }
        ISOFileSystem umd(&handles, device);
        const PSPFileInfo info = umd.GetFileInfo("/PSP_GAME/PARAM.SFO");
        const int handle = info.exists ? umd.OpenFile("/PSP_GAME/PARAM.SFO", FILEACCESS_READ) : -1;
        if (handle < 0) {
            return -1;
        }
        std::string data(static_cast<size_t>(info.size), '\0');
        umd.ReadFile(handle, reinterpret_cast<u8*>(data.data()), info.size);
        umd.CloseFile(handle);
        read = sfo.ReadSFO(reinterpret_cast<const u8*>(data.data()), data.size());
        break;
    }
    default:
        return -1;
    }
    const std::string disc = read ? sfo.GetValueString("DISC_ID") : std::string();
    if (disc.empty()) {
        return -1;
    }
    std::snprintf(id, id_size, "%s", disc.c_str());
    if (title != nullptr && title_size > 0) {
        std::snprintf(title, title_size, "%s", sfo.GetValueString("TITLE").c_str());
    }
    return 0;
}

// ---- The program's settings, as PPSSPP's core options ----

void ApplySetting(const std::string& key, const std::string& value,
                  const std::function<void(const std::string&, const std::string&)>& set) {
    if (key == READER_CORE_SETTING_RESOLUTION) {
        const int times = std::clamp(std::atoi(value.c_str()), 1, 4);
        static const char* const sizes[] = {"480x272", "960x544", "1440x816", "1920x1088"};
        set("ppsspp_internal_resolution", sizes[times - 1]);
    } else if (key == "psp.language") {
        set("ppsspp_language", value == "english" ? "English" : value == "automatic" ? "Automatic" : "Japanese");
    } else if (key == "psp.confirm") {
        set("ppsspp_button_preference", value == "cross" ? "Cross" : "Circle");
    } else if (key == "psp.texture_filter") {
        set("ppsspp_texture_filtering", value == "nearest" ? "Nearest" : value == "linear" ? "Linear" : "Auto");
    } else if (key == "psp.cpu") {
        set("ppsspp_cpu_core", value == "ir" ? "IR JIT" : value == "interpreter" ? "Interpreter" : "JIT");
    }
}

/// Always so here: OpenGL (the picture is drawn into the program's window with it), the
/// Japanese console that says yes with circle, unless the program says otherwise.
const char* const FixedOptions[] = {
    "ppsspp_backend", "opengl",
    "ppsspp_language", "Japanese",
    "ppsspp_button_preference", "Circle",
    nullptr, nullptr,
};

/// PPSSPP's assets (its fonts, the PSP's flash0, compat.ini): the console's system folder's
/// PPSSPP, where libretro's PPSSPP looks, or else the "ppsspp" folder beside this core, where
/// Reader Home's package puts them.
void UseAssetsBesideCore() {
    static bool done = false;
    if (done || File::Exists(Path(ReaderLibretro::SystemFolder()) / "PPSSPP" / "compat.ini")) {
        return;
    }
    const Path beside = Path(ReaderLibretro::CoreFolder()) / "ppsspp";
    if (!File::Exists(beside / "compat.ini")) {
        ReaderLibretro::Log(READER_CORE_LOG_WARNING, "PPSSPP's assets are neither in the console's system folder (PPSSPP) "
                                                     "nor beside the core (ppsspp)");
        return;
    }
    done = true;
    g_Config.flash0Directory = beside / "flash0";
    g_VFS.Register("", new DirectoryReader(beside));
    ReaderLibretro::Log(READER_CORE_LOG_INFO, "PPSSPP's assets: " + beside.ToString());
}

/// The memory stick's folders in the save folder the program gives (its root is ms0:): PPSSPP
/// writes a game's save into PSP/SAVEDATA and does not make PSP itself. And the assets.
void BeforeLoad() {
    const std::filesystem::path root(ReaderLibretro::SavesFolder());
    std::error_code error;
    for (const char* folder : {"PSP/SAVEDATA", "PSP/GAME", "PSP/SYSTEM"}) {
        std::filesystem::create_directories(root / folder, error);
    }
    UseAssetsBesideCore();
}

/// The memory stick's SAVEDATA, where a game's saves are the folders that begin with its ID.
std::string SaveDataFolder() {
    return (std::filesystem::path(ReaderLibretro::SavesFolder()) / "PSP" / "SAVEDATA").string();
}

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    if (game_id == nullptr || game_id[0] == '\0' || each == nullptr || ReaderLibretro::SavesFolder().empty()) {
        return -1;
    }
    const std::string folder = SaveDataFolder();
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_PREFIX, game_id,
                                          folder.c_str()};
    each(context, &where);
    return 1;
}

ReaderLibretro::Glue MakeGlue() {
    ReaderLibretro::Glue glue;
    glue.info = MakeInfo();
    glue.read_memory = &ReadMemory;
    glue.write_memory = &WriteMemory;
    glue.memory_regions = &MemoryRegions;
    glue.hooks_changed = &HooksChanged;
    glue.identify = &Identify;
    glue.apply_setting = &ApplySetting;
    glue.fixed_options = FixedOptions;
    glue.one_frame_a_run = true;
    glue.before_load = &BeforeLoad;
    glue.save_locations = &SaveLocations;
    return glue;
}

ReaderLibretro::Glue g_glue;
bool g_has_main = false;
/// The running game's ID, for its saves' moments (PPSSPP's files call without it).
std::mutex g_game_mutex;
std::string g_game_id;

} // namespace

// ---- What PPSSPP's files call (reader/core/emulator.h) ----

namespace Reader::Emu {

std::atomic<bool> g_any_hooks{false};

} // namespace Reader::Emu

namespace {

void UpdateAnyHooks() {
    const reader_core_host* host = Host();
    const bool has = host != nullptr &&
                     host->struct_size >= offsetof(reader_core_host, hook_count) + sizeof(host->hook_count) &&
                     host->hook_count != nullptr;
    Reader::Emu::g_any_hooks.store(has ? host->hook_count(host->user) != 0 : true, std::memory_order_relaxed);
}

} // namespace

namespace Reader::Emu {

void OnGameLoading() {
    g_has_main = false;
    if (const reader_core_host* host = Host(); host != nullptr && host->game_loading != nullptr) {
        host->game_loading(host->user);
    }
}

void OnExecLoading() {
    OnGameLoading();
}

void OnModuleLoaded(const char* name, uint32_t base, uint32_t size, uint32_t crc) {
    const reader_core_host* host = Host();
    if (host == nullptr || host->module_loaded == nullptr) {
        return;
    }
    // The first executable of a program is the game's own: "main" to profiles.
    char build[16];
    std::snprintf(build, sizeof(build), "%08X", crc);
    host->module_loaded(host->user, g_has_main ? (name != nullptr ? name : "") : "main", base, size, build);
    g_has_main = true;
}

void OnExecLoaded() {
    const reader_core_host* host = Host();
    if (host == nullptr) {
        return;
    }
    const std::string game_id = g_paramSFO.GetDiscID();
    const std::string title = g_paramSFO.GetValueString("TITLE");
    {
        std::scoped_lock lock{g_game_mutex};
        g_game_id = game_id;
    }
    // Before the game reads its save: save sync puts the newest there (the program waits for
    // the service meanwhile), as PPSSPP Reader does at this moment.
    if (READER_LIBRETRO_HOST_HAS(saves_opening) && !game_id.empty()) {
        const std::string folder = SaveDataFolder();
        const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_PREFIX,
                                              game_id.c_str(), folder.c_str()};
        const reader_core_saves saves{sizeof(reader_core_saves), game_id.c_str(), title.c_str(), 0, &where, 1};
        host->saves_opening(host->user, &saves);
    }
    if (host->game_loaded != nullptr) {
        host->game_loaded(host->user, game_id.c_str(), title.c_str()); // the profile goes in now
    }
    UpdateAnyHooks();
}

void OnShutdown() {
    g_any_hooks.store(false, std::memory_order_relaxed);
    g_has_main = false;
}

void OnSaveWritten() {
    if (const reader_core_host* host = Host(); host != nullptr && host->saved != nullptr) {
        host->saved(host->user, ReaderLibretro::SavesFolder().c_str());
    }
    if (READER_LIBRETRO_HOST_HAS(saves_written)) {
        std::string game_id;
        {
            std::scoped_lock lock{g_game_mutex};
            game_id = g_game_id;
        }
        const std::string folder = SaveDataFolder();
        Host()->saves_written(Host()->user, game_id.c_str(), folder.c_str(), READER_CORE_WRITE_GAME);
    }
}

void OnStateLoaded() {
    if (const reader_core_host* host = Host(); host != nullptr && host->state_loaded != nullptr) {
        host->state_loaded(host->user);
    }
}

void OnFrame() {
    // A profile's places are in the 480x272 screen.
    if (const reader_core_host* host = Host(); host != nullptr && host->frame != nullptr) {
        host->frame(host->user, 480, 272);
    }
}

bool IsHookedSlow(uint32_t pc) {
    const reader_core_host* host = Host();
    return host != nullptr && host->is_hooked != nullptr && host->is_hooked(host->user, pc) != 0;
}

void DispatchHook(uint32_t pc) {
    const reader_core_host* host = Host();
    if (host == nullptr || host->hook == nullptr) {
        return;
    }
    const MIPSState& cpu = *currentMIPS;
    uint64_t values[ValueCount];
    for (std::size_t i = 0; i < 32; ++i) {
        values[i] = cpu.r[i];
        values[32 + i] = static_cast<uint32_t>(cpu.fi[i]);
    }
    values[64] = cpu.hi;
    values[65] = cpu.lo;
    const reader_core_registers registers{pc, values, ValueCount};
    host->hook(host->user, &registers);
}

void OnAudio(const int32_t*, std::size_t) {
    // The sound reaches the program through libretro's sound callback (cores/libretro).
}

// What the app's frontend calls besides: not in a core (the program's reader has these).
void SetGameLoadedCallback(std::function<void(const std::string&)>) {}
void SetGameStoppedCallback(std::function<void()>) {}
void SetMenuCallback(std::function<void(const std::string&)>) {}
void OpenMenu(const std::string&) {}
void SetProfileCheck(std::function<bool(const std::string&)>) {}
bool HasProfileCheck() {
    return false;
}
bool HasProfile(const std::string&) {
    return false;
}
bool FilterKey(int, int, bool) {
    return false;
}
void PressButton(int, bool) {}
void RestSticks() {}
bool FilterStick(int, float, float) {
    return false;
}
bool EnsureSaveSync() {
    return false;
}
void ToggleNextChoice() {}
bool GoingToNextChoice() {
    return false;
}
void SetNextChoiceCallback(std::function<void(bool, int, std::uint64_t)>) {}

} // namespace Reader::Emu

extern "C" READER_CORE_EXPORT const reader_core_api* READER_CORE_CALL reader_core_get(uint32_t api_version) {
    if (api_version != READER_CORE_API_VERSION) {
        return nullptr;
    }
    if (g_glue.info.struct_size == 0) {
        g_glue = MakeGlue();
    }
    return ReaderLibretro::MakeApi(g_glue);
}
