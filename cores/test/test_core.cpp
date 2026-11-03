// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// A core of a made-up console, for the tests of the program's side of cores/reader_core.h
// (home/tests/cores_test.cpp): what a real core does, without an emulator. Its "game" is
// 1 MB of memory and a thread that presents a frame every few milliseconds:
//
//   frame 2   writes a line of text at 0x1000 (a profile reads it from its frame callback)
//   each frame, when 0x2000 is hooked, "runs" the instruction there: the hook gets the
//             registers, a0 (r1) pointing at a second line at 0x3000
//   each frame, the first controller's buttons (or a pressed button) are kept at 0x4000, the
//             second's at 0x4008; B held on the first sets its rumble going (and stops it)
//
// The settings it is handed are kept at 0x5000 ("key=value\n" each). Since the interface's
// additions of 2026-10-24 it also has everything Furoshiki needs of a core: its save (the folder
// <saves>/TEST-00001, asked for before the game opens it, written at frame 3 and flushed by the
// "emulator" when the game stops, each told), two discs (the one in kept at 0x6000), a pointer on
// either screen (the last kept at 0x7000), ten savestate slots (files in <states>), patches.
// Built as a module of its own with everything hidden but reader_core_get, as a real core is.

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "reader_core.h"

namespace {

constexpr uint64_t MemorySize = 0x100000;
constexpr uint64_t Text = 0x1000, Hooked = 0x2000, HookText = 0x3000, Pad = 0x4000, Settings = 0x5000,
                   Disc = 0x6000, PointerAt = 0x7000;
constexpr const char* GameId = "TEST-00001";

std::vector<uint8_t> memory(MemorySize);
std::mutex memory_mutex;
const reader_core_host* host{};
std::thread game;
std::atomic<bool> stopping{false};
std::atomic<bool> paused{false};
std::atomic<int> pressed{-1};
std::atomic<int> hooks_changed{0};
std::atomic<int> disc{0};
std::atomic<int> disc_wanted{-2}; // -2: no change asked
std::filesystem::path saves_folder, states_folder;

void Put(uint64_t address, const std::string& text) {
    std::scoped_lock lock{memory_mutex};
    std::memcpy(memory.data() + address, text.c_str(), text.size() + 1);
}

// ---- What it says of itself ----

const reader_core_register_name register_names[] = {
    {"r0", 0, READER_CORE_REGISTER_INTEGER, 64}, {"r1", 1, READER_CORE_REGISTER_INTEGER, 64},
    {"a0", 1, READER_CORE_REGISTER_INTEGER, 64}, {"w1", 1, READER_CORE_REGISTER_INTEGER, 32},
    {"r2", 2, READER_CORE_REGISTER_INTEGER, 64}, {"f0", 3, READER_CORE_REGISTER_FLOAT32, 32},
};
const char* const file_types[] = {"test"};
const reader_core_button buttons[] = {{"A", 0}, {"B", 1}, {"Up", 4}, {"Down", 5}, {"Left", 6}, {"Right", 7}};
const char* const speeds[] = {"slow", "fast"};
const reader_core_setting settings[] = {
    {READER_CORE_SETTING_VOLUME, nullptr, READER_CORE_SETTING_NUMBER, "100", 0, 100, 1, nullptr, 0},
    {"test.speed", "Speed", READER_CORE_SETTING_CHOICE, "slow", 0, 0, 0, speeds, 2},
    {READER_CORE_SETTING_CODES, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
    {READER_CORE_SETTING_PATCHES, nullptr, READER_CORE_SETTING_TEXT, "", 0, 0, 0, nullptr, 0},
};
const uint32_t setting_flags[] = {0, READER_CORE_SETTING_AT_START, READER_CORE_SETTING_PER_GAME,
                                  READER_CORE_SETTING_PER_GAME};
const reader_core_device devices[] = {{"Pad", 1, 0b11}, {"Gun", 2, 0b10}};

reader_core_info MakeInfo() {
    reader_core_info info{};
    info.struct_size = sizeof(info);
    info.api_version = READER_CORE_API_VERSION;
    info.key = "test";
    info.name = "Test core";
    info.version = "1";
    info.system = "Test Console";
    info.short_name = "TEST";
    info.app_key = "test-reader";
    info.app_name = "Test Reader";
    info.media_prefix = "test_reader_";
    info.game_id_pattern = "TEST-[0-9]{5}";
    info.game_id_upper = 1;
    info.game_page = "";
    info.script_index_url = "";
    info.script_system = "test";
    info.button_yes = 0;
    info.button_up = 4;
    info.button_down = 5;
    info.button_left = 6;
    info.button_right = 7;
    info.button_press_ms = 50;
    info.pick_step_ms = 100;
    info.screen_width = 640;
    info.screen_height = 480;
    info.lower_screen_width = 320;
    info.file_types = file_types;
    info.file_type_count = 1;
    info.buttons = buttons;
    info.button_count = sizeof(buttons) / sizeof(buttons[0]);
    info.cpu.family = "test";
    info.cpu.value_count = 4;
    info.cpu.names = register_names;
    info.cpu.name_count = sizeof(register_names) / sizeof(register_names[0]);
    info.cpu.hook_alignment = 4;
    info.cpu.highest_address = 0xFFFFFFFF;
    info.settings = settings;
    info.setting_count = sizeof(settings) / sizeof(settings[0]);
    info.features = READER_CORE_HAS_SAVE_SYNC | READER_CORE_HAS_DISCS | READER_CORE_HAS_RUMBLE |
                    READER_CORE_HAS_POINTER | READER_CORE_HAS_STATES | READER_CORE_HAS_PATCHES | READER_CORE_HAS_CODES;
    info.port_count = 2;
    info.devices = devices;
    info.device_count = 2;
    info.pointer_screens = 0b11;
    info.state_slots = 10;
    info.setting_flags = setting_flags;
    return info;
}
const reader_core_info info = MakeInfo();

// ---- The game ----

std::filesystem::path SaveFolder() {
    return saves_folder / GameId;
}

void WriteSave(const char* what, uint32_t kind) {
    std::filesystem::create_directories(SaveFolder());
    std::ofstream(SaveFolder() / "save.bin", std::ios::binary | std::ios::app) << what;
    const std::string path = SaveFolder().string();
    host->saves_written(host->user, GameId, path.c_str(), kind);
}

void Run() {
    host->game_loading(host->user);
    // The save is asked for before the game opens it.
    const std::string folder = SaveFolder().string();
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, GameId,
                                          folder.c_str()};
    const reader_core_saves saves{sizeof(reader_core_saves), GameId, "Test Game", 0, &where, 1};
    host->saves_opening(host->user, &saves);
    host->module_loaded(host->user, "main", 0, MemorySize, "0123ABCD");
    Put(HookText, "フックの行");
    host->game_loaded(host->user, GameId, "Test Game");
    bool rumbling = false;
    for (uint64_t frame = 1; !stopping; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        if (paused) {
            continue;
        }
        if (frame == 2) {
            Put(Text, "テストの行");
        }
        if (frame == 3) {
            WriteSave("saved by the game\n", READER_CORE_WRITE_GAME);
        }
        if (const int wanted = disc_wanted.exchange(-2); wanted != -2) {
            disc = wanted;
            {
                std::scoped_lock lock{memory_mutex};
                std::memcpy(memory.data() + Disc, &wanted, sizeof(wanted));
            }
            host->disc_changed(host->user, wanted, 0);
        }
        reader_core_pad pad{};
        host->pad(host->user, 0, &pad);
        uint64_t held = pad.buttons;
        if (pressed >= 0) {
            held |= 1ull << pressed;
        }
        reader_core_pad second{};
        host->pad(host->user, 1, &second);
        {
            std::scoped_lock lock{memory_mutex};
            std::memcpy(memory.data() + Pad, &held, sizeof(held));
            std::memcpy(memory.data() + Pad + 8, &second.buttons, sizeof(second.buttons));
        }
        if (const bool b = (pad.buttons & 2) != 0; b != rumbling) {
            rumbling = b;
            host->rumble(host->user, 0, b ? 65535 : 0, b ? 32768 : 0);
        }
        if (host->is_hooked(host->user, Hooked) != 0) {
            float half = 0.5f;
            uint32_t half_bits;
            std::memcpy(&half_bits, &half, sizeof(half));
            const uint64_t values[4] = {7, HookText | 0xABCD00000000ull, 9, half_bits};
            const reader_core_registers registers{Hooked, values, 4};
            host->hook(host->user, &registers);
        }
        host->frame(host->user, 640, 480);
    }
    // What an emulator writes of its own at the end (a flush, a checksum) is told too, and
    // before game_stopped.
    WriteSave("flushed by the emulator\n", READER_CORE_WRITE_EMULATOR);
    host->game_stopped(host->user);
}

// ---- Its functions ----

const reader_core_info* Info() {
    return &info;
}

int32_t Init(const reader_core_host* given, const reader_core_paths* paths) {
    if (given == nullptr || given->struct_size < sizeof(reader_core_host)) {
        return 1;
    }
    host = given;
    if (paths != nullptr && paths->saves != nullptr && paths->states != nullptr) {
        saves_folder = paths->saves;
        states_folder = paths->states;
    }
    return 0;
}

void Stop() {
    stopping = true;
    if (game.joinable()) {
        game.join();
    }
}

void Shutdown() {
    Stop();
}

int32_t SetWindow(const reader_core_window*) {
    return 0;
}

void ApplySettings(const reader_core_setting_value* values, uint32_t count) {
    std::string text;
    for (uint32_t i = 0; i < count; ++i) {
        text += std::string(values[i].key) + "=" + values[i].value + "\n";
    }
    Put(Settings, text);
}

int32_t LoadGame(const char* game_file) {
    if (host == nullptr || game_file == nullptr || std::strcmp(game_file, "missing") == 0) {
        return 1;
    }
    Stop();
    {
        std::scoped_lock lock{memory_mutex};
        std::fill(memory.begin(), memory.begin() + Settings, 0);
    }
    stopping = false;
    game = std::thread(Run);
    return 0;
}

void Pause(int32_t on) {
    paused = on != 0;
}

std::filesystem::path StateFile(int32_t slot) {
    return states_folder / (std::string(GameId) + "." + std::to_string(slot) + ".state");
}

int32_t SaveState(int32_t slot) {
    if (slot < 1 || slot > 10 || !game.joinable()) {
        return 1;
    }
    std::filesystem::create_directories(states_folder);
    {
        std::scoped_lock lock{memory_mutex};
        std::ofstream(StateFile(slot), std::ios::binary).write(reinterpret_cast<const char*>(memory.data()), Settings);
    }
    host->state_done(host->user, 0, slot, 0, "");
    return 0;
}

int32_t LoadState(int32_t slot) {
    if (slot < 1 || slot > 10 || !game.joinable()) {
        return 1;
    }
    std::ifstream in(StateFile(slot), std::ios::binary);
    if (!in) {
        host->state_done(host->user, 1, slot, 1, "nothing in that slot");
        return 0;
    }
    {
        std::scoped_lock lock{memory_mutex};
        in.read(reinterpret_cast<char*>(memory.data()), Settings);
    }
    host->state_loaded(host->user);
    host->state_done(host->user, 1, slot, 0, "");
    return 0;
}

int32_t StateInfo(int32_t slot, reader_core_state_info* out) {
    if (out == nullptr || slot < 1 || slot > 10) {
        return 1;
    }
    std::error_code error;
    const auto when = std::filesystem::last_write_time(StateFile(slot), error);
    out->exists = error ? 0 : 1;
    out->saved_ms = error ? 0
                          : std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::clock_cast<std::chrono::system_clock>(when).time_since_epoch())
                                .count();
    return 0;
}

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    if (game_id == nullptr || std::strcmp(game_id, GameId) != 0) {
        return -1;
    }
    const std::string folder = SaveFolder().string();
    const reader_core_save_location where{sizeof(reader_core_save_location), READER_CORE_SAVE_FOLDER, GameId,
                                          folder.c_str()};
    each(context, &where);
    return 1;
}

