// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Flycast Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
// the repository, Reader Home's docs/one-program.md): Flycast's libretro build, with the
// libretro host every libretro-made core shares (cores/libretro/libretro_core.*) as its
// frontend. This file is the fork's part: it is reader/core/emulator.cpp's twin for the core
// (Flycast's files call the same Reader::Emu functions; the reader hears of a game through
// Flycast's own events, as in the app, and is told through Reader Home's host table), the
// Dreamcast's description (from this fork's console.h), the reader's way into its memory, how
// a disc is told apart, and the program's settings as Flycast's core options.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include "types.h"
#include "emulator.h"
#include "hw/aica/aica_if.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/sh4/sh4_mem.h"
#include "imgread/common.h"
#include "reios/reios.h"

#include "libretro.h"
#include "libretro_core.h"
#include "reader/core/console.h"
#include "reader/core/emulator.h"
#include "reader/render/crt_gl.h"
#include "achievements/achievements.h"
#include "cfg/option.h"
#include "reader/core_library/achievements_notifier.h"

#ifdef __ANDROID__
// Flycast's app does its requests in Java, which a core has not: RetroAchievements cannot reach
// its site from the Android app yet, and says so rather than reading a fault as an answer.
#include "oslib/http_client.h"
namespace http {
void init() {}
int get(const std::string&, std::vector<u8>&, const Headers*, Headers*) {
    return 503;
}
int post(const std::string&, const char*, const char*, std::vector<u8>&) {
    return 503;
}
} // namespace http
#endif

