// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The program's console.h: what the shared reader layer (shared/reader, which every fork builds)
// knows of the app and its console. A fork's console.h says it at compile time, for its one
// console; Furoshiki reads every console, so here the console's part is said at run time,
// by the core of the game that runs (cores/reader_core.h, reader_core_info), and set by
// Home::ApplyConsole (src/core_library.cpp) before that game starts. Only one game runs at a
// time, so one set of values serves.
//
// The names are a fork's console.h's. What is the program's own (its controller's buttons, as
// it reads every controller once) stays constant, as in a fork. The shared files the program
// builds use these only as values; one that wants a constant of the console's part (a
// `case`, a `constexpr`) does not build here and is changed to take the value at run time.
//
// The program builds the shared engine from its source (shared/reader/core) with this file
// and registers.h before the fork's on the include path (CMakeLists.txt, HOME_CORES).

namespace Reader::ConsoleTraits {

// ---- The console's part: set from the running core's description ----

inline const char* AppName = "Furoshiki";
inline const char* AppKey = "reader-home";
inline const char* MediaPrefix = "reader_home_";

inline const char* GameIdPattern = "[A-Za-z0-9-]{4,32}";
inline bool GameIdUpper = true;
inline const char* GamePage = "";
inline bool GamePageUpper = false;
inline bool GamePageWithoutHyphens = false;

inline const char* ScriptIndexUrl = "";
inline const char* ScriptSystem = "";

inline int ButtonYes = -1;
inline int ButtonUp = -1;
inline int ButtonDown = -1;
inline int ButtonLeft = -1;
inline int ButtonRight = -1;
inline int ButtonPressMs = 80;
inline int PickStepMs = 220;

inline float ScreenWidth = 1280.0f;
inline float ScreenHeight = 720.0f;
inline float LowerScreenWidth = 0.0f;

inline bool HasNextChoice = true; // the program runs any core to its next choice (reader_emu.cpp)

// ---- The program's own ----

/// What the program mixes a core's sound at for the reader (cards' audio): the core's own
/// rate is converted to it as the sound arrives, so that the audio history has one rate.
inline constexpr unsigned AudioSampleRate = 48000;

/// The program's controller (SDL's numbering and its Steam Controller bridge's, as a fork's
/// desktop app has them): the button that, held, moves the reader's cursor, and the one that
/// opens the Reader menu over the game. None of either by default: every button is the game's,
/// and the mouse and the Steam Controller's pad point without one (the user, 2026-10-31).
inline constexpr int CursorButtonDefault = -1;
inline constexpr int MenuButtonDefault = -1;

inline constexpr int PadUp = 19;
inline constexpr int PadDown = 20;
inline constexpr int PadLeft = 21;
inline constexpr int PadRight = 22;
inline constexpr int PadYes = 96;
inline constexpr int PadNo = 97;
inline constexpr int PadLeftStick = 106;
inline constexpr int PadRightStick = 107;

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

/// Made-up game IDs the shared tests use.
namespace Test {
inline constexpr char Game[] = "TEST-99001";
inline constexpr char OtherGame[] = "TEST-99002";
inline constexpr char UnknownGame[] = "TEST-99999";
inline constexpr char ScriptGame[] = "TEST-99998";
inline constexpr char OtherScriptGame[] = "TEST-99997";
inline constexpr char ForeignId[] = "0100d7f00fb1a000";
} // namespace Test

} // namespace Reader::ConsoleTraits
