// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The console's own look of the reader's popup and pointer, on the desktop. This file and
// reader_look.cpp are the fork's own (the rest of the reader layer is shared: see the Flycast
// repository's shared/); the Android app's twin is ui/ConsoleLook.kt with
// ui/ReaderStyle.kt. Change all of them together.

#include <QColor>

namespace ReaderUi::Style {

// The popup is the console's own, where the rest are the Android app's: the light grey of the
// console's case and of its system menu's panels, dark ink, the deep blue of its menus for
// what stands out.
inline const QColor PopupText{0x23, 0x24, 0x2B};
inline const QColor PopupDim{0x6B, 0x6C, 0x74};
inline const QColor PopupAccent{0x2B, 0x3A, 0x8C};
/// The plus that makes a card and the book of a word Anki has, in their boxes.
inline const QColor PopupSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBookSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBox{0x2B, 0x3A, 0x8C};
inline const QColor PopupBoxPressed{0x1A, 0x24, 0x5C};
inline const QColor PopupEdge{0x9C, 0x9B, 0x95};

} // namespace ReaderUi::Style
