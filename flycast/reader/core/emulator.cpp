// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The one file that ties the text engine to the emulator.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "types.h"
#include "cfg/option.h"
#include "emulator.h"
#include "hw/aica/aica_if.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/sh4/sh4_mem.h"
#include "input/gamepad.h"
#include "input/gamepad_device.h"
#include "oslib/oslib.h"
#include "reios/reios.h"
#include "stdclass.h"

#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif

namespace Reader::Emu {

namespace {

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
/// Where the console puts a disc's program, as the program itself sees it.
constexpr u32 ProgramBase = 0x8C010000;

std::string Slice(RamRegion& region, u32 size_of_region, u32 offset, std::size_t size) {
    const u8* data = &region[0];
    if (data == nullptr || size_of_region == 0) {
        return {};
    }
    offset %= size_of_region;
    const std::size_t valid = std::min<std::size_t>(size, size_of_region - offset);
    return std::string(reinterpret_cast<const char*>(data + offset), valid);
}

class DreamcastHost final : public Host {
public:
    std::string ReadMemory(u64 address, std::size_t size) override {
        if (address > 0xFFFFFFFFULL || size == 0 || address >= 0xE0000000ULL) {
            return {};
        }
        u32 start = static_cast<u32>(address) & AreaMask;
        if (start >= MainBase && start < MainEnd) {
            return Slice(mem_b, RAM_SIZE, start - MainBase, size);
        }
        // 0x06000000-0x07FFFFFF mirrors the video memory's two views below.
        if (start >= VideoEnd && start < VideoEnd + (VideoEnd - VideoBase)) {
            start -= VideoEnd - VideoBase;
        }
        if (start >= VideoBase && start < Video32Base) {
            // The 64-bit view: the video memory as the emulator keeps it.
            return Slice(vram, VRAM_SIZE, start - VideoBase, size);
        }
        if (start >= Video32Base && start < VideoEnd && &vram[0] != nullptr) {
            // The 32-bit view, where the two banks are interleaved every four bytes: each byte
            // through the emulator's own mapping of it (hw/pvr/pvr_mem.cpp).
            const std::size_t valid = std::min<std::size_t>(size, VideoEnd - start);
            std::string out(valid, '\0');
            for (std::size_t i = 0; i < valid; ++i) {
                out[i] = static_cast<char>(pvr_read32p<u8>(static_cast<u32>(start + i)));
            }
            return out;
        }
        if (start >= SoundBase && start < SoundEnd) {
            return Slice(aica::aica_ram, ARAM_SIZE, start - SoundBase, size);
        }
        return {};
    }

    void InvalidateCode() override {
        // Hooks in the game's code are not carried out by this emulator's recompiler yet: a
        // profile here reads memory once a frame.
    }

