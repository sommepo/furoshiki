// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Azahar Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
// the repository, Reader Home's docs/one-program.md): Azahar's libretro build, with the
// libretro host every libretro-made core shares (cores/libretro/libretro_core.*) as its
// frontend. This file is the fork's part: core/reader/emulator_engine.cpp's twin for the core
// (Azahar's files tell the reader of the game, its frames and its hooks through the same
// Reader::Emu calls; here they go to Reader Home through its host table), the 3DS's
// description (from this fork's console.h), the reader's way into the game's memory
// (emulator.cpp's GameHost, as in the app), where the two screens are, how a game file is
// told apart, and the program's settings as Azahar's core options.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>
#include "common/file_util.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/fs/archive.h"
#include "core/loader/loader.h"
#include "reader/core/console.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/save_hooks.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

#include "libretro.h"
#include "libretro_core.h"

namespace {

namespace Traits = Reader::ConsoleTraits;

// ---- The 3DS as the reader knows it ----

/// The console's buttons by libretro's joypad bits (Azahar's libretro build: each is its
/// namesake, ZL and ZR the second shoulder buttons).
const reader_core_button Buttons[] = {
    {"A", RETRO_DEVICE_ID_JOYPAD_A},         {"B", RETRO_DEVICE_ID_JOYPAD_B},
    {"X", RETRO_DEVICE_ID_JOYPAD_X},         {"Y", RETRO_DEVICE_ID_JOYPAD_Y},
    {"L", RETRO_DEVICE_ID_JOYPAD_L},         {"R", RETRO_DEVICE_ID_JOYPAD_R},
    {"ZL", RETRO_DEVICE_ID_JOYPAD_L2},       {"ZR", RETRO_DEVICE_ID_JOYPAD_R2},
    {"Start", RETRO_DEVICE_ID_JOYPAD_START}, {"Select", RETRO_DEVICE_ID_JOYPAD_SELECT},
    {"Up", RETRO_DEVICE_ID_JOYPAD_UP},       {"Down", RETRO_DEVICE_ID_JOYPAD_DOWN},
    {"Left", RETRO_DEVICE_ID_JOYPAD_LEFT},   {"Right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
};

/// A button of this fork's console.h (the Android app's numbers, NativeLibrary.ButtonType) as
/// libretro's.
int RetroBit(int button) {
    switch (button) {
    case 700:
        return RETRO_DEVICE_ID_JOYPAD_A;
    case 701:
        return RETRO_DEVICE_ID_JOYPAD_B;
    case 702:
        return RETRO_DEVICE_ID_JOYPAD_X;
    case 703:
        return RETRO_DEVICE_ID_JOYPAD_Y;
    case 704:
        return RETRO_DEVICE_ID_JOYPAD_START;
    case 705:
        return RETRO_DEVICE_ID_JOYPAD_SELECT;
    case 773:
        return RETRO_DEVICE_ID_JOYPAD_L;
    case 774:
        return RETRO_DEVICE_ID_JOYPAD_R;
    case 709:
        return RETRO_DEVICE_ID_JOYPAD_UP;
    case 710:
        return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case 711:
        return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case 712:
        return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    default:
        return -1;
    }
}

const char* const FileTypes[] = {"3ds", "cci", "cxi", "cia", "3dsx", "app", "elf", "axf"};

// The ARM11's registers as this fork's registers.h reads them: r0 to r15 (sp, lr and pc too),
// s0 to s31 (single floats) and d0 to d15 (doubles, the s registers in pairs). A snapshot holds
// the 16, the 32 single floats' bits, then the 16 doubles' bits.
constexpr uint32_t ValueCount = 16 + 32 + 16;
std::vector<std::string> g_number_names;
std::vector<reader_core_register_name> g_register_names;

const char* const LanguageChoices[] = {"japanese", "english"};
const char* const RegionChoices[] = {"auto", "japan", "usa", "europe"};
const char* const LayoutChoices[] = {"top_bottom", "side_by_side", "large_screen"};
#define READER_CHOICES(list) list, static_cast<uint32_t>(std::size(list))
const reader_core_setting Settings[] = {
    {READER_CORE_SETTING_RESOLUTION, nullptr, READER_CORE_SETTING_NUMBER, "1", 1, 8, 1, nullptr, 0},
    {READER_CORE_SETTING_FRAME_LIMIT, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr,
     0},
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 5, nullptr, 0},
    {"3ds.language", "The console's language", READER_CORE_SETTING_CHOICE, "japanese", 0, 0, 0,
     READER_CHOICES(LanguageChoices)},
    {"3ds.region", "Console region", READER_CORE_SETTING_CHOICE, "auto", 0, 0, 0,
     READER_CHOICES(RegionChoices)},
    {"3ds.layout", "The two screens", READER_CORE_SETTING_CHOICE, "top_bottom", 0, 0, 0,
     READER_CHOICES(LayoutChoices)},
    {"3ds.new_3ds", "A New 3DS", READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_CRT, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_SPEED, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 1000, 10, nullptr,
     0},
};
#undef READER_CHOICES

reader_core_info MakeInfo() {
    g_number_names.clear();
    for (int i = 0; i < 16; ++i) {
        g_number_names.push_back("r" + std::to_string(i));
    }
    for (int i = 0; i < 32; ++i) {
        g_number_names.push_back("s" + std::to_string(i));
    }
    for (int i = 0; i < 16; ++i) {
        g_number_names.push_back("d" + std::to_string(i));
    }
    g_register_names.clear();
    for (uint32_t i = 0; i < 16; ++i) {
        g_register_names.push_back(
            {g_number_names[i].c_str(), i, READER_CORE_REGISTER_INTEGER, 32});
    }
    g_register_names.push_back({"sp", 13, READER_CORE_REGISTER_INTEGER, 32});
    g_register_names.push_back({"lr", 14, READER_CORE_REGISTER_INTEGER, 32});
    g_register_names.push_back({"pc", 15, READER_CORE_REGISTER_INTEGER, 32});
    for (uint32_t i = 0; i < 32; ++i) {
        g_register_names.push_back(
            {g_number_names[16 + i].c_str(), 16 + i, READER_CORE_REGISTER_FLOAT32, 32});
    }
    for (uint32_t i = 0; i < 16; ++i) {
        g_register_names.push_back(
            {g_number_names[48 + i].c_str(), 48 + i, READER_CORE_REGISTER_FLOAT64, 64});
    }

    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "3ds";
    info.name = "Azahar";
    info.version = "2126.1.2 (Azahar Reader)";
    info.system = "Nintendo 3DS";
    info.short_name = "3DS";
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
    info.cpu.family = "armv6k";
    info.cpu.value_count = ValueCount;
    info.cpu.names = g_register_names.data();
    info.cpu.name_count = static_cast<uint32_t>(g_register_names.size());
    info.cpu.hook_alignment = 2;
    info.cpu.thumb_odd = 1;
    info.cpu.highest_address = 0xFFFFFFFFULL;
    info.settings = Settings;
    info.setting_count = static_cast<uint32_t>(std::size(Settings));
    // Since 2026-10-24: a finger on the lower screen (the libretro build's touchscreen, on by
    // default, read as libretro's pointer: the host puts it where the lower screen is drawn);
    // save sync's moments (Azahar's save hooks, as Azahar Reader ties them to its save sync).
    info.features = READER_CORE_HAS_POINTER | READER_CORE_HAS_SAVE_SYNC;
    info.port_count = 1;
    info.pointer_screens = 0b10;
    return info;
}

// ---- The running game, as emulator.cpp's GameHost reaches it ----

std::atomic<Core::System*> g_system{nullptr};
std::atomic<u32> g_process_id{0};
/// The reader's hooks changed: the translated code is dropped at the next frame, on the
/// emulation thread.
std::atomic<bool> g_drop_code{false};

Reader::Host* RunningGame() {
    Core::System* system = g_system.load();
    return system != nullptr ? &Reader::Emu::GameHost(*system, g_process_id.load()) : nullptr;
}

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    Reader::Host* host = RunningGame();
    if (host == nullptr) {
        return 0;
    }
    const std::string bytes = host->ReadMemory(address, size);
    std::copy(bytes.begin(), bytes.end(), static_cast<char*>(buffer));
    return bytes.size();
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    Reader::Host* host = RunningGame();
    if (host == nullptr) {
        return -1;
    }
    return host->WriteMemory(address, std::string(static_cast<const char*>(buffer), size)) ? 0 : -1;
}

