// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The computer's own sound, for the cards of a title read by a hook or OCR (the maintainer,
// 2026-11-10: "GSM uses OBS for this, is there a way to do it without OBS?"). On Windows it is what
// the default output plays, taken by WASAPI's loopback (no OBS, no driver), turned into the reader's
// audio history (shared/reader/core/audio_tap.h) as a core's sound is: interleaved stereo at its
// rate, the quiet between sounds filled in so that a line's place in it stays true to the clock.
// Everything playing is in it, not the game's alone. Elsewhere it takes nothing.

#pragma once

#include <atomic>
#include <thread>

namespace Home {

class Loopback {
public:
    static Loopback& Instance();
    ~Loopback();
    /// Begins taking the sound (once; again is nothing). False where it cannot.
    bool Start();
    void Stop();
    [[nodiscard]] bool Running() const {
        return running.load();
    }

private:
    void Run();
    std::thread worker;
    std::atomic<bool> running{false}, stop{false};
};

} // namespace Home
