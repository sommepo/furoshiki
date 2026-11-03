// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// The console's own look of the reader's popup and pointer, on the desktop. This file and
// reader_look.cpp are the fork's own (the rest of the reader layer is shared: see the Flycast
// repository's shared/); the Android app's twin is ui/ConsoleLook.kt with
// ui/ReaderStyle.kt. Change all of them together.

#include <QColor>

namespace ReaderUi::Style {

// The popup is the console's own, where the rest are the Android app's:
// the 3DS's lower-screen panels: dark grey pinstripes, white type, light bevelled buttons.
inline const QColor PopupText{0xF3, 0xF5, 0xF7};
inline const QColor PopupDim{0xC2, 0xC8, 0xCF};
inline const QColor PopupAccent{0x9F, 0xD8, 0xFF};
/// The plus that makes a card and the book of a word Anki has, in their boxes.
inline const QColor PopupSign{0x2A, 0x2F, 0x35};
inline const QColor PopupBookSign{0x2A, 0x2F, 0x35};
inline const QColor PopupBox{0xE4, 0xE8, 0xEC};
inline const QColor PopupBoxPressed{0xB8, 0xBE, 0xC5};
inline const QColor PopupEdge{0x1D, 0x20, 0x24};

} // namespace ReaderUi::Style
