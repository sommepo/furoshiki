// SPDX-FileCopyrightText: melonDS Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// melonDS Reader as a core of Furoshiki (cores/reader_core.h at the top of the repository):
// melonDS's libretro build, with the libretro host every libretro-made core shares
// (cores/libretro/libretro_core.*) as its frontend. This file is the fork's part: the Nintendo
// DS's description (from this fork's console.h), the reader's way into its memory and its hooks
// (reader/core/emulator.h, which melonDS's interpreter calls), where the two screens are, how a
// game file is told apart, and the program's settings as melonDS's core options.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "libretro.h"
#include "libretro_core.h"
#include "reader/core/console.h"
#include "reader/core/emulator.h"
#include "reader/core/screens.h"

namespace {

namespace Traits = Reader::ConsoleTraits;
using ReaderLibretro::Host;

// ---- The DS as the reader knows it ----

/// The console's buttons by libretro's joypad bits (melonDS's libretro build: each its namesake).
const reader_core_button Buttons[] = {
    {"A", RETRO_DEVICE_ID_JOYPAD_A},         {"B", RETRO_DEVICE_ID_JOYPAD_B},
    {"X", RETRO_DEVICE_ID_JOYPAD_X},         {"Y", RETRO_DEVICE_ID_JOYPAD_Y},
    {"L", RETRO_DEVICE_ID_JOYPAD_L},         {"R", RETRO_DEVICE_ID_JOYPAD_R},
    {"Start", RETRO_DEVICE_ID_JOYPAD_START}, {"Select", RETRO_DEVICE_ID_JOYPAD_SELECT},
    {"Up", RETRO_DEVICE_ID_JOYPAD_UP},       {"Down", RETRO_DEVICE_ID_JOYPAD_DOWN},
    {"Left", RETRO_DEVICE_ID_JOYPAD_LEFT},   {"Right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
    {"Close the lid", RETRO_DEVICE_ID_JOYPAD_L3}, {"Blow into the microphone", RETRO_DEVICE_ID_JOYPAD_L2},
};

const char* const FileTypes[] = {"nds"};

// The ARM9's registers: r0 to r15, sp, lr and pc their other names. No floating point.
constexpr uint32_t ValueCount = 16;
std::string g_number_names[16];
reader_core_register_name g_register_names[19];

const char* const LayoutChoices[] = {"top_bottom", "side_by_side", "top_only", "bottom_only", "hybrid"};
const char* const RotationChoices[] = {"auto", "none", "left", "right"};
const char* const LanguageChoices[] = {"japanese", "english"};
#define READER_CHOICES(list) list, static_cast<uint32_t>(std::size(list))
const reader_core_setting Settings[] = {
    {READER_CORE_SETTING_FRAME_LIMIT, nullptr, READER_CORE_SETTING_SWITCH, "1", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 5, nullptr, 0},
    {"nds.layout", "The two screens", READER_CORE_SETTING_CHOICE, "top_bottom", 0, 0, 0, READER_CHOICES(LayoutChoices)},
    // A game held like a book (Wish Room) is drawn on its side, as the DS is held: by default the
    // picture is the DS's own and the device is turned (the user, 2026-11-15: "as designed");
    // "auto" turns it for the games known to be held so, on a device that is not turned.
    {"nds.rotation", "The picture turned (a game held like a book)", READER_CORE_SETTING_CHOICE, "none", 0, 0, 0,
     READER_CHOICES(RotationChoices)},
    {"nds.gap", "Space between the screens", READER_CORE_SETTING_NUMBER, "0", 0, 126, 2, nullptr, 0},
    {"nds.language", "The console's language", READER_CORE_SETTING_CHOICE, "japanese", 0, 0, 0, READER_CHOICES(LanguageChoices)},
    {"nds.boot_menu", "Start from the console's menu (needs its BIOS and firmware)", READER_CORE_SETTING_SWITCH, "0", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_CRT, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_SPEED, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 1000, 10, nullptr, 0},
};
#undef READER_CHOICES

reader_core_info MakeInfo() {
    for (uint32_t i = 0; i < 16; ++i) {
        g_number_names[i] = "r" + std::to_string(i);
        g_register_names[i] = {g_number_names[i].c_str(), i, READER_CORE_REGISTER_INTEGER, 32};
    }
    g_register_names[16] = {"sp", 13, READER_CORE_REGISTER_INTEGER, 32};
    g_register_names[17] = {"lr", 14, READER_CORE_REGISTER_INTEGER, 32};
    g_register_names[18] = {"pc", 15, READER_CORE_REGISTER_INTEGER, 32};

    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "nds";
    info.name = "melonDS";
    info.version = "0.9.3 (melonDS Reader)";
    info.system = "Nintendo DS";
    info.short_name = "DS";
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
    info.has_next_choice = 0;
    info.file_types = FileTypes;
    info.file_type_count = static_cast<uint32_t>(std::size(FileTypes));
    info.buttons = Buttons;
    info.button_count = static_cast<uint32_t>(std::size(Buttons));
    info.cpu.family = "armv5te";
    info.cpu.value_count = ValueCount;
    info.cpu.names = g_register_names;
    info.cpu.name_count = static_cast<uint32_t>(std::size(g_register_names));
    info.cpu.hook_alignment = 2;
    info.cpu.thumb_odd = 1;
    info.cpu.highest_address = 0xFFFFFFFFULL;
    info.settings = Settings;
    info.setting_count = static_cast<uint32_t>(std::size(Settings));
    // A finger on the lower screen (the libretro build's touch mode, read as libretro's pointer:
    // the host puts it where the lower screen is drawn).
    info.features = READER_CORE_HAS_POINTER;
    info.port_count = 1;
    info.pointer_screens = 0b10;
    return info;
}

// ---- The reader's way in ----

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    return Reader::Emu::ReadMemory(address, buffer, size);
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    return Reader::Emu::WriteMemory(address, buffer, size) ? 0 : -1;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    const auto all = Reader::Emu::MemoryRegions();
    uint32_t n = 0;
    for (const auto& region : all) {
        if (n < capacity && regions != nullptr) {
            regions[n] = reader_core_region{};
            regions[n].base = region.base;
            regions[n].size = region.size;
            regions[n].flags = READER_CORE_REGION_READ | (region.writable ? READER_CORE_REGION_WRITE : 0) |
                               (region.executable ? READER_CORE_REGION_EXECUTE : 0);
            regions[n].state = region.state;
        }
        ++n;
    }
    return n;
}

void HooksChanged() {
    bool any = true;
    if (READER_LIBRETRO_HOST_HAS(hook_count)) {
        any = Host()->hook_count(Host()->user) > 0;
    }
    Reader::Emu::HooksChanged(any);
}

// ---- What melonDS's files call (reader/core/emulator.h) ----

bool IsHooked(u32 pc) {
    const reader_core_host* host = Host();
    return host != nullptr && host->is_hooked != nullptr && host->is_hooked(host->user, pc) != 0;
}

void Hook(const u32 registers[16]) {
    const reader_core_host* host = Host();
    if (host == nullptr || host->hook == nullptr) {
        return;
    }
    uint64_t values[ValueCount];
    for (uint32_t i = 0; i < ValueCount; ++i) {
        values[i] = registers[i];
    }
    const reader_core_registers snapshot{registers[15], values, ValueCount};
    host->hook(host->user, &snapshot);
}

void Frame() {
    // A profile's places are in the upper screen's 256x192.
    if (const reader_core_host* host = Host(); host != nullptr && host->frame != nullptr) {
        host->frame(host->user, 256, 192);
    }
}

const Reader::Emu::Sink g_sink{&IsHooked, &Hook, &Frame};

void GameLoaded() {
    Reader::Emu::SetSink(&g_sink);
    const reader_core_host* host = Host();
    if (host == nullptr) {
        return;
    }
    if (host->game_loading != nullptr) {
        host->game_loading(host->user);
    }
    const std::string id = Reader::Emu::GameId();
    const std::string title = Reader::Emu::GameTitle();
    if (host->game_loaded != nullptr) {
        host->game_loaded(host->user, id.c_str(), title.c_str()); // the profile goes in now
    }
    HooksChanged();
}

void GameUnloading() {
    Reader::Emu::HooksChanged(false);
}

bool ScreenRects(float rects[2][4]) {
    return Reader::Screens::Rects(rects);
}

int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (path == nullptr || id == nullptr || id_size == 0) {
        return -1;
    }
    std::string game_id, name;
    if (!Reader::Emu::IdentifyFile(path, game_id, name)) {
        return -1;
    }
    std::snprintf(id, id_size, "%s", game_id.c_str());
    if (title != nullptr && title_size > 0) {
        std::snprintf(title, title_size, "%s", name.c_str());
    }
    return 0;
}

