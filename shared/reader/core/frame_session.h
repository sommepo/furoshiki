// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The reader for a host that only hears, once a frame, which game runs: a reader that is not
// inside the emulator's own loading of a game (Adrenaline Reader on the Vita, a libretro core,
// a core of Reader Home's). It follows the game (another one, the same started again), loads
// its script once its build is known, runs it once a frame, and keeps what it read and the
// line to show. The host hands it the game's memory as the engine's Host.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include "reader/core/engine.h"

namespace Reader {

/// What runs, as the host knows it this frame.
struct RunningGame {
    std::string id;     ///< As a script is named ("ULJM05887", "SLPS-01794")
    std::string title;
    std::uint64_t start{}; ///< Another value when the game was started anew
    /// Its build, as the scripts check it (reader.build_id); empty when it has none or it is not
    /// known. While `build_pending` the host is still working it out and nothing is loaded.
    std::string build;
    bool build_pending{};
    /// Where its program is (reader.module("main")).
    std::uint64_t main_base{};
    std::uint64_t main_size{};
    /// Where its data is, where that is not beside its program (reader.module("data"): a Vita game
    /// on the real console, whose segments the system places apart). 0: none.
    std::uint64_t data_base{};
    std::uint64_t data_size{};
};

/// One piece of text the game's script read.
struct ReadLine {
    std::string text;
    std::string speaker;
    std::string channel;
    std::string window; ///< What it is shown in, when the script says ("message")
    std::uint64_t sequence{};
    std::uint64_t frame{}; ///< The session's frame it came on
};

class FrameSession {
public:
    /// A game's script by its ID, or nothing when it has none.
    using Scripts = std::function<std::optional<std::string>(const std::string& game_id)>;
    using Logger = std::function<void(const std::string& message)>;

    /// The engine is the one there is (Engine::Instance()): one session at a time.
    /// `carries_hooks`: whether the host carries out a script's hooks (else a script that asks
    /// for them is said to read only what its frame callbacks read).
    FrameSession(Host& host, Scripts scripts, Logger log = {}, bool carries_hooks = false);
    ~FrameSession();
    FrameSession(const FrameSession&) = delete;
    FrameSession& operator=(const FrameSession&) = delete;

    /// Also hands each text event and layout of the engine's to these (a reading layer that
    /// places words, as the Vita apps' LookupLayer), on the thread that ticks.
    void SetListeners(std::function<void(const TextEvent&)> on_text,
                      std::function<void(const TextLayout&)> on_layout);

    /// Once a frame, with what runs (nothing: no game).
    void Tick(const RunningGame* running);
    /// The game left: what was read of it stays, nothing runs.
    void Stop();

    [[nodiscard]] const std::string& GameId() const {
        return game_id;
    }
    /// Whether a script runs for the game.
    [[nodiscard]] bool Reading() const;
    /// What was read, the latest last (at most MaxLines).
    [[nodiscard]] const std::deque<ReadLine>& Lines() const {
        return lines;
    }
    /// The line to show over the game: the latest, while the game shows it (its script has
    /// not said that its window closed), or for ShowFrames when the script says nothing of
    /// windows; nothing otherwise.
    [[nodiscard]] const ReadLine* Current() const;
    /// A line on the reader's state for the player ("No script for SLPS-01794", the script's
    /// error), or empty.
    [[nodiscard]] const std::string& Status() const {
        return status;
    }

    static constexpr std::size_t MaxLines = 200;
    static constexpr std::uint64_t ShowFrames = 60 * 20;

private:
    void Begin(const RunningGame& running);

    Host& host;
    bool carries_hooks;
    Scripts scripts;
    Logger log;
    std::string game_id;
    std::uint64_t start{};
    bool waiting_for_build{};
    std::deque<ReadLine> lines;
    std::string status;
    std::uint64_t frames{};
    std::function<void(const TextEvent&)> text_listener;
    std::function<void(const TextLayout&)> layout_listener;
    /// Windows the script reported a layout for, and whether each is closed now.
    std::unordered_map<std::string, bool> windows_closed;
};

} // namespace Reader
