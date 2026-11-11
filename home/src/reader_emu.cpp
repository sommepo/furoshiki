// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The program's emulator.cpp (engine/reader/core/emulator.h): what the shared reader windows
// ask of the emulator, asked of the core that runs the game.

#include "reader/core/engine.h"
#include "reader/core/next_choice.h"
#include <mutex>
#include <atomic>
#include <cstdio>
#include <vector>
#include "core_session.h"
#include "reader/core/emulator.h"

#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif

namespace Reader::Emu {

namespace {
std::atomic<Home::CoreSession*> g_session{nullptr};
} // namespace

void NextChoiceFrame();
void NextChoicePlayerInput();
void NextChoiceGameGone();

void SetSession(Home::CoreSession* session) {
    g_session = session;
    Home::SessionHooks::frame = &NextChoiceFrame;
    Home::SessionHooks::player_input = &NextChoicePlayerInput;
    Home::SessionHooks::game_gone = &NextChoiceGameGone;
}

Home::CoreSession* Session() {
    return g_session.load();
}

void PressButton(int bit, bool down) {
    Home::CoreSession* session = g_session.load();
    if (session != nullptr && bit >= 0 && session->Api().press_button != nullptr) {
        session->Api().press_button(bit, down ? 1 : 0);
    }
}

void RestSticks() {
    // The program's controller hands the core its sticks each frame; while the reader's cursor
    // holds them the input filter keeps them from the game (Home's pad source asks it).
}

bool DumpMemory(const std::string& path) {
    Home::CoreSession* session = g_session.load();
    if (session == nullptr || !session->Running()) {
        return false;
    }
    const reader_core_api& api = session->Api();
    std::vector<reader_core_region> regions(16);
    for (;;) {
        const uint32_t count = api.memory_regions(regions.data(), static_cast<uint32_t>(regions.size()));
        if (count <= regions.size()) {
            regions.resize(count);
            break;
        }
        regions.resize(count);
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    bool ok = !regions.empty();
    std::vector<char> buffer;
    for (const reader_core_region& region : regions) {
        buffer.assign(region.size, '\0');
        const std::size_t got = api.read_memory(region.base, buffer.data(), buffer.size());
        ok = ok && std::fwrite(buffer.data(), 1, got, file) == got;
    }
    std::fclose(file);
    return ok;
}

// ---- On to the next choice (reader/core/next_choice.h), as PCSX2 Reader has it: the game runs
// as fast as it goes with the console's "yes" pressed over and over, the engine kept quiet,
// until the script says a choice is up, nothing was said for a long while, or the player
// presses a button. Any core: it asks only for the speed setting and a button pressed. ----

namespace {
NextChoice g_next_choice;
std::atomic<bool> g_next_choice_running{false};
std::atomic<bool> g_next_choice_wanted{false}; ///< Asked for from another thread: taken at a frame
std::atomic<bool> g_player_pressed{false};
std::mutex g_next_choice_mutex;
std::function<void(bool, int, std::uint64_t)> g_next_choice_callback;
bool g_yes_held = false;

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

void HoldYes(Home::CoreSession& session, bool down) {
    const int yes = session.Info().button_yes;
    if (yes >= 0 && down != g_yes_held && session.Api().press_button != nullptr) {
        g_yes_held = down;
        session.Api().press_button(yes, down ? 1 : 0);
    }
}

void EndNextChoice(Home::CoreSession& session, NextChoice::End end) {
    if (!g_next_choice_running.exchange(false)) {
        return;
    }
    const std::uint64_t passed = g_next_choice.Passed();
    g_next_choice.Cancel();
    HoldYes(session, false);
    session.ApplySettings({{READER_CORE_SETTING_SPEED, "100"}});
    Engine::Instance().SetQuiet(false);
    TellNextChoice(false, static_cast<int>(end), passed);
}
} // namespace

void NextChoiceFrame() {
    Home::CoreSession* session = g_session.load();
    if (session == nullptr) {
        return;
    }
    Engine& engine = Engine::Instance();
    if (g_next_choice_wanted.exchange(false)) {
        if (g_next_choice_running) {
            EndNextChoice(*session, NextChoice::End::Stopped);
        } else if (engine.HasProfile() && !engine.ChoiceShown()) {
            engine.SetQuiet(true);
            g_player_pressed = false;
            g_next_choice.Begin(engine.Frame(), engine.ChoicesEmitted(), engine.TextsEmitted());
            g_next_choice_running = true;
            session->ApplySettings({{READER_CORE_SETTING_SPEED, "0"}});
            TellNextChoice(true, -1, 0);
        }
        return;
    }
    if (!g_next_choice_running) {
        return;
    }
    const NextChoice::Step step = g_next_choice.OnFrame(engine.Frame(), engine.ChoicesEmitted(), engine.TextsEmitted(),
                                                        g_player_pressed.exchange(false));
    if (step.end) {
        EndNextChoice(*session, *step.end);
        return;
    }
    HoldYes(*session, step.press);
}

void NextChoicePlayerInput() {
    if (g_next_choice_running) {
        g_player_pressed = true;
    }
}

void NextChoiceGameGone() {
    if (g_next_choice_running.exchange(false)) {
        g_next_choice.Cancel();
        g_yes_held = false;
        Engine::Instance().SetQuiet(false);
        TellNextChoice(false, static_cast<int>(NextChoice::End::GameGone), 0);
    }
}

void ToggleNextChoice() {
    // Taken at the game's next frame, on the thread the engine's counts are the game's on.
    g_next_choice_wanted = true;
}

bool GoingToNextChoice() {
    return g_next_choice_running;
}

void SetNextChoiceCallback(std::function<void(bool, int, std::uint64_t)> callback) {
    std::scoped_lock lock{g_next_choice_mutex};
    g_next_choice_callback = std::move(callback);
}

bool EnsureSaveSync() {
#ifdef READER_NO_SAVE_SYNC
    return false;
#else
    return SaveSync::Host::Started();
#endif
}

} // namespace Reader::Emu
