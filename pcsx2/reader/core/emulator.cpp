// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// The one file that ties the text engine to the emulator.

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "Config.h"
#include "Host.h"
#include "Memory.h"
#include "MemoryTypes.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadDualshock2.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Memcard/MemoryCardFolder.h"
#include "VMManager.h"
#include "MTGS.h"
#include "GS/Renderers/Common/GSRenderer.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#include "reader/core/memory_card.h"
#include "reader/core/next_choice.h"
#include "reader/save_sync/host.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace Reader::Emu {

namespace {

// A game's own program and data live in the console's main memory, which every one of its
// address ranges (the plain one, the two kernel ones) shows whole.
constexpr u32 MainMemoryMask = 0x1FFFFFFF;
constexpr u32 ScratchpadBase = 0x70000000;

class Ps2Host final : public Host {
public:
    std::string ReadMemory(u64 address, std::size_t size) override {
        if (address > 0xFFFFFFFFULL || !eeMem || size == 0) {
            return {};
        }
        const u32 start = static_cast<u32>(address);
        if (start >= ScratchpadBase && start < ScratchpadBase + Ps2MemSize::Scratch) {
            const u32 offset = start - ScratchpadBase;
            const std::size_t valid = std::min<std::size_t>(size, Ps2MemSize::Scratch - offset);
            return std::string(reinterpret_cast<const char*>(eeMem->Scratch + offset), valid);
        }
        // 0x00000000, 0x80000000 and 0xA0000000 are three views of the same memory.
        if (start >= 0xC0000000 || (start >= 0x20000000 && start < 0x80000000)) {
            return {};
        }
        const u32 offset = start & MainMemoryMask;
        if (offset >= Ps2MemSize::ExposedRam) {
            return {};
        }
        const std::size_t valid = std::min<std::size_t>(size, Ps2MemSize::ExposedRam - offset);
        return std::string(reinterpret_cast<const char*>(eeMem->Main + offset), valid);
    }

    bool WriteMemory(u64 address, const std::string& bytes) override {
        // Data only, in main memory: what a profile marking progress writes (a game's own
        // variables). Code written here would not reach what the recompilers already made of it.
        if (address > 0xFFFFFFFFULL || !eeMem || bytes.empty()) {
            return false;
        }
        const u32 start = static_cast<u32>(address);
        if (start >= 0xC0000000 || (start >= 0x20000000 && start < 0x80000000)) {
            return false;
        }
        const u32 offset = start & MainMemoryMask;
        if (offset < 0x00100000 || offset + bytes.size() > Ps2MemSize::ExposedRam) {
            return false;
        }
        std::memcpy(eeMem->Main + offset, bytes.data(), bytes.size());
        return true;
    }

    void InvalidateCode() override {
        // Hooks in the game's code are not carried out by this emulator's recompilers yet: a
        // profile here reads memory once a frame. (needs proper testing once they are)
    }

    std::vector<MemoryRegion> MemoryRegions() override {
        if (!eeMem) {
            return {};
        }
        return {
            MemoryRegion{0x00100000, Ps2MemSize::ExposedRam - 0x00100000, true, true, true, 1},
            MemoryRegion{ScratchpadBase, Ps2MemSize::Scratch, true, true, false, 2},
        };
    }
};

Ps2Host g_host;
std::mutex g_callback_mutex;
std::function<void(const std::string&, const std::string&)> g_game_loaded;
std::function<void()> g_game_stopped;
std::function<bool(const std::string&)> g_profile_check;
std::string g_serial;
u32 g_crc = 0;
bool g_loaded = false;

void EndNextChoice(NextChoice::End end);

void Unload() {
    EndNextChoice(NextChoice::End::GameGone);
    if (!g_loaded) {
        return;
    }
    g_loaded = false;
    Engine::Instance().OnShutdown();
    InputFilter::Instance().HoldSticks(false);
    std::function<void()> callback;
    {
        std::scoped_lock lock{g_callback_mutex};
        callback = g_game_stopped;
    }
    if (callback) {
        callback();
    }
}

} // Anonymous namespace

