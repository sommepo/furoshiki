// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// The one file that ties the text engine to the emulator.

#include <algorithm>
#include <cstring>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <mutex>
#include <optional>
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
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "reader/core/audio_tap.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"
#include "reader/core/next_choice.h"
#include "reader/save_sync/host.h"

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

namespace {
// The app's settings for save sync, kept until the emulator's folders are known (the app's
// screen can be up before the emulator has read its settings).
std::mutex g_sync_settings_mutex;
std::optional<SaveSync::Host::Settings> g_sync_settings;

void ApplySyncSettings() {
    // Before save sync has started the settings stay where they are, for EnsureSaveSync to
    // apply once it starts: taken now, they would be lost.
    if (!SaveSync::Host::Started()) {
        return;
    }
    std::optional<SaveSync::Host::Settings> settings;
    {
        std::scoped_lock lock{g_sync_settings_mutex};
        settings.swap(g_sync_settings);
    }
    if (!settings) {
        return;
    }
    const SaveSync::Host::Settings before = SaveSync::Host::Get();
    if (before.enabled == settings->enabled && before.addresses == settings->addresses &&
        before.code == settings->code && before.device == settings->device) {
        return;
    }
    SaveSync::Host::Set(*settings);
    if (settings->enabled) {
        SaveSync::Host::SyncAll();
    }
}
} // namespace

void ConfigureSaveSync(const SaveSync::Host::Settings& settings) {
#ifdef READER_NO_SAVE_SYNC
    return;
#endif
    {
        std::scoped_lock lock{g_sync_settings_mutex};
        g_sync_settings = settings;
    }
    EnsureSaveSync();
    ApplySyncSettings();
}

void EnsureSaveSync() {
#ifdef READER_NO_SAVE_SYNC
    return; // a published build has no save sync
#endif
    if (SaveSync::Host::Started()) {
        return;
    }
    if (EmuFolders::MemoryCards.empty() || EmuFolders::DataRoot.empty()) {
        return; // the emulator has not read its settings yet: the app's settings wait
    }
    const std::string cards = Path::Combine(EmuFolders::MemoryCards, "reader-saves");
    const std::string data = Path::Combine(EmuFolders::DataRoot, "reader-savesync");
    FileSystem::CreateDirectoryPath(cards.c_str(), true);
    FileSystem::CreateDirectoryPath(data.c_str(), true);
    // The app names the device (its model, or what the user wrote) when it sets save sync up.
    const std::string device = "ARMSX2 Reader";
    SaveSync::Host::Start(
        cards, data,
        [](const std::string& text) { ::Host::AddKeyedOSDMessage("ReaderSaveSync", text, ::Host::OSD_INFO_DURATION); },
        device);
    ApplySyncSettings();
}

