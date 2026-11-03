// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace Reader {

/// The serial a game's own memory card is kept under: for a later disc of a game on several
/// discs, the first disc's ("SLPS-00701" -> "SLPS-00700"), so that every disc sees the card
/// the first one wrote; any other serial as it is. From DuckStation's game database
/// (disc_sets.inc, written by tools/reader/disc-sets.py).
std::string CardSerialOf(std::string_view serial);

} // namespace Reader
