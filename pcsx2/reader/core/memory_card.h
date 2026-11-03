// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// A game's own memory card, as the app (reader/core/emulator.cpp) and the core of Reader Home
// (reader/core_library/pcsx2_core.cpp) make it.

#include <string>

namespace Reader {

/// Formats a blank 8 MB card file (just made by FileMcd_CreateNewCard) as the console does.
/// The emulator's own new cards are blank, which most games format when they first save; some
/// do not (CLANNAD only says the card is not formatted), and with a card of the game's own
/// there is no other card to format it in.
bool FormatCard(const std::string& path);

} // namespace Reader