uint32_t DiscCount() {
    return 2;
}

int32_t DiscCurrent() {
    return disc;
}

int32_t DiscLabel(uint32_t index, char* label, size_t size) {
    if (index >= 2 || size == 0) {
        return 1;
    }
    std::snprintf(label, size, "Disc %u", index + 1);
    return 0;
}

int32_t DiscInsert(int32_t index) {
    if (index < -1 || index >= 2) {
        return 1;
    }
    disc_wanted = index;
    return 0;
}

int32_t SetPortDevice(uint32_t port, uint32_t device) {
    return (device == 1 && port < 2) || (device == 2 && port == 1) ? 0 : 1;
}

void Pointer(const reader_core_pointer* pointer) {
    if (pointer == nullptr) {
        return;
    }
    const int32_t values[4] = {static_cast<int32_t>(pointer->screen), static_cast<int32_t>(pointer->flags),
                               static_cast<int32_t>(pointer->x * 1000), static_cast<int32_t>(pointer->y * 1000)};
    std::scoped_lock lock{memory_mutex};
    std::memcpy(memory.data() + PointerAt, values, sizeof(values));
}

int32_t Patches(const char* game_id, reader_core_patch_fn each, void* context) {
    if (game_id == nullptr || std::strcmp(game_id, GameId) != 0) {
        return 0;
    }
    const reader_core_patch patches[] = {{sizeof(reader_core_patch), 1, "widescreen", "The picture made wide"},
                                         {sizeof(reader_core_patch), 0, "no-blur", "No blur"}};
    for (const reader_core_patch& patch : patches) {
        each(context, &patch);
    }
    return 2;
}

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    if (address >= MemorySize) {
        return 0;
    }
    const size_t valid = std::min<uint64_t>(size, MemorySize - address);
    std::scoped_lock lock{memory_mutex};
    std::memcpy(buffer, memory.data() + address, valid);
    return valid;
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    if (address >= MemorySize || size > MemorySize - address) {
        return 1;
    }
    std::scoped_lock lock{memory_mutex};
    std::memcpy(memory.data() + address, buffer, size);
    return 0;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    if (capacity >= 1) {
        regions[0] = reader_core_region{0, MemorySize, READER_CORE_REGION_READ | READER_CORE_REGION_WRITE, 2};
    }
    return 1;
}

void HooksChanged() {
    ++hooks_changed;
}

int32_t RunningGameCode() {
    return 1;
}

void PressButton(int32_t bit, int32_t down) {
    pressed = down != 0 ? bit : -1;
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
    api.save_state = &SaveState;
    api.load_state = &LoadState;
    api.read_memory = &ReadMemory;
    api.write_memory = &WriteMemory;
    api.memory_regions = &MemoryRegions;
    api.hooks_changed = &HooksChanged;
    api.running_game_code = &RunningGameCode;
    api.press_button = &PressButton;
    api.save_locations = &SaveLocations;
    api.disc_count = &DiscCount;
    api.disc_current = &DiscCurrent;
    api.disc_label = &DiscLabel;
    api.disc_insert = &DiscInsert;
    api.set_port_device = &SetPortDevice;
    api.pointer = &Pointer;
    api.state_info = &StateInfo;
    api.patches = &Patches;
    return api;
}
const reader_core_api api = MakeApi();

} // namespace

extern "C" READER_CORE_EXPORT const reader_core_api* READER_CORE_CALL reader_core_get(uint32_t api_version) {
    return api_version == READER_CORE_API_VERSION ? &api : nullptr;
}