namespace {

namespace Traits = Reader::ConsoleTraits;
using ReaderLibretro::Host;

// ---- The Dreamcast as the reader knows it ----

/// The console's buttons by libretro's joypad bits (Flycast's libretro build: the lower face
/// button is A, the triggers are L2 and R2).
const reader_core_button Buttons[] = {
    {"A", RETRO_DEVICE_ID_JOYPAD_B},      {"B", RETRO_DEVICE_ID_JOYPAD_A},     {"X", RETRO_DEVICE_ID_JOYPAD_Y},
    {"Y", RETRO_DEVICE_ID_JOYPAD_X},      {"Start", RETRO_DEVICE_ID_JOYPAD_START},
    {"Up", RETRO_DEVICE_ID_JOYPAD_UP},    {"Down", RETRO_DEVICE_ID_JOYPAD_DOWN},
    {"Left", RETRO_DEVICE_ID_JOYPAD_LEFT}, {"Right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
    {"L", RETRO_DEVICE_ID_JOYPAD_L2},     {"R", RETRO_DEVICE_ID_JOYPAD_R2},
};

/// A button of this fork's console.h (Flycast's own numbering, which the app's reader presses)
/// as libretro's bit.
int RetroBit(int dreamcast_bit) {
    switch (dreamcast_bit) {
    case 2: return RETRO_DEVICE_ID_JOYPAD_B;  // A
    case 1: return RETRO_DEVICE_ID_JOYPAD_A;  // B
    case 10: return RETRO_DEVICE_ID_JOYPAD_Y; // X
    case 9: return RETRO_DEVICE_ID_JOYPAD_X;  // Y
    case 3: return RETRO_DEVICE_ID_JOYPAD_START;
    case 4: return RETRO_DEVICE_ID_JOYPAD_UP;
    case 5: return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case 6: return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case 7: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    default: return -1;
    }
}

const char* const FileTypes[] = {"chd", "cdi", "gdi", "cue", "iso"};

// The SH-4's registers are never handed over (no hooks here); the description still has its
// processor.
const char* const ResolutionChoices[] = {nullptr};
const char* const RegionChoices[] = {"japan", "usa", "europe", "default"};
const char* const LanguageChoices[] = {"japanese", "english", "german", "french", "spanish", "italian"};
const char* const FilterChoices[] = {"default", "nearest", "linear"};
#define READER_CHOICES(list) list, static_cast<uint32_t>(std::size(list))
const reader_core_setting Settings[] = {
    {READER_CORE_SETTING_RESOLUTION, nullptr, READER_CORE_SETTING_NUMBER, "1", 1, 6, 1, nullptr, 0},
    {READER_CORE_SETTING_FRAME_LIMIT, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 5, nullptr, 0},
    {"dc.region", "Console region", READER_CORE_SETTING_CHOICE, "japan", 0, 0, 0, READER_CHOICES(RegionChoices)},
    {"dc.language", "The console's language", READER_CORE_SETTING_CHOICE, "japanese", 0, 0, 0, READER_CHOICES(LanguageChoices)},
    {"dc.widescreen", "Widescreen (a hack: some games)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    // Karakuri (the branch karakuri/flycast-memory-writes): with the widescreen hack, the picture out
    // to the window's own shape (Flycast's rend.SuperWideScreen; 32:9 for Shenmue II with Karakuri's
    // profile) rather than 16:9. Needs proper testing on Windows.
    {"dc.superwide", "Widescreen to the window's shape (with the widescreen hack)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {"dc.texture_filter", "Texture smoothing", READER_CORE_SETTING_CHOICE, "default", 0, 0, 0, READER_CHOICES(FilterChoices)},
    {"dc.hle_bios", "Without the console's BIOS (its stand-in)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_CRT, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_SPEED, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 1000, 10, nullptr, 0},
};
#undef READER_CHOICES

const reader_core_device Devices[] = {{"Controller", RETRO_DEVICE_JOYPAD, 0b1111},
                                      {"Light gun", RETRO_DEVICE_LIGHTGUN, 0b1111}};

reader_core_info MakeInfo() {
    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "dc";
    info.name = "Flycast";
    info.version = "2.7 (Flycast Reader)";
    info.system = "Dreamcast";
    info.short_name = "DC";
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
    info.cpu.family = "sh4";
    info.cpu.value_count = 0;
    info.cpu.names = nullptr;
    info.cpu.name_count = 0;
    info.cpu.hook_alignment = 2;
    info.cpu.highest_address = 0xFFFFFFFFULL;
    info.settings = Settings;
    info.setting_count = static_cast<uint32_t>(std::size(Settings));
    // Since 2026-10-24: the four controller ports, a light gun on any (the pointer), the
    // libretro build's disc changes and rumble (the host answers them). Its cheats are empty.
    // Save sync's moments: a game's own memory unit (the first controller's first slot), in
    // saves/<product number>/ as Flycast Reader keeps it.
    info.features = READER_CORE_HAS_DISCS | READER_CORE_HAS_RUMBLE | READER_CORE_HAS_POINTER | READER_CORE_HAS_SAVE_SYNC |
                    READER_CORE_HAS_ACHIEVEMENTS;
    info.port_count = 4;
    info.devices = Devices;
    info.device_count = static_cast<uint32_t>(std::size(Devices));
    info.pointer_screens = 0b1;
    (void)ResolutionChoices;
    return info;
}

// ---- Memory, as the app's reader reads it (reader/core/emulator.cpp) ----

// The console's main memory is seen at 0x0C000000 and again every 0x20000000 above it (a
// program runs at 0x8C010000); the address's top three bits only say how it is reached.
constexpr u32 AreaMask = 0x1FFFFFFF;
constexpr u32 MainBase = 0x0C000000;
constexpr u32 MainEnd = 0x10000000;
constexpr u32 VideoBase = 0x04000000;
constexpr u32 Video32Base = 0x05000000;
constexpr u32 VideoEnd = 0x06000000;
constexpr u32 SoundBase = 0x00800000;
constexpr u32 SoundEnd = 0x01000000;
constexpr u32 ProgramBase = 0x8C010000;

size_t Slice(RamRegion& region, u32 size_of_region, u32 offset, void* buffer, size_t size) {
    const u8* data = &region[0];
    if (data == nullptr || size_of_region == 0) {
        return 0;
    }
    offset %= size_of_region;
    const size_t valid = std::min<size_t>(size, size_of_region - offset);
    std::memcpy(buffer, data + offset, valid);
    return valid;
}

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    if (address > 0xFFFFFFFFULL || size == 0 || address >= 0xE0000000ULL) {
        return 0;
    }
    u32 start = static_cast<u32>(address) & AreaMask;
    if (start >= MainBase && start < MainEnd) {
        return Slice(mem_b, RAM_SIZE, start - MainBase, buffer, size);
    }
    // 0x06000000-0x07FFFFFF mirrors the video memory's two views below.
    if (start >= VideoEnd && start < VideoEnd + (VideoEnd - VideoBase)) {
        start -= VideoEnd - VideoBase;
    }
    if (start >= VideoBase && start < Video32Base) {
        return Slice(vram, VRAM_SIZE, start - VideoBase, buffer, size);
    }
    if (start >= Video32Base && start < VideoEnd && &vram[0] != nullptr) {
        // The 32-bit view, the two banks interleaved every four bytes (hw/pvr/pvr_mem.cpp).
        const size_t valid = std::min<size_t>(size, VideoEnd - start);
        for (size_t i = 0; i < valid; ++i) {
            static_cast<u8*>(buffer)[i] = pvr_read32p<u8>(static_cast<u32>(start + i));
        }
        return valid;
    }
    if (start >= SoundBase && start < SoundEnd) {
        return Slice(aica::aica_ram, ARAM_SIZE, start - SoundBase, buffer, size);
    }
    return 0;
}

/// Main memory only (and its mirrors), all or nothing: emulator.cpp's DreamcastHost::WriteMemory
/// for the core. Code the recompiler already translated is not translated again for it.
int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    if (address > 0xFFFFFFFFULL || size == 0 || address >= 0xE0000000ULL || &mem_b[0] == nullptr) {
        return -1;
    }
    const u32 start = static_cast<u32>(address) & AreaMask;
    if (start < MainBase || start >= MainEnd) {
        return -1;
    }
    const u32 offset = (start - MainBase) % RAM_SIZE;
    if (offset + size > RAM_SIZE) {
        return -1;
    }
    std::memcpy(&mem_b[offset], buffer, size);
    return 0;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    if (&mem_b[0] == nullptr) {
        return 0;
    }
    const reader_core_region all[] = {
        {0x8C000000, 0x00010000, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE, 1},
        {ProgramBase, RAM_SIZE - 0x00010000, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE | READER_CORE_REGION_EXECUTE, 2},
        {0xA5000000, VRAM_SIZE, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE, 3},
    };
    for (uint32_t i = 0; i < capacity && i < std::size(all); ++i) {
        regions[i] = all[i];
    }
    return static_cast<uint32_t>(std::size(all));
}

// ---- A game, as the reader is told of it ----

u32 Crc32(const u8* data, size_t size) {
    u32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::string Trimmed(const char* field, size_t size) {
    std::string text(field, strnlen(field, size));
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) {
        text.pop_back();
    }
    return text;
}

/// The disc in the drive is the game from here on: its ID is its header's product number
/// ("HDR-0164"), what a profile binds itself to a checksum of that header (as in the app).
void GameStarted() {
    const reader_core_host* host = Host();
    if (host == nullptr) {
        return;
    }
    if (host->game_loading != nullptr) {
        host->game_loading(host->user);
    }
    char build[16];
    std::snprintf(build, sizeof(build), "%08X", Crc32(reinterpret_cast<const u8*>(&ip_meta), sizeof(ip_meta)));
    if (host->module_loaded != nullptr) {
        host->module_loaded(host->user, "main", ProgramBase, RAM_SIZE - 0x00010000, build);
    }
    if (host->game_loaded != nullptr) {
        const std::string game_id = settings.content.gameId;
        const std::string title = Trimmed(ip_meta.software_name, sizeof(ip_meta.software_name));
        host->game_loaded(host->user, game_id.c_str(), title.c_str());
    }
}

void OnEvent(Event event, void*) {
    const reader_core_host* host = Host();
    if (host == nullptr) {
        return;
    }
    switch (event) {
    case Event::Start:
    case Event::DiskChange: // the next disc of a game on several: another program
        if (settings.platform.isConsole()) {
            GameStarted();
        }
        break;
    case Event::LoadState:
        if (host->state_loaded != nullptr) {
            host->state_loaded(host->user);
        }
        break;
    case Event::VBlank:
        // A profile's places are in the 640x480 picture.
        if (host->frame != nullptr) {
            host->frame(host->user, 640, 480);
        }
        break;
    default:
        break;
    }
}

// ---- What a disc is, without running it ----

int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0) {
        return -1;
    }
    try {
        std::unique_ptr<Disc> disc(OpenDisc(path));
        if (!disc) {
            return -1;
        }
        u8 sector[2048] = {};
        disc->ReadSectors(disc->GetBaseFAD(), 1, sector, sizeof(sector));
        ip_meta_t header{};
        std::memcpy(&header, sector, sizeof(header));
        if (std::memcmp(header.hardware_id, "SEGA SEGAKATANA", 15) != 0) {
            return -1;
        }
        const std::string product = Trimmed(header.product_number, sizeof(header.product_number));
        if (product.empty()) {
            return -1;
        }
        std::snprintf(id, id_size, "%s", product.c_str());
        if (title != nullptr && title_size > 0) {
            std::snprintf(title, title_size, "%s", Trimmed(header.software_name, sizeof(header.software_name)).c_str());
        }
        return 0;
    } catch (...) {
        return -1;
    }
}

// ---- RetroAchievements: Flycast's own (core/achievements), with the program's login ----

std::string g_ra_user;
std::string g_ra_token;

/// The user's name, token and hardcore switch into Flycast's settings; with a name and a token
/// its client starts and logs in (Flycast loads the game's achievements when it starts or once
/// the login is through). Hardcore takes effect at a game's start, as in Flycast.
void ApplyAchievements(const std::string& key, const std::string& value) {
    if (key == READER_CORE_SETTING_ACHIEVEMENTS_USER) {
        g_ra_user = value;
    } else if (key == READER_CORE_SETTING_ACHIEVEMENTS_TOKEN) {
        g_ra_token = value;
    } else {
        config::AchievementsHardcoreMode.override(value == "1");
    }
    const bool on = !g_ra_user.empty() && !g_ra_token.empty();
    config::EnableAchievements.override(on);
    if (on) {
        config::AchievementsUserName.override(g_ra_user);
        config::AchievementsToken.override(g_ra_token);
        achievements::init();
    }
}

// ---- The program's settings, as Flycast's core options ----

void ApplySetting(const std::string& key, const std::string& value,
                  const std::function<void(const std::string&, const std::string&)>& set) {
    if (key == READER_CORE_SETTING_RESOLUTION) {
        const int times = std::clamp(std::atoi(value.c_str()), 1, 6);
        set("reicast_internal_resolution", std::to_string(640 * times) + "x" + std::to_string(480 * times));
    } else if (key == "dc.region") {
        set("reicast_region", value == "usa" ? "USA" : value == "europe" ? "Europe" : value == "default" ? "Default" : "Japan");
    } else if (key == "dc.language") {
        std::string language = value.empty() ? "Japanese" : value;
        language[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(language[0])));
        set("reicast_language", language);
    } else if (key == "dc.widescreen") {
        set("reicast_widescreen_hack", value == "1" ? "enabled" : "disabled");
    } else if (key == "dc.superwide") {
        config::SuperWidescreen.override(value == "1");
    } else if (key == "dc.texture_filter") {
        set("reicast_texture_filtering", value == "nearest" ? "1" : value == "linear" ? "2" : "0");
    } else if (key == "dc.hle_bios") {
        set("reicast_hle_bios", value == "1" ? "enabled" : "disabled");
    } else if (key == READER_CORE_SETTING_ACHIEVEMENTS_USER || key == READER_CORE_SETTING_ACHIEVEMENTS_TOKEN ||
               key == READER_CORE_SETTING_ACHIEVEMENTS_HARDCORE) {
        ApplyAchievements(key, value);
    }
}

