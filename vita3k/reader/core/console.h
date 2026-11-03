// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What the reader layer, which every reader fork shares (the repository's shared/), needs to
// know of this core and its console. This file is the fork's own: each fork has one, with the
// same names. This fork has only a core, so the desktop app's values have no place here.

namespace Reader::ConsoleTraits {

/// The app's name, and its key: Furoshiki's file, the statistics record, the Anki tag.
inline constexpr char AppName[] = "Vita3K Reader";
inline constexpr char AppKey[] = "vita3k-reader";
/// What the files given to Anki begin with ("vita3k_reader_1791…jpg").
inline constexpr char MediaPrefix[] = "vita3k_reader_";

/// A game's ID, and how a game script is named: its title ID as the Vita writes it, four
/// capitals and five digits ("PCSG00123"). A regular expression of the common kind.
inline constexpr char GameIdPattern[] = "[A-Z]{4}[0-9]{5}";
/// Whether an ID is written in capitals (true) or small letters (false) where it is compared.
inline constexpr bool GameIdUpper = true;
/// The game's page for a card's {url}, with %1 for its ID; how the ID is written in it.
inline constexpr char GamePage[] = "";
inline constexpr bool GamePageUpper = false;
/// Whether the ID's hyphens are left out in the page's address.
inline constexpr bool GamePageWithoutHyphens = false;

/// The published list of game scripts, and the system it names: none yet.
inline constexpr char ScriptIndexUrl[] = "";
inline constexpr char ScriptSystem[] = "playstationvita";

/// The console's buttons the reader presses, by their bit in the Vita's own button word
/// (SCE_CTRL_*): going on (circle, as on a Japanese Vita), and the four ways of the d-pad.
inline constexpr int ButtonYes = 13;
inline constexpr int ButtonUp = 4;
inline constexpr int ButtonDown = 6;
inline constexpr int ButtonLeft = 7;
inline constexpr int ButtonRight = 5;
/// How long a button is held when the reader presses it, and the time between the presses
/// that move the game's cursor to an answer (ms).
inline constexpr int ButtonPressMs = 80;
inline constexpr int PickStepMs = 220;

/// The console's screen in a profile's units: its own pixels.
inline constexpr float ScreenWidth = 960.0f;
inline constexpr float ScreenHeight = 544.0f;
inline constexpr float LowerScreenWidth = 0.0f;

/// What the core hands its sound over at: the Vita's own output rate.
inline constexpr unsigned AudioSampleRate = 48000;
/// Whether the emulator can run a game on to its next choice (reader/core/next_choice.h).
inline constexpr bool HasNextChoice = false;

/// Made-up game IDs the tests use: of the console's form, none of a real game.
namespace Test {
inline constexpr char Game[] = "PCSG99001";
inline constexpr char OtherGame[] = "PCSG99002";
inline constexpr char UnknownGame[] = "PCSG99999";
inline constexpr char ScriptGame[] = "PCSG98999";
inline constexpr char OtherScriptGame[] = "PCSG98998";
/// An ID of another console's form, which this one's is not.
inline constexpr char ForeignId[] = "HDR-0164";
} // namespace Test

} // namespace Reader::ConsoleTraits
