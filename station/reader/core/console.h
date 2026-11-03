// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// What the reader layer, which every reader fork shares (the Flycast Reader fork's
// the repository's shared/), needs to know of this app and its console. This file is the
// fork's own: each fork has one, with the same names. The Android app's twin is its
// reader/Console.kt.

namespace Reader::ConsoleTraits {

/// The app's name, and its key: Reader Home's file, the statistics record, the Anki tag.
inline constexpr char AppName[] = "Station Reader";
inline constexpr char AppKey[] = "station-reader";
/// What the files the app gives Anki begin with ("station_reader_1791…jpg").
inline constexpr char MediaPrefix[] = "station_reader_";

/// A game's ID as its disc gives it, and as a game script is named: the disc's serial, four
/// capitals, a hyphen and five digits ("SLPS-01794"). A regular expression of the common
/// kind (Qt's and Kotlin's).
inline constexpr char GameIdPattern[] = "[A-Z]{4}-[0-9]{5}";
/// Whether an ID is written in capitals (true) or small letters (false) where it is compared.
inline constexpr bool GameIdUpper = true;
/// The game's page for a card's {url}, with %1 for its ID; how the ID is written in it.
inline constexpr char GamePage[] = "https://www.jp.playstation.com/software/title/%1.html";
inline constexpr bool GamePageUpper = false;
/// Whether the ID's hyphens are left out in the page's address ("slps01794").
inline constexpr bool GamePageWithoutHyphens = true;

/// The published list of game scripts, and the system it names: Furoshiki's repository carries
/// the PlayStation's (tools/public-tree.py makes it; nothing is there until it is published).
inline constexpr char ScriptIndexUrl[] =
    "https://raw.githubusercontent.com/sommepo/furoshiki/main/station/reader/profiles/index.json";
inline constexpr char ScriptSystem[] = "ps1";

/// The first controller's buttons the reader presses, by the emulator's numbers
/// (DigitalController::Button, as Reader::Emu::PressButton takes them): going on (circle, yes
/// on a Japanese console), and the four ways of the d-pad.
inline constexpr int ButtonYes = 13;
inline constexpr int ButtonUp = 4;
inline constexpr int ButtonDown = 6;
inline constexpr int ButtonLeft = 7;
inline constexpr int ButtonRight = 5;
/// How long a button is held when the reader presses it (a PS1 game reads its pad once a
/// frame), and the time between the presses that move the game's cursor to an answer (ms).
inline constexpr int ButtonPressMs = 100;
inline constexpr int PickStepMs = 250;

/// The desktop app's controller buttons at first (as its Steam Controller bridge and SDL
/// number them; -1 none): the one that, held, moves the reader's cursor, and the one that
/// opens the Reader menu over the game.
inline constexpr int CursorButtonDefault = 104;
inline constexpr int MenuButtonDefault = -1;

/// The controller's buttons as the reader's input filter hears them (InputFilter::OnButton; the
/// numbers of CursorButtonDefault, Android's key codes): the d-pad, going on and going back,
/// and the two sticks pressed. While one of the reader's windows has the controller (its menu
/// button opened it) the d-pad moves through it; a stick pressed clicks where the reader's
/// cursor is.
inline constexpr int PadUp = 19;
inline constexpr int PadDown = 20;
inline constexpr int PadLeft = 21;
inline constexpr int PadRight = 22;
inline constexpr int PadYes = 96;
inline constexpr int PadNo = 97;
inline constexpr int PadLeftStick = 106;
inline constexpr int PadRightStick = 107;

/// The buttons the desktop's Reader settings offer for the reader's own (the cursor's, the
/// statistics', the menu's), in the order listed; "None" comes before them. `menu_only`:
/// offered for the menu's button alone (one no game is told of).
struct ReaderButton {
    const char* name;
    int number;
    bool menu_only;
};
inline constexpr ReaderButton ReaderButtons[] = {
    {"L2", 104, false},
    {"R2", 105, false},
    {"L1", 102, false},
    {"R1", 103, false},
    {"Left stick, pressed", 106, false},
    {"Right stick, pressed", 107, false},
    {"Select", 109, false},
    {"Start", 108, false},
};

/// The game's screen in the units a profile's layout uses unless it says otherwise, and the
/// width of a second (lower) screen where the console has one (0: none).
inline constexpr float ScreenWidth = 1280.0f;
inline constexpr float ScreenHeight = 720.0f;
inline constexpr float LowerScreenWidth = 0.0f;

/// What the console mixes its sound at (the reader keeps it for cards' audio).
inline constexpr unsigned AudioSampleRate = 44100;
/// Whether the emulator can run a game on to its next choice (reader/core/next_choice.h).
inline constexpr bool HasNextChoice = false;

/// Made-up game IDs the tests use: of the console's form, none of a real game.
namespace Test {
inline constexpr char Game[] = "SLPS-69001";
inline constexpr char OtherGame[] = "SLPS-69002";
inline constexpr char UnknownGame[] = "SLPS-69999";
inline constexpr char ScriptGame[] = "SLPS-99999";
inline constexpr char OtherScriptGame[] = "SLPS-99998";
/// An ID of another console's form, which this one's is not.
inline constexpr char ForeignId[] = "0100d7f00fb1a000";
} // namespace Test

} // namespace Reader::ConsoleTraits