/// Always so here: the emulation inside retro_run on the core's thread (the reader's frame and
/// memory then belong together), each game's own memory unit in the first slot.
const char* const FixedOptions[] = {
    "reicast_threaded_rendering", "disabled",
    "reicast_per_content_vmus", "VMU A1",
    nullptr, nullptr,
};

std::mutex g_save_mutex;
std::string g_save_game; ///< The game save sync was last told of (BeforeMemoryUnit)
int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context);

ReaderLibretro::Glue MakeGlue() {
    ReaderLibretro::Glue glue;
    glue.info = MakeInfo();
    glue.read_memory = &ReadMemory;
    glue.write_memory = &WriteMemory;
    glue.memory_regions = &MemoryRegions;
    glue.identify = &Identify;
    glue.apply_setting = &ApplySetting;
    glue.fixed_options = FixedOptions;
    glue.save_locations = &SaveLocations;
    glue.game_unloading = [] {
        std::scoped_lock lock{g_save_mutex};
        g_save_game.clear(); // the next start of the same game asks save sync again
    };
    return glue;
}

ReaderLibretro::Glue g_glue;

} // namespace

// ---- Flycast's renderer and the CRT picture ----

// In the app Flycast's OpenGL renderer draws the reader's CRT shader as it puts the frame on
// the screen (core/rend/gles/quad.cpp). In the core the frame goes into the libretro host's
// framebuffer, and the host draws the shader at the window's size: the renderer is told no.
bool readerCrtGlUse(bool, const ReaderCrtQuad&) {
    return false;
}
void readerCrtGlTerm() {}