    std::vector<MemoryRegion> MemoryRegions() override {
        if (&mem_b[0] == nullptr) {
            return {};
        }
        return {
            MemoryRegion{0x8C000000, 0x00010000, true, true, false, 1},
            MemoryRegion{ProgramBase, RAM_SIZE - 0x00010000, true, true, true, 2},
            MemoryRegion{0xA5000000, VRAM_SIZE, true, true, false, 3},
        };
    }
};

DreamcastHost g_host;
std::mutex g_mutex;
std::function<void(const std::string&)> g_game_loaded;
std::function<void()> g_game_stopped;
std::function<void(const std::string&)> g_menu;
std::function<bool(const std::string&)> g_profile_check;
std::string g_title;
bool g_running = false;
std::atomic<bool> g_installed{false};

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

std::string Trimmed(const char* field, std::size_t size) {
    std::string text(field, strnlen(field, size));
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) {
        text.pop_back();
    }
    return text;
}

/// The disc in the drive is the game from here on. Its ID is the product number of its
/// header ("HDR-0164"), and what a profile binds itself to (reader.build_id) is a checksum of
/// that whole header: product number, version, date, which disc of how many, boot file.
void GameStarted() {
    const std::string game_id = settings.content.gameId;
    Engine& engine = Engine::Instance();
    engine.OnGameLoading(g_host);
    char build[16];
    std::snprintf(build, sizeof(build), "%08X",
                  Crc32(reinterpret_cast<const u8*>(&ip_meta), sizeof(ip_meta)));
    engine.OnModuleLoaded("main", ProgramBase, RAM_SIZE - 0x00010000, build);
    engine.OnGameLoaded(game_id);
    engine.OnPicture(640, 480);
    std::function<void(const std::string&)> callback;
    {
        std::scoped_lock lock{g_mutex};
        g_title = Trimmed(ip_meta.software_name, sizeof(ip_meta.software_name));
        g_running = true;
        callback = g_game_loaded;
    }
    if (callback && !game_id.empty()) {
        callback(game_id);
    }
}

/// The game whose memory unit save sync was told of, so that it is told once per start.
std::string g_synced_game;

std::string DeviceName() {
#if defined(__ANDROID__)
    return "Android";
#else
    const char* name = std::getenv("COMPUTERNAME");
    if (name == nullptr || *name == '\0') {
        name = std::getenv("HOSTNAME");
    }
    if (name != nullptr && *name != '\0') {
        return name;
    }
#if !defined(_WIN32)
    if (std::FILE* file = std::fopen("/etc/hostname", "r")) {
        char text[128] = {};
        const bool read = std::fgets(text, sizeof(text), file) != nullptr;
        std::fclose(file);
        std::string host = read ? text : "";
        while (!host.empty() && (host.back() == '\n' || host.back() == ' ')) {
            host.pop_back();
        }
        if (!host.empty()) {
            return host;
        }
    }
#endif
    return "Computer";
#endif
}

void GameStopped() {
#ifndef READER_NO_SAVE_SYNC
    SaveSync::Host::GameStopped();
#endif
    g_synced_game.clear();
    std::function<void()> callback;
    {
        std::scoped_lock lock{g_mutex};
        if (!g_running) {
            return;
        }
        g_running = false;
        g_title.clear();
        callback = g_game_stopped;
    }
    Engine::Instance().OnShutdown();
    InputFilter::Instance().HoldSticks(false);
    if (callback) {
        callback();
    }
}

void OnEvent(Event event, void*) {
    switch (event) {
    case Event::Start:
        if (settings.platform.isConsole()) {
            GameStarted();
        }
        break;
    case Event::DiskChange:
        // The next disc of a game on several: another program, with a header of its own.
        if (settings.platform.isConsole()) {
            GameStopped();
            GameStarted();
        }
        break;
    case Event::Terminate:
        GameStopped();
        break;
    case Event::LoadState:
        Engine::Instance().OnStateLoaded();
        break;
    case Event::VBlank:
        Engine::Instance().OnFrame();
        break;
    default:
        break;
    }
}

} // Anonymous namespace

void Install() {
    if (g_installed.exchange(true)) {
        return;
    }
    for (const Event event : {Event::Start, Event::DiskChange, Event::Terminate, Event::LoadState,
                              Event::VBlank}) {
        EventManager::listen(event, OnEvent);
    }
    EnsureSaveSync();
}

bool EnsureSaveSync() {
#ifdef READER_NO_SAVE_SYNC
    return false; // a published build has no save sync
#else
    if (SaveSync::Host::Started()) {
        return true;
    }
    // Not before the app knows where it keeps its files (a first start on Android asks).
    if (get_writable_data_path("").empty()) {
        return false;
    }
    // Every game's memory unit is in a folder of its own under "saves".
    const std::string saves = get_writable_data_path("saves");
    make_directory(saves);
    SaveSync::Host::Start(
        saves, get_writable_data_path("reader-savesync"),
        [](const std::string& text) { os_notify("Save sync", 6000, text.c_str()); }, DeviceName());
    return SaveSync::Host::Started();
#endif
}

void BeforeMemoryUnit(const std::string& port) {
#ifndef READER_NO_SAVE_SYNC
    if (port != "A1" || !config::PerGameVmu || !settings.platform.isConsole()) {
        return;
    }
    const std::string game_id = settings.content.gameId;
    if (game_id.empty() || game_id == g_synced_game || SaveSync::Host::TitleOf(game_id).empty() ||
        !EnsureSaveSync()) {
        return;
    }
    g_synced_game = game_id;
    SaveSync::Host::GameStarting(game_id,
                                 Trimmed(ip_meta.software_name, sizeof(ip_meta.software_name)));
#endif
}

