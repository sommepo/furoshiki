// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The console's own look of the reader's popup and pointer, on the desktop. This file and
// reader_look.cpp are the fork's own (the rest of the reader layer is shared: see
// the repository's shared/); the Android app's twin is ui/ConsoleLook.kt with ui/ReaderStyle.kt.
// Change all of them together.

#include <QColor>

namespace ReaderUi::Style {

// The popup is the console's own, where the rest are the Android app's: plain white with a
// faint orange spiral in its corner, dark blue type, orange for the plus and the book.
inline const QColor PopupText{0x1F, 0x3F, 0x8F};
inline const QColor PopupDim{0x6B, 0x82, 0xB5};
inline const QColor PopupAccent{0xD8, 0x62, 0x0A};
/// The plus that makes a card and the book of a word Anki has, in their boxes.
inline const QColor PopupSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBookSign{0xFF, 0xFF, 0xFF};
inline const QColor PopupBox{0xF2, 0x7C, 0x1E};
inline const QColor PopupBoxPressed{0xD8, 0x62, 0x0A};
inline const QColor PopupEdge{0xC9, 0xD6, 0xEC};

} // namespace ReaderUi::Style