namespace {
std::mutex g_card_mutex;
std::string g_card_serial; ///< The game whose own card is in the first slot
std::string g_card_folder;
std::string g_card_file;   ///< That game's card: "card.ps2" in its folder
} // namespace

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
    Engine& engine = Engine::Instance();
    engine.OnGameLoading(g_host);
    char build[16];
    std::snprintf(build, sizeof(build), "%08X", crc);
    // A game's program is loaded where it was linked for: profiles count from 0.
    engine.OnModuleLoaded("main", 0, Ps2MemSize::ExposedRam, build);
    engine.OnGameLoaded(serial);
    engine.OnPicture(640, 448);
    g_loaded = true;
    std::function<void(const std::string&, const std::string&)> callback;
    {
        std::scoped_lock lock{g_callback_mutex};
        callback = g_game_loaded;
    }
    if (callback) {
        callback(serial, title);
    }
}

void OnShutdown() {
    Unload();
    g_serial.clear();
    g_crc = 0;
    if (SaveSync::Host::Started()) {
        SaveSync::Host::GameStopped();
    }
    std::scoped_lock lock{g_card_mutex};
    g_card_serial.clear();
}

void EnsureSaveSync() {
#ifdef READER_NO_SAVE_SYNC
    return; // a published build has no save sync
#endif
    if (SaveSync::Host::Started()) {
        return;
    }
    const std::string cards = Path::Combine(EmuFolders::MemoryCards, "reader-saves");
    const std::string data = Path::Combine(EmuFolders::DataRoot, "reader-savesync");
    FileSystem::CreateDirectoryPath(cards.c_str(), true);
    FileSystem::CreateDirectoryPath(data.c_str(), true);
    std::string device = "PCSX2 Reader";
#ifdef _WIN32
    if (const char* name = std::getenv("COMPUTERNAME")) {
        device = name;
    }
#else
    char name[128] = {};
    if (gethostname(name, sizeof(name) - 1) == 0 && name[0] != 0) {
        device = name;
    }
#endif
    SaveSync::Host::Start(
        cards, data,
        [](const std::string& text) { ::Host::AddKeyedOSDMessage("ReaderSaveSync", text, ::Host::OSD_INFO_DURATION); },
        device);
}

void BeforeMemoryCards(const std::string& serial, const std::string& title) {
    EnsureSaveSync();
    // READER_NO_OWN_CARD=1 leaves the memory cards as the settings have them (for looking
    // into a problem with a card).
    bool game = !SaveSync::Host::TitleOf(serial).empty() && std::getenv("READER_NO_OWN_CARD") == nullptr;
#ifdef READER_NO_SAVE_SYNC
    game = false; // without save sync the cards are the settings' own, as in PCSX2
#endif
    {
        std::scoped_lock lock{g_card_mutex};
        if (game && serial == g_card_serial) {
            return; // the same game again (its program started): the card is in already
        }
        g_card_serial = game ? serial : std::string{};
        g_card_folder = game ? Path::Combine(Path::Combine(EmuFolders::MemoryCards, "reader-saves"), serial)
                             : std::string{};
        g_card_file = game ? Path::Combine(g_card_folder, "card.ps2") : std::string{};
    }
    if (!game) {
        SaveSync::Host::GameStopped();
        return;
    }
    // The newest save first, then the card: a card made here before the fetch would look
    // like a save of this device's own.
    SaveSync::Host::GameStarting(serial, title);
    std::string folder, card;
    {
        std::scoped_lock lock{g_card_mutex};
        folder = g_card_folder;
        card = g_card_file;
    }
    FileSystem::CreateDirectoryPath(folder.c_str(), true);
    if (!FileSystem::FileExists(card.c_str())) {
        // A new card: an ordinary 8 MB card file, formatted. The emulator's own new cards are
        // blank, which most games format when they first save; some do not (CLANNAD only says
        // the card is not formatted), and with a card of the game's own there is no other card
        // to format it in.
        if (FileMcd_CreateNewCard(Path::Combine(Path::Combine("reader-saves", serial), "card.ps2"),
                                  MemoryCardType::File, MemoryCardFileType::PS2_8MB) &&
            !Reader::FormatCard(card)) {
            Console.Warning("Reader: could not format the new memory card %s", card.c_str());
        }
    }
}