// ---- What Flycast's files call (reader/core/emulator.h) ----

namespace Reader::Emu {

void Install() {
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    for (const Event event : {Event::Start, Event::DiskChange, Event::LoadState, Event::VBlank}) {
        EventManager::listen(event, OnEvent);
    }
}

void BeforeMemoryUnit(const std::string& port) {
    // Before a game's own memory unit is read: save sync puts the newest there (the program
    // waits for the service meanwhile). Once a game: again after a disc change to another game.
    const std::string game_id = settings.content.gameId;
    if (port != "A1" || !settings.platform.isConsole() || SaveFolderOf(game_id).empty() ||
        !READER_LIBRETRO_HOST_HAS(saves_opening)) {
        return;
    }
    {
        std::scoped_lock lock{g_save_mutex};
        if (g_save_game == game_id) {
            return;
        }
        g_save_game = game_id;
    }
    const std::string folder = SaveFolderOf(game_id);
    const std::string title = Trimmed(ip_meta.software_name, sizeof(ip_meta.software_name));
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, game_id.c_str(),
                                          folder.c_str()};
    const reader_core_saves saves{sizeof(reader_core_saves), game_id.c_str(), title.c_str(), 0, &where, 1};
    Host()->saves_opening(Host()->user, &saves);
}

void OnSaveWritten(const std::string& port) {
    // The game wrote its memory unit (the first slot's is the game's own): save sync's moment,
    // the program's to take.
    const reader_core_host* host = Host();
    if (host != nullptr && host->saved != nullptr && port == "A1") {
        const std::string folder = ReaderLibretro::SavesFolder();
        host->saved(host->user, folder.c_str());
    }
    std::string game_id;
    {
        std::scoped_lock lock{g_save_mutex};
        game_id = g_save_game;
    }
    if (port == "A1" && !game_id.empty() && READER_LIBRETRO_HOST_HAS(saves_written)) {
        const std::string folder = SaveFolderOf(game_id);
        Host()->saves_written(Host()->user, game_id.c_str(), folder.c_str(), READER_CORE_WRITE_GAME);
    }
}