void HooksChanged() {
    g_drop_code.store(true);
}

int32_t RunningGameCode() {
    Reader::Host* host = RunningGame();
    return host == nullptr || host->RunningGameCode() ? 1 : 0;
}

// ---- The two screens, as the emulator lays them out in its picture ----

bool ScreenRects(float rects[2][4]) {
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return false;
    }
    const Layout::FramebufferLayout& layout =
        system.GPU().Renderer().GetRenderWindow().GetFramebufferLayout();
    if (layout.width == 0 || layout.height == 0) {
        return false;
    }
    const auto put = [&layout](float* out, bool shown, const Common::Rectangle<u32>& rect) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        if (shown && rect.GetWidth() > 0 && rect.GetHeight() > 0) {
            out[0] = static_cast<float>(rect.left) / layout.width;
            out[1] = static_cast<float>(rect.top) / layout.height;
            out[2] = static_cast<float>(rect.GetWidth()) / layout.width;
            out[3] = static_cast<float>(rect.GetHeight()) / layout.height;
        }
    };
    put(rects[0], layout.top_screen_enabled, layout.top_screen);
    put(rects[1], layout.bottom_screen_enabled, layout.bottom_screen);
    return true;
}

// ---- What a game file is, without running it ----

int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0) {
        return -1;
    }
    try {
        const std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(path);
        u64 program_id = 0;
        if (!loader || loader->ReadProgramId(program_id) != Loader::ResultStatus::Success ||
            program_id == 0) {
            return -1;
        }
        std::snprintf(id, id_size, "%s", Reader::Emu::GameId(program_id).c_str());
        if (title != nullptr && title_size > 0) {
            std::string name;
            loader->ReadTitle(name);
            std::snprintf(title, title_size, "%s", name.c_str());
        }
        return 0;
    } catch (...) {
        return -1;
    }
}

