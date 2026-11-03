// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The one file that ties the text engine to the emulator (DuckStation's core).

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/cd_image.h"
#include "core/bus.h"
#include "core/controller.h"
#include "core/cpu_core.h"
#include "core/host_display.h"
#include "core/host_interface.h"
#include "core/pad.h"
#include "core/system.h"

#include "reader/core/audio_tap.h"
#include "reader/core/crt.h"
#include "reader/core/disc_sets.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#include "reader/core/registers.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif

namespace Reader::Emu {

namespace {

namespace fs = std::filesystem;

// The console's main memory (2 MB) is seen at 0x00000000, 0x80000000 and 0xA0000000 (a program
// runs in the second); the scratchpad, 1 KB of fast memory, at 0x1F800000 and 0x9F800000.
constexpr u32 SegmentMask = 0x1FFFFFFF;
constexpr u32 ScratchpadBase = 0x1F800000;

bool InKuseg(u32 address) {
    // kuseg, kseg0, kseg1: what a PlayStation program addresses its memory through.
    return address < 0x80000000u || (address >= 0x80000000u && address < 0xC0000000u);
}

class PlayStationHost final : public Host {
public:
    std::string ReadMemory(u64 address, std::size_t size) override {
        if (address > 0xFFFFFFFFULL || size == 0 || Bus::g_ram == nullptr ||
            !InKuseg(static_cast<u32>(address))) {
            return {};
        }
        const u32 physical = static_cast<u32>(address) & SegmentMask;
        if (physical < Bus::g_ram_size) {
            const std::size_t valid = std::min<std::size_t>(size, Bus::g_ram_size - physical);
            return std::string(reinterpret_cast<const char*>(Bus::g_ram + physical), valid);
        }
        if (physical >= ScratchpadBase && physical < ScratchpadBase + CPU::DCACHE_SIZE) {
            const u32 offset = physical - ScratchpadBase;
            const std::size_t valid = std::min<std::size_t>(size, CPU::DCACHE_SIZE - offset);
            return std::string(reinterpret_cast<const char*>(CPU::g_state.dcache.data() + offset), valid);
        }
        return {};
    }

    bool WriteMemory(u64 address, const std::string& bytes) override {
        if (address > 0xFFFFFFFFULL || Bus::g_ram == nullptr || !InKuseg(static_cast<u32>(address))) {
            return false;
        }
        // Only the main memory and the scratchpad, as ReadMemory reads them, and all of it or
        // nothing: never the console's registers (0x1F801000 on), never a part of the bytes.
        const u32 physical = static_cast<u32>(address) & SegmentMask;
        const bool in_ram = physical < Bus::g_ram_size && bytes.size() <= Bus::g_ram_size - physical;
        const bool in_scratchpad = physical >= ScratchpadBase && physical - ScratchpadBase < CPU::DCACHE_SIZE &&
                                   bytes.size() <= CPU::DCACHE_SIZE - (physical - ScratchpadBase);
        if (!in_ram && !in_scratchpad) {
            return false;
        }
        // Through the CPU's own way of writing, which drops code translated from what changes.
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (!CPU::SafeWriteMemoryByte(static_cast<u32>(address + i), static_cast<u8>(bytes[i]))) {
                return false;
            }
        }
        return true;
    }

    void InvalidateCode() override {
        // The interpreter runs while there are hooks (cpu_core.cpp asks AnyHooks); the change
        // of dispatcher is made by the CPU itself at its next frame.
        any_hooks.store(Engine::Instance().HookCount() != 0, std::memory_order_relaxed);
        if (System::IsValid()) {
            CPU::UpdateDebugDispatcherFlag();
        }
    }

    std::vector<MemoryRegion> MemoryRegions() override {
        if (Bus::g_ram == nullptr) {
            return {};
        }
        return {
            // The kernel's own area, then the rest of the main memory, then the scratchpad.
            MemoryRegion{0x80000000, 0x00010000, true, true, false, 1},
            MemoryRegion{0x80010000, Bus::g_ram_size - 0x00010000, true, true, true, 2},
            MemoryRegion{0x1F800000, CPU::DCACHE_SIZE, true, true, false, 3},
        };
    }

    std::atomic<bool> any_hooks{false};
};

PlayStationHost g_host;
std::mutex g_mutex;
std::function<void(const std::string&)> g_game_loaded;
std::function<void()> g_game_stopped;
std::function<void(const std::string&)> g_menu;
std::function<bool(const std::string&)> g_profile_check;
std::function<void(std::function<void()>)> g_press_poster;
std::string g_title;
std::string g_game;
std::string g_data_directory;
bool g_running = false;
std::atomic<bool> g_installed{false};
/// The picture's size the engine was last told of (OnFrame).
s32 g_picture_width = 0;
s32 g_picture_height = 0;

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
                                      static_cast<u32>(data[at + 2]) << 16 |
                                      static_cast<u32>(data[at + 3]) << 24;
}