std::string MemoryCardOverride() {
    std::scoped_lock lock{g_card_mutex};
    return g_card_file;
}

void OnMemoryCardWritten(unsigned slot) {
    if (slot == 0 && SaveSync::Host::Started() && !MemoryCardOverride().empty()) {
        SaveSync::Host::NoteSaved();
    }
}

void OnPadVibration(unsigned, float, float) {}

// ---- Going on to the next choice ----

namespace {
// On the emulator's thread, except the two atomics.
NextChoice g_next_choice;
std::atomic<bool> g_next_choice_running{false};
std::atomic<bool> g_player_pressed{false};
thread_local bool g_own_press = false; ///< The button being set is the reader's, not the player's
bool g_holding = false;     ///< The reader holds ○ down
LimiterModeType g_limiter_before = LimiterModeType::Nominal;
/// After it ends, the engine stays quiet up to this frame (0: not waiting): a profile reports
/// the message under the choice a few frames after the choice, and what it still had of the
/// text passed on the way comes then too. The last message is sent when the time is up.
u64 g_quiet_until = 0;
constexpr u64 QuietAfterFrames = 30;
std::mutex g_next_choice_mutex;
std::function<void(bool, int, std::uint64_t)> g_next_choice_callback;

constexpr const char* NextChoiceMessage = "ReaderNextChoice";

void TellNextChoice(bool running, int end, std::uint64_t passed) {
    std::function<void(bool, int, std::uint64_t)> callback;
    {
        std::scoped_lock lock{g_next_choice_mutex};
        callback = g_next_choice_callback;
    }
    if (callback) {
        callback(running, end, passed);
    }
}

void SayNextChoice(std::string text) {
    Console.WriteLn("Reader: %s", text.c_str());
    ::Host::AddKeyedOSDMessage(NextChoiceMessage, std::move(text), ::Host::OSD_INFO_DURATION);
}

void HoldCircle(bool down) {
    if (g_holding == down) {
        return;
    }
    g_holding = down;
    g_own_press = true;
    Pad::SetControllerState(0, PadDualshock2::Inputs::PAD_CIRCLE, down ? 1.0f : 0.0f);
    g_own_press = false;
}

void StartNextChoice() {
    if (g_next_choice.Running() || !g_loaded || !VMManager::HasValidVM()) {
        return;
    }
    Engine& engine = Engine::Instance();
    if (!engine.HasProfile()) {
        SayNextChoice("Next choice: this game has no text profile");
        return;
    }
    if (engine.ChoiceShown()) {
        SayNextChoice("Next choice: a choice is on the screen already");
        return;
    }
    engine.SetQuiet(true);
    g_quiet_until = 0;
    g_player_pressed = false;
    g_next_choice.Begin(engine.Frame(), engine.ChoicesEmitted(), engine.TextsEmitted());
    g_next_choice_running = true;
    g_limiter_before = VMManager::GetLimiterMode();
    VMManager::SetLimiterMode(LimiterModeType::Unlimited);
    SayNextChoice("Going on to the next choice (press any button to stop)");
    TellNextChoice(true, -1, 0);
}

void EndNextChoice(NextChoice::End end) {
    if (!g_next_choice_running) {
        return;
    }
    g_next_choice.Cancel();
    g_next_choice_running = false;
    if (VMManager::HasValidVM()) {
        HoldCircle(false);
        VMManager::SetLimiterMode(g_limiter_before);
    }
    g_holding = false;
    if (end == NextChoice::End::GameGone) {
        g_quiet_until = 0;
        Engine::Instance().SetQuiet(false);
    } else {
        g_quiet_until = Engine::Instance().Frame() + QuietAfterFrames;
    }
    const std::uint64_t passed = g_next_choice.Passed();
    std::string text;
    switch (end) {
    case NextChoice::End::Choice:
        text = "Next choice: here (" + std::to_string(passed) + " texts passed)";
        break;
    case NextChoice::End::NoText:
        text = "Next choice: stopped, nothing was said for a while";
        break;
    case NextChoice::End::UserInput:
        text = "Next choice: stopped by a button (" + std::to_string(passed) + " texts passed)";
        break;
    case NextChoice::End::Stopped:
        text = "Next choice: stopped (" + std::to_string(passed) + " texts passed)";
        break;
    case NextChoice::End::GameGone:
        break;
    }
    if (!text.empty()) {
        SayNextChoice(std::move(text));
    }
    TellNextChoice(false, static_cast<int>(end), passed);
}
} // namespace

