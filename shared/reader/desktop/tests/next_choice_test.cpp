// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PCSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "reader/core/next_choice.h"

using Reader::NextChoice;

TEST_CASE("next choice presses the button in turns until a choice comes", "[next_choice]") {
    NextChoice next;
    next.Begin(100, 3, 40);
    REQUIRE(next.Running());
    // Held for some frames, let go for some, held again...
    constexpr u64 cycle = NextChoice::HeldFrames + NextChoice::ReleasedFrames;
    for (u64 at = 0; at < cycle; ++at) {
        CHECK(next.OnFrame(100 + at, 3, 40 + at / 4, false).press == (at < NextChoice::HeldFrames));
    }
    CHECK(next.OnFrame(100 + cycle, 3, 42, false).press);
    CHECK(next.Passed() == 2);

    const NextChoice::Step step = next.OnFrame(101 + cycle, 4, 43, false);
    REQUIRE(step.end == NextChoice::End::Choice);
    CHECK_FALSE(step.press);
    CHECK_FALSE(next.Running());
    CHECK(next.Passed() == 3);
    // Over: nothing more is pressed.
    CHECK_FALSE(next.OnFrame(102 + cycle, 4, 43, false).press);
    CHECK_FALSE(next.OnFrame(102 + cycle, 4, 43, false).end);
}

TEST_CASE("next choice stops at a press of the player's", "[next_choice]") {
    NextChoice next;
    next.Begin(0, 0, 0);
    CHECK_FALSE(next.OnFrame(1, 0, 0, false).end);
    CHECK(next.OnFrame(2, 0, 0, true).end == NextChoice::End::UserInput);
    CHECK_FALSE(next.Running());
}

TEST_CASE("next choice stops when nothing is said for long", "[next_choice]") {
    NextChoice next;
    next.Begin(10, 0, 5);
    // Text now and then keeps it going.
    CHECK_FALSE(next.OnFrame(10 + NextChoice::NoTextFrames - 1, 0, 5, false).end);
    CHECK_FALSE(next.OnFrame(10 + NextChoice::NoTextFrames, 0, 6, false).end);
    const u64 last = 10 + NextChoice::NoTextFrames;
    CHECK_FALSE(next.OnFrame(last + NextChoice::NoTextFrames - 1, 0, 6, false).end);
    CHECK(next.OnFrame(last + NextChoice::NoTextFrames, 0, 6, false).end == NextChoice::End::NoText);
}

TEST_CASE("next choice can be cancelled", "[next_choice]") {
    NextChoice next;
    next.Begin(0, 0, 0);
    next.Cancel();
    CHECK_FALSE(next.Running());
    CHECK_FALSE(next.OnFrame(1, 1, 1, false).end);
}