/// A game and its boot program, as the reader is told of it.
struct StartingGame {
    std::string game_id;
    std::string title;
    u32 base = 0x80010000;
    u32 size = 0;
    std::string build = "00000000";
};

/// A game heard of before the console's memory was made (a boot names its disc first, then
/// makes the memory), started on the first frame: a profile's hooks need that memory.
std::optional<StartingGame> g_pending_game;

/// Tells the engine of the game, which loads its profile.
void StartGame(const StartingGame& game) {
    Engine& engine = Engine::Instance();
    engine.OnGameLoading(g_host);
    engine.OnModuleLoaded("main", game.base, game.size, game.build);
    const std::string& game_id = game.game_id;
    const std::string& title = game.title;
    engine.OnGameLoaded(game_id);
    engine.OnPicture(320, 240); // until the first frame says otherwise (OnFrame)
    g_picture_width = 0;
    g_picture_height = 0;
    g_host.any_hooks.store(engine.HookCount() != 0, std::memory_order_relaxed);
    std::function<void(const std::string&)> callback;
    {
        std::scoped_lock lock{g_mutex};
        g_title = title;
        g_game = game_id;
        g_running = true;
        callback = g_game_loaded;
    }
    if (callback && !game_id.empty()) {
        callback(game_id);
    }
}

/// The disc in the drive is the game from here on. Its ID is the serial its boot file names
/// ("SLPS-01794"), and what a profile binds itself to (reader.build_id) is a checksum of the
/// boot program as the disc has it, PS-X EXE header and all.
void GameStarted(const std::string& game_id, const std::string& title, CDImage* image) {
    StartingGame game{game_id, title};
    std::string executable_name;
    std::vector<u8> executable;
    if (image != nullptr && System::ReadExecutableFromImage(image, &executable_name, &executable) &&
        executable.size() > 0x800) {
        char build[16];
        std::snprintf(build, sizeof(build), "%08X", Crc32(executable.data(), executable.size()));
        game.build = build;
        // The header says where the program goes and how long it is.
        game.base = Le32(executable, 0x18);
        game.size = Le32(executable, 0x1C);
    }
    if (Bus::g_ram == nullptr) {
        g_pending_game = std::move(game);
        return;
    }
    g_pending_game.reset();
    StartGame(game);
}

/// The game whose card save sync was told of, so that it is told once per start.
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
    g_pending_game.reset();
    std::function<void()> callback;
    {
        std::scoped_lock lock{g_mutex};
        if (!g_running) {
            return;
        }
        g_running = false;
        g_title.clear();
        g_game.clear();
        callback = g_game_stopped;
    }
    Engine::Instance().OnShutdown();
    g_host.any_hooks.store(false, std::memory_order_relaxed);
    InputFilter::Instance().HoldSticks(false);
    if (callback) {
        callback();
    }
}

fs::path FromUtf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string DataPath(const std::string& relative) {
    std::scoped_lock lock{g_mutex};
    if (g_data_directory.empty()) {
        return {};
    }
    return g_data_directory + "/" + relative;
}

} // Anonymous namespace

void Install() {
    if (g_installed.exchange(true)) {
        return;
    }
    // The console's picture has 240 lines (480 when interlaced): what the CRT picture's
    // "Lines: as the console" means.
    Crt::SetNativeLines(240);
    EnsureSaveSync();
}

void SetDataDirectory(const std::string& directory) {
    {
        std::scoped_lock lock{g_mutex};
        g_data_directory = directory;
    }
    EnsureSaveSync();
}

void OnRunningGameChanged(const std::string& code, const std::string& title, void* image) {
    std::string running;
    {
        std::scoped_lock lock{g_mutex};
        running = g_running ? g_game : std::string{};
    }
    if (!running.empty() && running == code) {
        return;
    }
    GameStopped();
    if (!code.empty()) {
        GameStarted(code, title, static_cast<CDImage*>(image));
    }
}

void OnShutdown() {
    GameStopped();
}

void OnFrame() {
    if (g_pending_game && Bus::g_ram != nullptr) {
        const StartingGame game = std::move(*g_pending_game);
        g_pending_game.reset();
        StartGame(game);
    }
    // The picture's size as the console shows it now (320 by 224, 240, 480 and so on): what a
    // profile's places are in (reader.picture()), told when it changes.
    if (const HostDisplay* display = g_host_interface ? g_host_interface->GetDisplay() : nullptr) {
        const s32 width = display->GetDisplayWidth();
        const s32 height = display->GetDisplayHeight();
        if (width > 0 && height > 0 && (width != g_picture_width || height != g_picture_height)) {
            g_picture_width = width;
            g_picture_height = height;
            Engine::Instance().OnPicture(static_cast<u32>(width), static_cast<u32>(height));
        }
    }
    Engine::Instance().OnFrame();
}

