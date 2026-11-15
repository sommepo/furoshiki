// SPDX-FileCopyrightText: melonDS Reader fork contributors (from the Azahar Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What the reader layer, which every reader fork shares (the Flycast Reader fork's
// the repository's shared/), needs to know of this app and its console. This file is the
// fork's own: each fork has one, with the same names. melonDS Reader is a core alone
// (reader/core_library/melonds_core.cpp hands these to Furoshiki); its buttons are libretro's
// joypad bits (RETRO_DEVICE_ID_JOYPAD_*), as melonDS's libretro build reads them.

namespace Reader::ConsoleTraits {

/// The app's name, and its key: Reader Home's file, the statistics record, the Anki tag.
inline constexpr char AppName[] = "melonDS Reader";
inline constexpr char AppKey[] = "melonds-reader";
/// What the files the app gives Anki begin with ("melonds_reader_1791…jpg").
inline constexpr char MediaPrefix[] = "melonds_reader_";

/// A game's ID, and how a game script is named: the cartridge's game code, four capitals or
/// digits from its header ("AWIJ": Wish Room). A regular expression of the common kind.
inline constexpr char GameIdPattern[] = "[A-Z0-9]{4}";
/// Whether an ID is written in capitals (true) or small letters (false) where it is compared.
inline constexpr bool GameIdUpper = true;
/// The game's page for a card's {url}, with %1 for its ID; how the ID is written in it.
inline constexpr char GamePage[] = "";
inline constexpr bool GamePageUpper = false;
/// Whether the ID's hyphens are left out in the page's address.
inline constexpr bool GamePageWithoutHyphens = false;

/// The published list of game scripts, and the system it names: Furoshiki's repository carries
/// the DS's (tools/public-tree.py makes it; nothing is there until it is published).
inline constexpr char ScriptIndexUrl[] =
    "https://raw.githubusercontent.com/sommepo/furoshiki/main/melonds/reader/profiles/index.json";
inline constexpr char ScriptSystem[] = "nds";

/// The console's buttons the reader presses, by libretro's joypad bits: going on (A, 8), and the
/// four ways of the d-pad (up 4, down 5, left 6, right 7).
inline constexpr int ButtonYes = 8;
inline constexpr int ButtonUp = 4;
inline constexpr int ButtonDown = 5;
inline constexpr int ButtonLeft = 6;
inline constexpr int ButtonRight = 7;
/// How long a button is held when the reader presses it, and the time between the presses
/// that move the game's cursor to an answer (ms).
inline constexpr int ButtonPressMs = 80;
inline constexpr int PickStepMs = 220;

/// The desktop app's controller buttons at first (libretro's bits; -1 none): the one that,
/// held, moves the reader's cursor (L2, which the DS has not), and the one that opens the
/// Reader menu over the game.
inline constexpr int CursorButtonDefault = 12;
inline constexpr int MenuButtonDefault = -1;

/// The controller's buttons as the reader's input filter hears them on a computer
/// (InputFilter::OnButton; libretro's bits, as CursorButtonDefault's): the d-pad, going on (A)
/// and going back (B), and the two sticks pressed (none on the DS). While one of the
/// reader's windows has the controller (its menu button opened it) the d-pad moves through it.
inline constexpr int PadUp = 4;
inline constexpr int PadDown = 5;
inline constexpr int PadLeft = 6;
inline constexpr int PadRight = 7;
inline constexpr int PadYes = 8;
inline constexpr int PadNo = 0;
inline constexpr int PadLeftStick = -1;
inline constexpr int PadRightStick = -1;

/// The buttons the desktop's Reader settings offer for the reader's own (the cursor's, the
/// statistics', the menu's), in the order listed; "None" comes before them. `menu_only`:
/// offered for the menu's button alone (one no game is told of).
struct ReaderButton {
    const char* name;
    int number;
    bool menu_only;
};
inline constexpr ReaderButton ReaderButtons[] = {
    {"L2", 12, false}, {"R2", 13, false},
    {"L", 10, false},  {"R", 11, false},     {"X", 9, false},
    {"Y", 1, false},   {"Select", 2, false}, {"Start", 3, false},
};

/// The console's upper screen, which a profile's layout is on unless it says otherwise, and its
/// lower screen's width, in the screens' own pixels.
inline constexpr float ScreenWidth = 256.0f;
inline constexpr float ScreenHeight = 192.0f;
inline constexpr float LowerScreenWidth = 256.0f;

/// What melonDS's libretro build hands its sound over at (32768 Hz).
inline constexpr unsigned AudioSampleRate = 32768;
/// Whether the emulator can run a game on to its next choice (reader/core/next_choice.h).
inline constexpr bool HasNextChoice = false;

/// Made-up game IDs the tests use: of the console's form, none of a real game.
namespace Test {
inline constexpr char Game[] = "ZZ1J";
inline constexpr char OtherGame[] = "ZZ2J";
inline constexpr char UnknownGame[] = "ZZ9J";
inline constexpr char ScriptGame[] = "ZY9J";
inline constexpr char OtherScriptGame[] = "ZY8J";
/// An ID of another console's form, which this one's is not.
inline constexpr char ForeignId[] = "HDR-0164";
} // namespace Test

} // namespace Reader::ConsoleTraits
