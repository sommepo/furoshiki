// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// The console's own look of the reader's popup and pointer, on the desktop. This file and
// reader_look.cpp are the fork's own (the rest of the reader layer is shared: see the Flycast
// repository's shared/); ARMSX2 Reader's twin is ui/ConsoleLook.kt with
// ui/ReaderStyle.kt. Change all of them together.

#include <QColor>

namespace ReaderUi::Style {

// The popup is the console's own, where the rest are the Android app's:
// the PS2's system menu: black-indigo glass, a thin light-blue edge, the headword lit from within.
inline const QColor PopupText{0xFF, 0xFF, 0xFF};
inline const QColor PopupDim{0x7F, 0xA6, 0xD6};
inline const QColor PopupAccent{0x9F, 0xD0, 0xFF};
/// The plus that makes a card and the book of a word Anki has, in their boxes.
inline const QColor PopupSign{0x9F, 0xD0, 0xFF};
inline const QColor PopupBookSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBox{0x0B, 0x12, 0x2E};
inline const QColor PopupBoxPressed{0x1F, 0x33, 0x6B};
inline const QColor PopupEdge{0x7F, 0xC4, 0xFF, 0x8C};

} // namespace ReaderUi::Style