// ---- The program's settings, as melonDS's core options ----

void ApplySetting(const std::string& key, const std::string& value,
                  const std::function<void(const std::string&, const std::string&)>& set) {
    if (key == "nds.layout") {
        set("melonds_screen_layout", value == "side_by_side"  ? "Left/Right"
                                     : value == "top_only"    ? "Top Only"
                                     : value == "bottom_only" ? "Bottom Only"
                                     : value == "hybrid"      ? "Hybrid Top"
                                                              : "Top/Bottom");
    } else if (key == "nds.rotation") {
        set("melonds_reader_rotation", value == "none" || value == "left" || value == "right" ? value : "auto");
    } else if (key == "nds.gap") {
        set("melonds_screen_gap", std::to_string(std::clamp(std::atoi(value.c_str()), 0, 126)));
    } else if (key == "nds.language") {
        set("melonds_language", value == "english" ? "English" : "Japanese");
    } else if (key == "nds.boot_menu") {
        set("melonds_boot_directly", value == "1" ? "disabled" : "enabled");
    }
}

/// Always so here: melonDS's software renderer (the host draws its picture), on a thread of its
/// own, a finger as the DS's touch, a DS (not a DSi), the console's settings from melonDS's
/// own firmware settings (the language below).
const char* const FixedOptions[] = {
    "melonds_opengl_renderer", "disabled", "melonds_threaded_renderer", "enabled",
    "melonds_touch_mode", "Touch", "melonds_console_mode", "DS",
    "melonds_use_fw_settings", "disabled", "melonds_swapscreen_mode", "Hold",
    nullptr, nullptr,
};

/// Until the program's settings say otherwise: a Japanese console, one screen over the other,
/// straight into the game, the picture turned for a game held like a book.
const char* const DefaultOptions[] = {
    "melonds_language", "Japanese", "melonds_screen_layout", "Top/Bottom",
    "melonds_boot_directly", "enabled", "melonds_reader_rotation", "none",
    "melonds_screen_gap", "0", "melonds_audio_interpolation", "Cubic",
    nullptr, nullptr,
};

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
    glue.default_options = DefaultOptions;
    glue.game_loaded = &GameLoaded;
    glue.game_unloading = &GameUnloading;
    glue.screen_rects = &ScreenRects;
    return glue;
}

ReaderLibretro::Glue g_glue;

} // namespace

extern "C" READER_CORE_EXPORT const reader_core_api* READER_CORE_CALL reader_core_get(uint32_t api_version) {
    if (api_version != READER_CORE_API_VERSION) {
        return nullptr;
    }
    if (g_glue.info.struct_size == 0) {
        g_glue = MakeGlue();
    }
    return ReaderLibretro::MakeApi(g_glue);
}