void OnStateLoaded() {
    Engine::Instance().OnStateLoaded();
}

bool AnyHooks() {
    return g_host.any_hooks.load(std::memory_order_relaxed);
}

void HookAt(uint32_t pc) {
    Engine& engine = Engine::Instance();
    if (!engine.IsHooked(pc)) {
        return;
    }
    GuestRegisters regs;
    for (std::size_t i = 0; i < 32; ++i) {
        regs.r[i] = CPU::g_state.regs.r[i];
    }
    // A load the instruction before began reaches its register after this one: the function
    // a hook is at sees it from its next instruction on, and so does the profile.
    if (CPU::g_state.load_delay_reg != CPU::Reg::count) {
        regs.r[static_cast<std::size_t>(CPU::g_state.load_delay_reg)] = CPU::g_state.load_delay_value;
    }
    regs.pc = pc;
    regs.hi = CPU::g_state.regs.hi;
    regs.lo = CPU::g_state.regs.lo;
    engine.Dispatch(regs);
}

bool EnsureSaveSync() {
#ifdef READER_NO_SAVE_SYNC
    return false; // a published build has no save sync
#else
    if (SaveSync::Host::Started()) {
        return true;
    }
    // Not before the app knows where it keeps its files.
    const std::string saves = DataPath("saves");
    if (saves.empty()) {
        return false;
    }
    // Every game's card is in a folder of its own under "saves".
    std::error_code error;
    fs::create_directories(FromUtf8(saves), error);
    SaveSync::Host::Start(saves, DataPath("reader-savesync"), [](const std::string&) {},
                          DeviceName());
    return SaveSync::Host::Started();
#endif
}

void BeforeMemoryCard(int slot, const std::string& path) {
#ifndef READER_NO_SAVE_SYNC
    std::string game_id;
    std::string title;
    {
        std::scoped_lock lock{g_mutex};
        game_id = g_game;
        title = g_title;
    }
    // A game booted from nothing is still waiting for the console's memory (GameStarted) when
    // its cards are put in: it is that game's card all the same. Same thread as GameStarted.
    if (game_id.empty() && g_pending_game) {
        game_id = g_pending_game->game_id;
        title = g_pending_game->title;
    }
    // Every disc of a game on several discs has the first disc's card, synced under its serial.
    game_id = CardSerialOf(game_id);
    // Only the game's own card, in its own folder, is synced.
    if (slot != 0 || game_id.empty() || game_id == g_synced_game || path.empty() ||
        path != MemoryCardPathOf(game_id) || !EnsureSaveSync()) {
        return;
    }
    g_synced_game = game_id;
    SaveSync::Host::GameStarting(game_id, title);
#else
    (void)slot;
    (void)path;
#endif
}

void OnMemoryCardWritten(const std::string& path) {
#ifndef READER_NO_SAVE_SYNC
    if (!g_synced_game.empty() && path == MemoryCardPathOf(g_synced_game)) {
        SaveSync::Host::NoteSaved();
    }
#else
    (void)path;
#endif
}

std::string MemoryCardPathOf(const std::string& game_id) {
#ifdef READER_NO_SAVE_SYNC
    (void)game_id;
    return {}; // without save sync each card is where the emulator keeps it
#else
    if (SaveSync::Host::TitleOf(game_id).empty()) {
        return {};
    }
    const std::string folder = DataPath("saves/" + game_id);
    if (folder.empty()) {
        return {};
    }
    std::error_code error;
    fs::create_directories(FromUtf8(folder), error);
    return folder + "/" + game_id + "_1.mcd";
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

void SetPressPoster(std::function<void(std::function<void()> press)> poster) {
    std::scoped_lock lock{g_mutex};
    g_press_poster = std::move(poster);
}

void PressButton(int bit, bool down) {
    if (bit < 0 || bit > 15) {
        return;
    }
    // Asked where the press is made: with a poster that is the emulation thread, which alone
    // destroys the controllers, so the check holds while the press is made.
    auto press = [bit, down]() {
        if (!System::IsValid()) {
            return;
        }
        if (Controller* controller = g_pad.GetController(0)) {
            controller->SetButtonState(bit, down);
        }
    };
    std::function<void(std::function<void()>)> poster;
    {
        std::scoped_lock lock{g_mutex};
        poster = g_press_poster;
    }
    if (poster) {
        poster(std::move(press));
    } else {
        press();
    }
}

void RestSticks() {
    // A digital controller has no sticks; an analog one is put at rest by its own axes, which
    // the reader does not take on this console.
}

bool FilterStick(int stick, float x, float y) {
    return InputFilter::Instance().OnStick(static_cast<std::size_t>(stick), x, y);
}

bool DumpMemory(const std::string& path) {
    if (Bus::g_ram == nullptr) {
        return false;
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(Bus::g_ram, 1, Bus::g_ram_size, file) == Bus::g_ram_size;
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