// ---- The program's settings, as Azahar's core options ----

void ApplySetting(const std::string& key, const std::string& value,
                  const std::function<void(const std::string&, const std::string&)>& set) {
    if (key == READER_CORE_SETTING_RESOLUTION) {
        set("citra_resolution_factor", std::to_string(std::clamp(std::atoi(value.c_str()), 1, 8)));
    } else if (key == "3ds.language") {
        set("citra_language_value", value == "english" ? "English" : "Japanese");
    } else if (key == "3ds.region") {
        set("citra_region_value", value == "japan"    ? "Japan"
                                  : value == "usa"    ? "USA"
                                  : value == "europe" ? "Europe"
                                                      : "Auto");
    } else if (key == "3ds.layout") {
        set("citra_layout_option", value == "side_by_side"   ? "side_by_side"
                                   : value == "large_screen" ? "large_screen"
                                                             : "default");
        // Large enough to touch: the core's four times leaves a finger too little of it.
        set("citra_large_screen_proportion", "2.25");
    } else if (key == "3ds.new_3ds") {
        set("citra_is_new_3ds", value == "0" ? "Old 3DS" : "New 3DS");
    }
}

/// Always so here: OpenGL (the picture is drawn into the program's window with it) and the
/// processor's JIT (where the reader's hooks are).
const char* const FixedOptions[] = {
    "citra_graphics_api",
    "OpenGL",
    "citra_use_cpu_jit",
    "enabled",
    nullptr,
    nullptr,
};

/// Until the program's settings say otherwise: the Japanese console, one screen over the other.
const char* const DefaultOptions[] = {
    "citra_language_value",
    "Japanese",
    "citra_layout_option",
    "default",
    nullptr,
    nullptr,
};

/// A game's save data on the emulator's SD card (the libretro build's, in Azahar/ of the saves
/// folder the program gives): title/<high>/<low>/data, the program ID's halves in small digits.
std::string SaveDataOf(u64 program_id) {
    return fmt::format("{}Nintendo 3DS/{}/{}/title/{:08x}/{:08x}/data", FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir),
                       SYSTEM_ID, SDCARD_ID, static_cast<u32>(program_id >> 32), static_cast<u32>(program_id));
}

std::atomic<u64> g_save_program{0}; ///< The game whose saves save sync was told of

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    const u64 program_id = game_id != nullptr ? Reader::Emu::TitleId(game_id) : 0;
    if (program_id == 0 || each == nullptr) {
        return -1;
    }
    const std::string folder = SaveDataOf(program_id);
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, game_id,
                                          folder.c_str()};
    each(context, &where);
    return 1;
}