void OnSaveWritten(const std::string& port) {
#ifndef READER_NO_SAVE_SYNC
    if (port == "A1" && !g_synced_game.empty()) {
        SaveSync::Host::NoteSaved();
    }
#endif
}

std::string SaveFolderOf(const std::string& game_id) {
#ifdef READER_NO_SAVE_SYNC
    return {}; // without save sync each memory unit is where Flycast keeps it
#else
    if (SaveSync::Host::TitleOf(game_id).empty()) {
        return {};
    }
    const std::string folder = get_writable_data_path("saves/" + game_id);
    make_directory(get_writable_data_path("saves"));
    make_directory(folder);
    return folder;
#endif
}

void SetGameLoadedCallback(std::function<void(const std::string& game_id)> callback) {
    std::scoped_lock lock{g_mutex};
    g_game_loaded = std::move(callback);
}

void SetGameStoppedCallback(std::function<void()> callback) {
    std::scoped_lock lock{g_mutex};
    g_game_stopped = std::move(callback);
}

void SetMenuCallback(std::function<void(const std::string& what)> callback) {
    std::scoped_lock lock{g_mutex};
    g_menu = std::move(callback);
}

void OpenMenu(const std::string& what) {
    std::function<void(const std::string&)> callback;
    {
        std::scoped_lock lock{g_mutex};
        callback = g_menu;
    }
    if (callback) {
        callback(what);
    }
}

void SetProfileCheck(std::function<bool(const std::string&)> check) {
    std::scoped_lock lock{g_mutex};
    g_profile_check = std::move(check);
}

bool HasProfileCheck() {
    std::scoped_lock lock{g_mutex};
    return static_cast<bool>(g_profile_check);
}

bool HasProfile(const std::string& game_id) {
    std::function<bool(const std::string&)> check;
    {
        std::scoped_lock lock{g_mutex};
        check = g_profile_check;
    }
    return check && check(game_id);
}

std::string GameTitle() {
    std::scoped_lock lock{g_mutex};
    return g_title;
}

void OnAudio(const int16_t* samples, std::size_t frames) {
    AudioTap& tap = AudioTap::Instance();
    if (tap.IsEnabled()) {
        tap.Push(samples, frames);
    }
}

bool FilterKey(int device, int key_code, bool down) {
    if (key_code < 0) {
        return false;
    }
    // The device stands for "the controller" the filter remembers a passed-on press by.
    return InputFilter::Instance().OnButton(
        reinterpret_cast<const void*>(static_cast<std::intptr_t>(device) + 1),
        static_cast<std::size_t>(key_code), down);
}

void PressButton(int bit, bool down) {
    if (bit < 0 || bit > 15) {
        return;
    }
    // The console's button word has a 0 for a button that is down.
    const u32 mask = 1u << bit;
    if (down) {
        kcode[0] &= ~mask;
    } else {
        kcode[0] |= mask;
    }
}

void PressTrigger(int trigger, bool down) {
    (trigger == 0 ? lt : rt)[0] = down ? 0xFFFF : 0;
}

void RestSticks() {
    joyx[0] = 0;
    joyy[0] = 0;
}

void MoveStick(int x, int y) {
    joyx[0] = static_cast<s16>(std::clamp(x, -100, 100) * 327);
    joyy[0] = static_cast<s16>(std::clamp(y, -100, 100) * 327);
}

bool FilterStick(int stick, float x, float y) {
    return InputFilter::Instance().OnStick(static_cast<std::size_t>(stick), x, y);
}

bool DumpMemory(const std::string& path) {
    if (&mem_b[0] == nullptr) {
        return false;
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(&mem_b[0], 1, RAM_SIZE, file) == RAM_SIZE;
    std::fclose(file);
    return ok;
}

#ifndef READER_NO_SAVE_SYNC
void ConfigureSaveSync(const SaveSync::Host::Settings& settings) {
    if (EnsureSaveSync()) {
        SaveSync::Host::Set(settings);
    }
}
#endif

void ToggleNextChoice() {}

bool GoingToNextChoice() {
    return false;
}

void SetNextChoiceCallback(std::function<void(bool, int, std::uint64_t)>) {}

} // namespace Reader::Emu
