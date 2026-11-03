// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PCSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include "reader/core/types.h"

namespace Reader {

/**
 * Runs a visual novel through to its next choice: for someone who has read this far before
 * (on another copy of the game, say) and wants to give the game the choices they made there.
 * The emulator runs as fast as it can and the confirm button is pressed over and over, until
 * the profile reports a choice. Knows nothing of the emulator: emulator.cpp does what it says,
 * once a frame, and keeps the engine quiet meanwhile (Engine::SetQuiet).
 */
class NextChoice {
public:
    enum class End {
        Choice,    ///< A choice is on the screen
        NoText,    ///< Nothing was said for a long while (a film, the credits, a menu)
        UserInput, ///< The player pressed a button of the game's
        Stopped,   ///< The player asked to stop
        GameGone,  ///< The game stopped or a savestate was loaded
    };

    /// Frames of the button held down, then frames let go, over and over. Each message stays
    /// up for some frames, long enough for a profile to see it whole and report it (quietly):
    /// that is how it is known that the game is still talking.
    static constexpr u64 HeldFrames = 2;
    static constexpr u64 ReleasedFrames = 6;
    /// Stops when no text at all came for this many frames (15 seconds of the game's time):
    /// short, because ○ pressed in a menu does what ○ does there (in a save menu, it saves).
    static constexpr u64 NoTextFrames = 60 * 15;

    /// Begins at `frame`, with the engine's counts of choices and texts as they are now.
    void Begin(u64 frame, u64 choices, u64 texts);

    struct Step {
        bool press{};            ///< The confirm button down on this frame
        std::optional<End> end;  ///< Set when it is over: let the button go
    };
    /// One frame of the game has passed: what to do now.
    Step OnFrame(u64 frame, u64 choices, u64 texts, bool user_input);
    /// Ends it from outside (the player, the game gone).
    void Cancel();

    [[nodiscard]] bool Running() const {
        return running;
    }
    /// Texts the game showed since Begin.
    [[nodiscard]] u64 Passed() const {
        return passed;
    }

private:
    bool running{};
    u64 started{};
    u64 choices_at_start{};
    u64 texts_at_start{};
    u64 last_text_frame{};
    u64 last_texts{};
    u64 passed{};
};

} // namespace Reader
