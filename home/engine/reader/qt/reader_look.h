// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The console's own look of the reader's popup and pointer: in a fork this file and
// reader_look.cpp are the fork's own, for its one console. The program reads every console,
// so the colours are chosen at run time, by the running core's console (SetConsoleLook, from
// Home::ApplyConsole's caller): the PlayStation's is Station Reader's, the Dreamcast's Flycast
// Reader's (each fork's reader/qt/reader_look.*); any other console has the program's plain look. The Android
// apps' twins are each fork's ui/ConsoleLook.kt with ui/ReaderStyle.kt.

#include <QColor>

namespace ReaderUi::Style {

// The popup's colours, as the shared windows read them.
inline QColor PopupText{0x1C, 0x22, 0x33};
inline QColor PopupDim{0x66, 0x6E, 0x82};
inline QColor PopupAccent{0x2F, 0x5B, 0xC8};
/// The plus that makes a card and the book of a word Anki has, in their boxes.
inline QColor PopupSign{0xFF, 0xFF, 0xFF};
inline QColor PopupBookSign{0xFF, 0xFF, 0xFF};
inline QColor PopupBox{0x2F, 0x5B, 0xC8};
inline QColor PopupBoxPressed{0x1E, 0x3C, 0x8A};
inline QColor PopupEdge{0xB4, 0xBC, 0xCC};

/// Makes the look of the console of this key ("ps1") the popup's and pointer's from now on.
void SetConsoleLook(const char* key);

} // namespace ReaderUi::Style
