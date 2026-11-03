// SPDX-FileCopyrightText: PPSSPP Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The console's own look of the reader's popup and pointer, on the desktop. This file and
// reader_look.cpp are the fork's own (the rest of the reader layer is shared: see the Flycast
// repository's shared/); the Android app's twin is ui/ConsoleLook.kt with
// ui/ReaderStyle.kt. Change all of them together.

#include <QColor>

namespace ReaderUi::Style {

// The popup is the console's own, where the rest are the Android app's:
// the PSP's cross-media bar: a blue band with the wave through it, white type, the headword aglow.
inline const QColor PopupText{0xFF, 0xFF, 0xFF};
inline const QColor PopupDim{0xD3, 0xE4, 0xF8};
inline const QColor PopupAccent{0xFF, 0xFF, 0xFF};
/// The plus that makes a card and the book of a word Anki has, in their boxes.
inline const QColor PopupSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBookSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBox{0x1E, 0x46, 0x96};
inline const QColor PopupBoxPressed{0x3C, 0x78, 0xD2};
inline const QColor PopupEdge{0xFF, 0xFF, 0xFF, 0x99};

} // namespace ReaderUi::Style