/// Azahar's save hooks (core.cpp, fs_user.cpp) to the program's save moments: before the game
/// runs (it has not opened its save yet; the program waits while the newest is fetched), and
/// each time the game makes its save final.
void TieSaveHooks() {
    Reader::SaveHooks::game_loading = [](u64 program_id) {
        g_save_program.store(program_id);
        if (!READER_LIBRETRO_HOST_HAS(saves_opening)) {
            return;
        }
        const std::string game_id = Reader::Emu::GameId(program_id);
        const std::string folder = SaveDataOf(program_id);
        const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER,
                                              game_id.c_str(), folder.c_str()};
        const reader_core_saves saves{sizeof(reader_core_saves), game_id.c_str(), "", program_id, &where, 1};
        ReaderLibretro::Host()->saves_opening(ReaderLibretro::Host()->user, &saves);
    };
    Reader::SaveHooks::save_committed = [] {
        const u64 program_id = g_save_program.load();
        if (program_id == 0 || !READER_LIBRETRO_HOST_HAS(saves_written)) {
            return;
        }
        const std::string game_id = Reader::Emu::GameId(program_id);
        const std::string folder = SaveDataOf(program_id);
        ReaderLibretro::Host()->saves_written(ReaderLibretro::Host()->user, game_id.c_str(), folder.c_str(),
                                              READER_CORE_WRITE_GAME);
    };
    Reader::SaveHooks::game_stopped = [] { g_save_program.store(0); };
}

ReaderLibretro::Glue MakeGlue() {
    ReaderLibretro::Glue glue;
    glue.info = MakeInfo();
    glue.read_memory = &ReadMemory;
    glue.write_memory = &WriteMemory;
    glue.hooks_changed = &HooksChanged;
    glue.identify = &Identify;
    glue.apply_setting = &ApplySetting;
    glue.fixed_options = FixedOptions;
    glue.default_options = DefaultOptions;
    glue.screen_rects = &ScreenRects;
    glue.running_game_code = &RunningGameCode;
    // Azahar's renderer draws the CRT picture on each screen itself
    // (video_core/reader_crt_present.h).
    glue.crt_in_core = true;
    glue.save_locations = &SaveLocations;
    TieSaveHooks();
    return glue;
}

ReaderLibretro::Glue g_glue;

} // namespace

// ---- What Azahar's files call (core/reader/emulator.h) ----

namespace Reader::Emu {

void OnGameStarted(Core::System& system, u32 process_id, u64 title_id) {
    g_process_id.store(process_id);
    g_system.store(&system);
    const reader_core_host* host = ReaderLibretro::Host();
    if (host == nullptr) {
        return;
    }
    if (host->game_loading != nullptr) {
        host->game_loading(host->user);
    }
    std::string title;
    if (system.IsPoweredOn()) {
        system.GetAppLoader().ReadTitle(title);
    }
    if (host->game_loaded != nullptr) {
        host->game_loaded(host->user, GameId(title_id).c_str(),
                          title.c_str()); // the profile goes in now
    }
}

void OnGameStopped() {
    g_system.store(nullptr);
}

void OnFrame() {
    if (g_drop_code.exchange(false)) {
        if (Reader::Host* host = RunningGame()) {
            host->InvalidateCode();
        }
    }
    // A profile's places are in the upper screen's 400x240.
    if (const reader_core_host* host = ReaderLibretro::Host();
        host != nullptr && host->frame != nullptr) {
        host->frame(host->user, 400, 240);
    }
}

void OnStateLoaded() {
    if (const reader_core_host* host = ReaderLibretro::Host();
        host != nullptr && host->state_loaded != nullptr) {
        host->state_loaded(host->user);
    }
}

bool IsHooked(u32 pc) {
    const reader_core_host* host = ReaderLibretro::Host();
    return host != nullptr && host->is_hooked != nullptr && host->is_hooked(host->user, pc) != 0;
}

void DispatchHook(const GuestRegisters& registers) {
    const reader_core_host* host = ReaderLibretro::Host();
    if (host == nullptr || host->hook == nullptr) {
        return;
    }
    uint64_t values[ValueCount];
    for (std::size_t i = 0; i < 16; ++i) {
        values[i] = registers.r[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        values[16 + i] = registers.s[i];
    }
    for (std::size_t i = 0; i < 16; ++i) {
        values[48 + i] = static_cast<uint64_t>(registers.s[2 * i]) |
                         (static_cast<uint64_t>(registers.s[2 * i + 1]) << 32);
    }
    const reader_core_registers snapshot{registers.r[15], values, ValueCount};
    host->hook(host->user, &snapshot);
}

void ReportUnusableHook(u32 pc, const std::string& why) {
    if (const reader_core_host* host = ReaderLibretro::Host();
        host != nullptr && host->hook_unusable != nullptr) {
        host->hook_unusable(host->user, pc, why.c_str());
    }
}

} // namespace Reader::Emu

extern "C" READER_CORE_EXPORT const reader_core_api* READER_CORE_CALL
reader_core_get(uint32_t api_version) {
    if (api_version != READER_CORE_API_VERSION) {
        return nullptr;
    }
    if (g_glue.info.struct_size == 0) {
        g_glue = MakeGlue();
    }
    return ReaderLibretro::MakeApi(g_glue);
}