namespace {

/// Formats a blank 8 MB card file as the console does: superblock, indirect FAT list, FAT and
/// an empty root folder, each page with its ECC (the layout of the file system documented in
/// pcsx2/Reference/PS2-MemoryCardFileSystem.htm; the values those of a card the console
/// formatted). Only for a file that was just made blank.
bool FormatCard(const std::string& path) {
    constexpr u32 PageSize = 512;
    constexpr u32 RawPage = 528;
    constexpr u32 Clusters = 8192;
    constexpr u32 FatClusters = 32;  // clusters 9 to 40
    constexpr u32 AllocOffset = 41;
    constexpr u32 AllocEnd = 8135;   // clusters to allocate, from AllocOffset
    std::vector<u8> card(static_cast<std::size_t>(Clusters) * 2 * RawPage, 0xFF);
    // Writes one page's data (512 bytes) and its spare area: ECC over each 128 bytes, then zeros.
    const auto put_page = [&card](u32 page, const u8* data) {
        u8* at = card.data() + static_cast<std::size_t>(page) * RawPage;
        std::memcpy(at, data, PageSize);
        u8 spare[16]{};
        for (int i = 0; i < 4; ++i) {
            FolderMemoryCard::CalculateECC(spare + i * 3, data + i * 128);
        }
        std::memcpy(at + PageSize, spare, sizeof(spare));
    };
    const auto put_cluster = [&put_page](u32 cluster, const std::vector<u8>& data) {
        put_page(cluster * 2, data.data());
        put_page(cluster * 2 + 1, data.data() + PageSize);
    };
    const auto set32 = [](std::vector<u8>& data, std::size_t at, u32 value) {
        std::memcpy(data.data() + at, &value, 4);
    };
    const auto set16 = [](std::vector<u8>& data, std::size_t at, u16 value) {
        std::memcpy(data.data() + at, &value, 2);
    };

    std::vector<u8> super(PageSize, 0xFF);
    static constexpr char magic[] = "Sony PS2 Memory Card Format 1.2.0.0";
    std::memset(super.data(), 0, 0x28);
    std::memcpy(super.data(), magic, sizeof(magic) - 1);
    set16(super, 0x28, PageSize);
    set16(super, 0x2A, 2);    // pages a cluster
    set16(super, 0x2C, 16);   // pages a block
    set16(super, 0x2E, 0xFF00);
    set32(super, 0x30, Clusters);
    set32(super, 0x34, AllocOffset);
    set32(super, 0x38, AllocEnd);
    set32(super, 0x3C, 0);    // the root folder's cluster
    set32(super, 0x40, 1023); // backup blocks
    set32(super, 0x44, 1022);
    std::memset(super.data() + 0x48, 0, 0x08);
    std::memset(super.data() + 0x50, 0, 0x80);
    set32(super, 0x50, 8);    // the indirect FAT list's cluster
    std::memset(super.data() + 0xD0, 0xFF, 0x80); // no bad blocks
    super[0x150] = 2;         // a PS2 card
    super[0x151] = 0x2B;
    static constexpr u8 tail[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                                  0x08, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x41, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(super.data() + 0x152, tail, sizeof(tail));
    put_page(0, super.data());

    std::vector<u8> ifc(PageSize * 2, 0xFF);
    for (u32 i = 0; i < FatClusters; ++i) {
        set32(ifc, i * 4, 9 + i);
    }
    put_cluster(8, ifc);

    // The FAT: the root folder's one cluster ends its chain; every other cluster is free.
    for (u32 c = 0; c < FatClusters; ++c) {
        std::vector<u8> fat(PageSize * 2, 0xFF);
        for (u32 i = 0; i < 256; ++i) {
            const u32 entry = c * 256 + i;
            if (entry < AllocEnd) {
                set32(fat, i * 4, entry == 0 ? 0xFFFFFFFFu : 0x7FFFFFFFu);
            }
        }
        put_cluster(9 + c, fat);
    }

    // The root folder: "." and "..", made now.
    std::vector<u8> root(PageSize * 2, 0x00);
    const std::time_t now = std::time(nullptr);
    std::tm when{};
#ifdef _WIN32
    gmtime_s(&when, &now);
#else
    gmtime_r(&now, &when);
#endif
    const auto stamp = [&](std::size_t at) {
        root[at] = 0;
        root[at + 1] = static_cast<u8>(when.tm_sec);
        root[at + 2] = static_cast<u8>(when.tm_min);
        root[at + 3] = static_cast<u8>(when.tm_hour);
        root[at + 4] = static_cast<u8>(when.tm_mday);
        root[at + 5] = static_cast<u8>(when.tm_mon + 1);
        set16(root, at + 6, static_cast<u16>(when.tm_year + 1900));
    };
    set16(root, 0x000, 0x8427);
    set32(root, 0x004, 2);
    stamp(0x008);
    stamp(0x018);
    root[0x040] = '.';
    set16(root, 0x200, 0xA426);
    stamp(0x208);
    stamp(0x218);
    root[0x240] = '.';
    root[0x241] = '.';
    put_cluster(AllocOffset, root);

    auto fp = FileSystem::OpenManagedCFile(path.c_str(), "r+b");
    if (!fp) {
        return false;
    }
    return std::fwrite(card.data(), card.size(), 1, fp.get()) == 1;
}

} // namespace

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
            !FormatCard(card)) {
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
    // By the emulator's own numbers of the first controller's inputs (PadDualshock2::Inputs), as
    // in PCSX2 Reader. Straight from the app's thread, as the upstream on-screen buttons do
    // (NativeApp.setPadButton); Pad.cpp passes it on as a press of the player's, so a press made
    // for the player (touch play, "Next line") also ends a run to the next choice.
    if (button < 0 || button >= static_cast<int>(PadDualshock2::Inputs::LENGTH) || !VMManager::HasValidVM()) {
        return;
    }
    Pad::SetControllerState(0, static_cast<u32>(button), down ? 1.0f : 0.0f);
}

void RestSticks() {
    if (!VMManager::HasValidVM()) {
        return;
    }
    for (u32 bind = PadDualshock2::Inputs::PAD_L_UP; bind <= PadDualshock2::Inputs::PAD_R_LEFT; ++bind) {
        Pad::SetControllerState(0, bind, 0.0f);
    }
}

} // namespace Reader::Emu
