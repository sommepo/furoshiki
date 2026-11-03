// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The reader in the Android app: what ties the shared engine to Vita3K, which a core leaves to
// Furoshiki (reader/core_library/vita3k_core.cpp answers the same questions there). The frontend
// calls in at "Reader fork" comments (reader/core/emulator.h).

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include <app/session_controller.h>
#include <config/state.h>
#include <ctrl/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <mem/state.h>
#include <util/log.h>

#include <filesystem>
#include <map>
#include "reader/save_sync/host.h"
#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"

namespace Reader::Emu {
namespace {

EmuEnvState *g_emu = nullptr;
std::shared_mutex g_memory_mutex;
bool g_memory_up = false;
bool g_loading = false;
std::atomic<std::uint32_t> g_pressed{ 0 };
std::mutex g_callback_mutex;
std::function<void(const std::string &)> g_game_loaded;
std::function<void()> g_game_stopped;
std::function<void(const std::string &)> g_menu;

constexpr std::uint32_t BitL = 1u << 8;
constexpr std::uint32_t BitR = 1u << 9;
constexpr std::size_t Page = 4096;

class GameMemory final : public Reader::Host {
public:
    std::string ReadMemory(u64 address, std::size_t size) override {
        std::string out;
        std::shared_lock lock{ g_memory_mutex };
        if (!g_memory_up || address > 0xFFFFFFFFull)
            return out;
        const MemState &mem = g_emu->mem;
        out.reserve(std::min<std::size_t>(size, Page));
        while (out.size() < size) {
            const u64 at = address + out.size();
            if (at > 0xFFFFFFFFull || !is_valid_addr(mem, static_cast<Address>(at)))
                break;
            const std::size_t in_page = Page - (at & (Page - 1));
            const std::size_t take = std::min(in_page, size - out.size());
            out.append(reinterpret_cast<const char *>(Ptr<uint8_t>(static_cast<Address>(at)).get(mem)), take);
        }
        return out;
    }

    void InvalidateCode() override {
        std::shared_lock lock{ g_memory_mutex };
        if (g_memory_up)
            g_emu->kernel.invalidate_jit_cache(0, 0xFFFFFFFFu);
    }

    std::vector<Reader::MemoryRegion> MemoryRegions() override {
        std::vector<Reader::MemoryRegion> regions;
        std::shared_lock lock{ g_memory_mutex };
        if (!g_memory_up)
            return regions;
        const MemState &mem = g_emu->mem;
        constexpr u64 End = 0x100000000ull;
        u64 start = 0;
        bool open = false;
        for (u64 page = 0; page < End; page += Page) {
            const bool valid = is_valid_addr(mem, static_cast<Address>(page));
            if (valid && !open) {
                start = page;
                open = true;
            } else if (!valid && open) {
                regions.push_back(Reader::MemoryRegion{ start, page - start });
                open = false;
            }
        }
        if (open)
            regions.push_back(Reader::MemoryRegion{ start, End - start });
        return regions;
    }

    bool WriteMemory(u64 address, const std::string &bytes) override {
        std::shared_lock lock{ g_memory_mutex };
        if (!g_memory_up || address + bytes.size() > 0x100000000ull)
            return false;
        const MemState &mem = g_emu->mem;
        for (std::size_t done = 0; done < bytes.size(); done += Page - ((address + done) & (Page - 1))) {
            if (!is_valid_addr(mem, static_cast<Address>(address + done)))
                return false;
        }
        std::memcpy(Ptr<uint8_t>(static_cast<Address>(address)).get(mem), bytes.data(), bytes.size());
        return true;
    }

    void Log(Reader::LogLevel level, const std::string &message) override {
        if (level == Reader::LogLevel::Error)
            LOG_ERROR("[reader] {}", message);
        else
            LOG_INFO("[reader] {}", message);
    }
};

GameMemory g_game_memory;

// Save sync (reader/save_sync/host.h). Vita3K writes a game's saves through the game's file calls,
// which have no one place to listen at: each save's size and time are looked at every few seconds.
using SaveFiles = std::map<std::string, std::pair<std::uintmax_t, std::filesystem::file_time_type>>;

std::filesystem::path g_save_folder;
SaveFiles g_saves_seen;
int g_save_look = 0;

SaveFiles SavesNow(const std::filesystem::path &folder) {
    SaveFiles out;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it{ folder, error }, end; !error && it != end; it.increment(error)) {
        std::error_code inner;
        if (it->is_regular_file(inner))
            out[it->path().generic_string()] = { it->file_size(inner), it->last_write_time(inner) };
    }
    return out;
}

void StartSaveSync(EmuEnvState &emu) {
    if (SaveSync::Host::Started())
        return;
    std::error_code error;
    const auto saves = emu.vita_fs_path / "ux0/user/00/savedata";
    const auto notes = emu.vita_fs_path.parent_path() / "reader-savesync";
    std::filesystem::create_directories(saves, error);
    std::filesystem::create_directories(notes, error);
    SaveSync::Host::Start(saves.string(), notes.string(), [](const std::string &) {}, "Android");
}

void LookAtSaves() {
    if (g_save_folder.empty() || --g_save_look > 0)
        return;
    g_save_look = 180; // some three seconds of frames
    SaveFiles now = SavesNow(g_save_folder);
    if (now != g_saves_seen) {
        g_saves_seen = std::move(now);
        SaveSync::Host::NoteSaved();
    }
}

void ModuleLoaded(const char *name, const char *path, uint32_t base, uint32_t size, uint32_t module_nid) {
    char build[12];
    std::snprintf(build, sizeof(build), "%08X", module_nid);
    // The program it started with is "main" to a profile (reader.base, reader.build_id).
    const bool is_main = path == "app0:" + g_emu->self_path;
    Engine::Instance().OnModuleLoaded(is_main ? "main" : name, base, size, build);
}

} // namespace

