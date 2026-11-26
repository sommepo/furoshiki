// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The numbers behind the Statistics page's goal soroban (the maintainer, 2026-11-26: "soroban is
// brilliant, make that"): a goal as three digits on three rods, tens, ones and tenths of its unit
// (thousands of characters a day, millions a year). A rod has four beads worth one and, left of
// the bar, one worth five; the daily goal's last rod has only the five (steps of 500), the yearly
// goal's first only three ones (30 million at most). No Qt here, so the tests can use it.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace GoalAbacus {

struct Rod {
    int earth;   ///< Beads worth one
    bool heaven; ///< Whether it has the bead worth five
    int cap;     ///< The highest digit it shows
};

struct Kind {
    int most;                ///< The highest goal
    int step;                ///< The smallest change
    int unit;                ///< What the middle rod counts
    std::array<Rod, 3> rods; ///< Tens, ones, tenths of the unit
};

inline constexpr Kind Daily{50000, 500, 1000, {{{4, true, 5}, {4, true, 9}, {0, true, 5}}}};
inline constexpr Kind Yearly{30000000, 100000, 1000000, {{{3, false, 3}, {4, true, 9}, {4, true, 9}}}};

/// The goal on the kind's own steps, within what it can show.
inline int Snap(const Kind& kind, long long goal) {
    const long long stepped = (goal + kind.step / 2) / kind.step * kind.step;
    return static_cast<int>(std::clamp<long long>(stepped, 0, kind.most));
}

/// The goal's three digits: tens, ones and tenths of the unit.
inline std::array<int, 3> Digits(const Kind& kind, int goal) {
    const long long tenths = std::llround(goal * 10.0 / kind.unit);
    return {static_cast<int>(tenths / 100), static_cast<int>(tenths / 10 % 10), static_cast<int>(tenths % 10)};
}

/// What one bead worth one of a rod adds; the daily goal's last rod has only its five.
inline int Place(const Kind& kind, int rod) {
    if (rod == 0) {
        return kind.unit * 10;
    }
    if (rod == 1) {
        return kind.unit;
    }
    return kind.rods[2].earth == 0 ? kind.unit / 2 : kind.unit / 10;
}

/// The goal a bead further (or back) on a rod, carrying into the next; it stops at the ends.
inline int Stepped(const Kind& kind, int goal, int rod, int by) {
    return Snap(kind, static_cast<long long>(goal) + static_cast<long long>(by) * Place(kind, rod));
}

/// The goal with one rod set to a digit, the others as they are.
inline int WithDigit(const Kind& kind, int goal, int rod, int digit) {
    std::array<int, 3> digits = Digits(kind, goal);
    digits[static_cast<std::size_t>(rod)] = std::clamp(digit, 0, kind.rods[static_cast<std::size_t>(rod)].cap);
    const long long tenths = digits[0] * 100LL + digits[1] * 10LL + digits[2];
    return Snap(kind, tenths * kind.unit / 10);
}

/// The next round figure above the goal (by > 0) or below it; the goal itself at either end.
inline int Round(const Kind& kind, int goal, int by) {
    static constexpr int DailyRound[] = {0, 5000, 10000, 15000, 20000, 25000, 30000, 40000, 50000};
    static constexpr int YearlyRound[] = {0, 500000, 1000000, 2000000, 3000000, 5000000, 8000000, 10000000, 15000000, 20000000, 30000000};
    const int* list = kind.unit == Daily.unit ? DailyRound : YearlyRound;
    const int count = static_cast<int>(kind.unit == Daily.unit ? std::size(DailyRound) : std::size(YearlyRound));
    if (by > 0) {
        for (int i = 0; i < count; ++i) {
            if (list[i] > goal) {
                return list[i];
            }
        }
        return goal;
    }
    for (int i = count - 1; i >= 0; --i) {
        if (list[i] < goal) {
            return list[i];
        }
    }
    return goal;
}

} // namespace GoalAbacus
