// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// What the reader layer, which every reader fork shares (the Flycast Reader fork's
// the repository's shared/), needs to know of this app and its console. This file is the
// fork's own: each fork has one, with the same names. The Android app's twin is
// reader/Console.kt.

namespace Reader::ConsoleTraits {

/// The app's name, and its key: Reader Home's file, the statistics record, the Anki tag.
inline constexpr char AppName[] = "Azahar Reader";
inline constexpr char AppKey[] = "azahar-reader";
/// What the files the app gives Anki begin with ("azahar_reader_1791…jpg").
inline constexpr char MediaPrefix[] = "azahar_reader_";

/// A game's ID, and how a game script is named: the program's title ID, sixteen hexadecimal
/// digits in capitals ("0004000000088A00"). A regular expression of the common kind.
inline constexpr char GameIdPattern[] = "[0-9A-F]{16}";
/// Whether an ID is written in capitals (true) or small letters (false) where it is compared.
inline constexpr bool GameIdUpper = true;
/// The game's page for a card's {url}, with %1 for its ID; how the ID is written in it.
inline constexpr char GamePage[] = "";
inline constexpr bool GamePageUpper = false;
/// Whether the ID's hyphens are left out in the page's address.
inline constexpr bool GamePageWithoutHyphens = false;

/// The published list of game scripts, and the system it names: Furoshiki's repository carries
/// the 3DS's (tools/public-tree.py makes it; nothing is there until it is published).
inline constexpr char ScriptIndexUrl[] =
    "https://raw.githubusercontent.com/sommepo/furoshiki/main/azahar/src/android/app/src/main/assets/reader/profiles/index.json";
inline constexpr char ScriptSystem[] = "3ds";

/// The console's buttons the reader presses, by the emulator's numbers on Android
/// (NativeLibrary.ButtonType): going on (A), and the four ways of the d-pad.
inline constexpr int ButtonYes = 700;
inline constexpr int ButtonUp = 709;
inline constexpr int ButtonDown = 710;
inline constexpr int ButtonLeft = 711;
inline constexpr int ButtonRight = 712;
/// How long a button is held when the reader presses it, and the time between the presses
/// that move the game's cursor to an answer (ms).
inline constexpr int ButtonPressMs = 80;
inline constexpr int PickStepMs = 220;

/// The desktop app's controller buttons at first, by the emulator's numbers there
/// (Settings::NativeButton; -1 none): the one that, held, moves the reader's cursor (ZL, as
/// on Android), and the one that opens the Reader menu over the game.
inline constexpr int CursorButtonDefault = 14;
inline constexpr int MenuButtonDefault = -1;

/// The controller's buttons as the reader's input filter hears them on a computer
/// (InputFilter::OnButton; NativeButton numbers, as CursorButtonDefault's): the d-pad, going
/// on (A) and going back (B), and the two sticks pressed (none on the 3DS). While one of the
/// reader's windows has the controller (its menu button opened it) the d-pad moves through it.
inline constexpr int PadUp = 4;
inline constexpr int PadDown = 5;
inline constexpr int PadLeft = 6;
inline constexpr int PadRight = 7;
inline constexpr int PadYes = 0;
inline constexpr int PadNo = 1;
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
    {"ZL", 14, false}, {"ZR", 15, false},
    {"L", 8, false}, {"R", 9, false},       {"X", 2, false},
    {"Y", 3, false}, {"Select", 11, false}, {"Start", 10, false},
};

/// The console's upper screen, which a profile's layout is on unless it says otherwise, and its
/// lower screen's width, in the screens' own pixels.
inline constexpr float ScreenWidth = 400.0f;
inline constexpr float ScreenHeight = 240.0f;
inline constexpr float LowerScreenWidth = 320.0f;

/// What the console mixes its sound at (AudioCore::native_sample_rate).
inline constexpr unsigned AudioSampleRate = 32728;
/// Whether the emulator can run a game on to its next choice (reader/core/next_choice.h).
inline constexpr bool HasNextChoice = false;

/// Made-up game IDs the tests use: of the console's form, none of a real game.
namespace Test {
inline constexpr char Game[] = "0004000000099001";
inline constexpr char OtherGame[] = "0004000000099002";
inline constexpr char UnknownGame[] = "0004000000099999";
inline constexpr char ScriptGame[] = "0004000000098999";
inline constexpr char OtherScriptGame[] = "0004000000098998";
/// An ID of another console's form, which this one's is not.
inline constexpr char ForeignId[] = "HDR-0164";
} // namespace Test

} // namespace Reader::ConsoleTraits