void GameLoading(EmuEnvState &emu, app::AppSessionController &session) {
    g_emu = &emu;
    StartSaveSync(emu);
    // As on a Japanese Vita: the games read here are Japanese, and ○ says yes.
    emu.cfg.sys_lang = static_cast<int>(SCE_SYSTEM_PARAM_LANG_JAPANESE);
    emu.cfg.sys_button = static_cast<int>(SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE);
    events.is_hooked = [](uint32_t address) { return Engine::Instance().IsHooked(address); };
    events.hook = [](const GuestRegisters &registers) { Engine::Instance().Dispatch(registers); };
    events.hook_unusable = [](uint32_t address, const char *why) { Engine::Instance().ReportUnusableHook(address, why); };
    events.module_loaded = &ModuleLoaded;
    session.set_before_run_callback([] {
        // Every module of the game is in memory and none of its code has run.
        {
            std::unique_lock lock{ g_memory_mutex };
            g_memory_up = true;
        }
        const std::string id = g_emu->io.title_id;
        // Before the game can read them: the newest comes from the service (at most a few seconds).
        SaveSync::Host::GameStarting(id, g_emu->current_app_title);
        g_save_folder = g_emu->vita_fs_path / "ux0/user/00/savedata" / id;
        g_saves_seen = SavesNow(g_save_folder);
        Engine::Instance().OnGameLoaded(id);
        std::scoped_lock lock{ g_callback_mutex };
        if (g_game_loaded)
            g_game_loaded(id);
    });
    g_loading = true;
    Engine::Instance().OnGameLoading(g_game_memory);
}

void Frame(EmuEnvState &emu, const app::AppSessionController &session) {
    const std::uint32_t pressed = g_pressed.load(std::memory_order_relaxed);
    if (pressed != 0) {
        std::scoped_lock lock{ emu.ctrl.mutex };
        auto &keyboard = emu.ctrl.keyboard_state;
        keyboard.buttons |= pressed;
        // ctrl_get asks in two ways: L and R by these bits, or by L1 and R1 in the extended form.
        keyboard.buttons_ext |= (pressed & ~(BitL | BitR)) | ((pressed & BitL) != 0 ? SCE_CTRL_L1 : 0) | ((pressed & BitR) != 0 ? SCE_CTRL_R1 : 0);
    }
    if (session.is_paused())
        return;
    LookAtSaves();
    auto &engine = Engine::Instance();
    engine.OnPicture(960, 544);
    engine.OnFrame();
}

void GameEnded() {
    if (!g_loading)
        return;
    g_loading = false;
    g_save_look = 0;
    LookAtSaves();
    g_save_folder.clear();
    SaveSync::Host::GameStopped();
    Engine::Instance().OnShutdown();
    {
        std::unique_lock lock{ g_memory_mutex };
        g_memory_up = false;
    }
    g_pressed.store(0);
    std::scoped_lock lock{ g_callback_mutex };
    if (g_game_stopped)
        g_game_stopped();
}

void OnAudio(const void *port, const std::int16_t *samples, std::size_t frames, unsigned channels, int rate) {
    if (rate == static_cast<int>(AudioTap::SampleRate))
        AudioTap::Instance().PushStream(port, samples, frames, channels);
}

void SetGameLoadedCallback(std::function<void(const std::string &)> callback) {
    std::scoped_lock lock{ g_callback_mutex };
    g_game_loaded = std::move(callback);
}

void SetGameStoppedCallback(std::function<void()> callback) {
    std::scoped_lock lock{ g_callback_mutex };
    g_game_stopped = std::move(callback);
}

void SetMenuCallback(std::function<void(const std::string &)> callback) {
    std::scoped_lock lock{ g_callback_mutex };
    g_menu = std::move(callback);
}

void OpenMenu(const std::string &what) {
    std::scoped_lock lock{ g_callback_mutex };
    if (g_menu)
        g_menu(what);
}

std::string GameTitle() {
    return g_emu != nullptr ? g_emu->current_app_title : std::string();
}

void PressButton(int bit, bool down) {
    if (bit < 0 || bit >= 32)
        return;
    if (down)
        g_pressed.fetch_or(1u << bit);
    else
        g_pressed.fetch_and(~(1u << bit));
}

void RestSticks() {
    if (g_emu == nullptr)
        return;
    std::scoped_lock lock{ g_emu->ctrl.mutex };
    auto &keyboard = g_emu->ctrl.keyboard_state;
    for (float &axis : keyboard.axes)
        axis = 0.0f;
}

bool Running() {
    std::shared_lock lock{ g_memory_mutex };
    return g_memory_up;
}

bool PictureRect(int (&values)[6]) {
    std::shared_lock lock{ g_memory_mutex };
    if (!g_memory_up)
        return false;
    const auto &display = g_emu->display;
    const auto width = static_cast<int>(display.viewport_w);
    const auto height = static_cast<int>(display.viewport_h);
    if (width <= 0 || height <= 0)
        return false;
    const auto left = static_cast<int>(display.viewport_x);
    const auto top = static_cast<int>(display.viewport_y);
    values[0] = display.viewport_drawable_w;
    values[1] = display.viewport_drawable_h;
    values[2] = left;
    values[3] = top;
    values[4] = left + width;
    values[5] = top + height;
    return true;
}

void ToggleNextChoice() {}

bool GoingToNextChoice() {
    return false;
}

void SetNextChoiceCallback(std::function<void(bool, int, std::uint64_t)>) {}

} // namespace Reader::Emu