void ToggleNextChoice() {
    if (!VMManager::HasValidVM()) {
        return;
    }
    ::Host::RunOnCPUThread([] {
        if (g_next_choice_running) {
            EndNextChoice(NextChoice::End::Stopped);
        } else {
            StartNextChoice();
        }
    });
}

void StopNextChoice() {
    if (!g_next_choice_running || !VMManager::HasValidVM()) {
        return;
    }
    ::Host::RunOnCPUThread([] { EndNextChoice(NextChoice::End::Stopped); });
}

bool GoingToNextChoice() {
    return g_next_choice_running;
}

void SetNextChoiceCallback(std::function<void(bool, int, std::uint64_t)> callback) {
    std::scoped_lock lock{g_next_choice_mutex};
    g_next_choice_callback = std::move(callback);
}

void OnPadInput(unsigned controller, unsigned bind, float value) {
    // The player's buttons (not the sticks, which are never quite at rest) on any controller.
    if (g_next_choice_running && !g_own_press && value >= 0.5f && bind <= PadDualshock2::Inputs::PAD_R3) {
        g_player_pressed = true;
    }
}

void OnStateLoaded() {
    EndNextChoice(NextChoice::End::GameGone);
    if (g_quiet_until != 0) {
        g_quiet_until = 0;
        Engine::Instance().SetQuiet(false);
    }
    if (g_loaded) {
        Engine::Instance().OnStateLoaded();
    }
}

void OnFrame() {
    if (!g_loaded) {
        return;
    }
    Engine& engine = Engine::Instance();
    engine.OnFrame();
    if (g_quiet_until != 0 && engine.Frame() >= g_quiet_until) {
        g_quiet_until = 0;
        engine.SetQuiet(false);
    }
    if (!g_next_choice_running) {
        return;
    }
    const NextChoice::Step step = g_next_choice.OnFrame(engine.Frame(), engine.ChoicesEmitted(),
                                                        engine.TextsEmitted(), g_player_pressed.exchange(false));
    if (step.end) {
        EndNextChoice(*step.end);
        return;
    }
    HoldCircle(step.press);
}

void SetGameLoadedCallback(
    std::function<void(const std::string& game_id, const std::string& title)> callback) {
    std::scoped_lock lock{g_callback_mutex};
    g_game_loaded = std::move(callback);
}

void SetGameStoppedCallback(std::function<void()> callback) {
    std::scoped_lock lock{g_callback_mutex};
    g_game_stopped = std::move(callback);
}

void SetProfileCheck(std::function<bool(const std::string&)> check) {
    std::scoped_lock lock{g_callback_mutex};
    g_profile_check = std::move(check);
}

