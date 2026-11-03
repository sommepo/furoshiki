// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "reader/core/disc_sets.h"

#include <algorithm>
#include <iterator>

namespace Reader {

namespace {

struct LaterDisc {
    std::string_view later;
    std::string_view first;
};

constexpr LaterDisc kLaterDiscs[] = {
#include "reader/core/disc_sets.inc"
};

} // namespace

std::string CardSerialOf(std::string_view serial) {
    const auto found = std::lower_bound(std::begin(kLaterDiscs), std::end(kLaterDiscs), serial,
                                        [](const LaterDisc& disc, std::string_view key) { return disc.later < key; });
    return std::string(found != std::end(kLaterDiscs) && found->later == serial ? found->first : serial);
}

} // namespace Reader