std::string SaveFolderOf(const std::string& game_id) {
    // A game's own memory unit's folder, saves/<product number>, as Flycast Reader has it: only
    // for an ID of capitals, digits and hyphens (save sync names no other). A memory unit the
    // libretro build kept before (<ID>.A1.bin beside the folders) is moved in once.
    if (game_id.empty() || ReaderLibretro::SavesFolder().empty() ||
        !std::all_of(game_id.begin(), game_id.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        })) {
        return {};
    }
    const std::filesystem::path saves(ReaderLibretro::SavesFolder());
    const std::filesystem::path folder = saves / game_id;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path unit = folder / "vmu_save_A1.bin";
    const std::filesystem::path before = saves / (game_id + ".A1.bin");
    if (!std::filesystem::exists(unit, error) && std::filesystem::exists(before, error)) {
        std::filesystem::rename(before, unit, error);
    }
    return folder.string();
}

} // namespace Reader::Emu

namespace {

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    const std::string folder = game_id != nullptr ? Reader::Emu::SaveFolderOf(game_id) : std::string();
    if (folder.empty() || each == nullptr) {
        return -1;
    }
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, game_id,
                                          folder.c_str()};
    each(context, &where);
    return 1;
}

} // namespace

namespace Reader::Emu {

void OnAudio(const int16_t*, std::size_t) {
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
bool EnsureSaveSync() {
    return false;
}
std::string GameTitle() {
    return Trimmed(ip_meta.software_name, sizeof(ip_meta.software_name));
}
bool FilterKey(int, int, bool) {
    return false;
}
void PressButton(int, bool) {}
void PressTrigger(int, bool) {}
void RestSticks() {}
void MoveStick(int, int) {}
bool FilterStick(int, float, float) {
    return false;
}
bool DumpMemory(const std::string&) {
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

// ---- RetroAchievements' notifier (achievements_notifier.h): its messages to the program ----

namespace achievements {

Notification notifier;

void Notification::notify(Type, const std::string&, const std::string& text1, const std::string& text2,
                          const std::string&) {
    ReaderLibretro::Log(READER_CORE_LOG_INFO, "RetroAchievements: " + text1 + (text2.empty() ? "" : " — " + text2));
    if (READER_LIBRETRO_HOST_HAS(notify)) {
        ReaderLibretro::Host()->notify(ReaderLibretro::Host()->user, text1.c_str(), text2.c_str());
    }
}

} // namespace achievements

// ---- What Flycast's libretro build leaves out and its achievements need (core/cfg/option.cpp
// and the standalone app have them): their settings, kept in memory, and no settings file ----

namespace config {
Option<bool> EnableAchievements("", false);
Option<bool> AchievementsHardcoreMode("", false);
OptionString AchievementsUserName("", "");
OptionString AchievementsToken("", "");
OptionString AchievementsHostUrl("", "");
} // namespace config

void SaveSettings() {}