bool HasProfile(const std::string& game_id) {
    std::function<bool(const std::string&)> check;
    {
        std::scoped_lock lock{g_callback_mutex};
        check = g_profile_check;
    }
    return check && check(game_id);
}

void OnAudioFloat(const float* samples, std::size_t frames) {
    AudioTap& tap = AudioTap::Instance();
    if (!tap.IsEnabled()) {
        return;
    }
    // On the thread that mixes the sound, a chunk at a time.
    static thread_local std::vector<s16> scratch;
    scratch.resize(frames * 2);
    for (std::size_t i = 0; i < frames * 2; ++i) {
        const float value = std::clamp(samples[i], -1.0f, 1.0f);
        scratch[i] = static_cast<s16>(value * 32767.0f);
    }
    tap.Push(scratch.data(), frames);
}

void OnAudio(const int16_t* samples, std::size_t frames) {
    AudioTap& tap = AudioTap::Instance();
    if (tap.IsEnabled()) {
        tap.Push(samples, frames);
    }
}

void PressButton(int button, bool down) {
    // By the emulator's own numbers of the first controller's inputs (PadDualshock2::Inputs).
    if (button < 0 || button >= static_cast<int>(PadDualshock2::Inputs::LENGTH) || !VMManager::HasValidVM()) {
        return;
    }
    ::Host::RunOnCPUThread([button, down] {
        if (VMManager::HasValidVM()) {
            Pad::SetControllerState(0, static_cast<u32>(button), down ? 1.0f : 0.0f);
        }
    });
}

void RestSticks() {
    if (!VMManager::HasValidVM()) {
        return;
    }
    ::Host::RunOnCPUThread([] {
        if (!VMManager::HasValidVM()) {
            return;
        }
        for (u32 bind = PadDualshock2::Inputs::PAD_L_UP; bind <= PadDualshock2::Inputs::PAD_R_LEFT; ++bind) {
            Pad::SetControllerState(0, bind, 0.0f);
        }
    });
}

bool DumpMemory(const std::string& path) {
    if (!eeMem) {
        return false;
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const bool written = std::fwrite(eeMem->Main, 1, Ps2MemSize::ExposedRam, file) == Ps2MemSize::ExposedRam;
    std::fclose(file);
    return written;
}

void SaveStateSlot(int slot) {
    VMManager::SaveStateToSlot(slot, false, [](const std::string& message) {
        Console.Error("Reader: save state: %s", message.c_str());
    });
}

bool LoadStateSlot(int slot) {
    return VMManager::LoadStateFromSlot(slot);
}

void SetTurbo(bool on) {
    VMManager::SetLimiterMode(on ? LimiterModeType::Turbo : LimiterModeType::Nominal);
}

void SetPaused(bool paused) {
    VMManager::SetPaused(paused);
}

bool DumpIopMemory(const std::string& path) {
    if (!iopMem) {
        return false;
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const bool written = std::fwrite(iopMem->Main, 1, Ps2MemSize::IopRam, file) == Ps2MemSize::IopRam;
    std::fclose(file);
    return written;
}

bool DumpVram4(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    std::fclose(file);
    // On the GS thread, where its memory is; the file is written there.
    MTGS::RunOnGSThread([path]() {
        if (!g_gs_renderer) {
            return;
        }
        const GSLocalMemory& mem = g_gs_renderer->m_mem;
        constexpr int width = 1024;
        constexpr int height = 8192; // 4 MB of 4-bit pixels
        std::vector<u8> pixels(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                pixels[static_cast<size_t>(y) * width + x] = static_cast<u8>(mem.ReadPixel4(x, y, 0, width / 64));
            }
        }
        if (std::FILE* out = std::fopen(path.c_str(), "wb")) {
            std::fwrite(pixels.data(), 1, pixels.size(), out);
            std::fclose(out);
        }
    });
    return true;
}

void RunOnEmulator(std::function<void()> work) {
    ::Host::RunOnCPUThread(std::move(work));
}

} // namespace Reader::Emu
