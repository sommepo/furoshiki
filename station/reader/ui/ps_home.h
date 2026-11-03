// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The app's home in the manner of the PlayStation's own system screen (its memory card
// manager and CD player), and the console's colours for the emulator's other screens. Drawn
// with the emulator's own interface library (Dear ImGui, in DuckStation's full-screen
// interface, src/frontend-common/fullscreen_ui.cpp). Nothing here is the console's own: no
// logo, picture, sound or font of it is used; every shape is drawn from lines and circles.

#include <string>
#include <vector>

#include "imgui.h"

struct GameListEntry;

namespace ReaderHome {

/// Whether the PlayStation-style home stands in for DuckStation's start page (on unless
/// switched off: [Reader] PlayStationHome in the emulator's settings).
bool Enabled();
/// Whether the emulator's other screens wear the console's colours ([Reader] PlayStationLook).
bool LookEnabled();

/// What the home asks the emulator's interface to do.
enum class Do {
    Nothing,
    Start,      ///< start `path`
    StartFile,  ///< choose a disc image
    StartBios,  ///< the console without a disc
    Resume,     ///< from where it was last closed
    LoadState,  ///< choose a savestate
    Settings,   ///< the emulator's settings, at `page` (FullscreenUI::SettingsPage)
    Refresh,    ///< look through the game folders again
    Fullscreen, ///< full screen or a window
    Exit,
};
struct Request {
    Do what = Do::Nothing;
    std::string path;
    int page = 0;
};

/// A cover picture for a game, or null when there is none (the home draws a disc then).
using CoverLookup = ImTextureID (*)(const GameListEntry* entry);

/// Draws the home for this frame over the whole window and says what was chosen. `games` is
/// the emulator's game list as it sorts it; `ready` is false while the folders are still
/// being looked through; `active` is false while a window of the emulator's own lies on top.
Request Draw(const std::vector<const GameListEntry*>& games, bool ready, CoverLookup cover, bool active);

/// Whether the home has left its start-up screen (the game list is wanted from then on).
bool PastStartUp();

/// Called once a frame before the emulator's interface draws: reads the switches and gives
/// Dear ImGui's own colours the console's look while that is wanted.
void Refresh();

/// The colours of DuckStation's full-screen interface (imgui_fullscreen.h, fullscreen_ui.cpp):
/// the console's look's in place of `upstream` while that is wanted.
enum Ui {
    Primary,
    PrimaryLight,
    PrimaryDark,
    PrimaryText,
    PrimaryDisabledText,
    TextHighlight,
    PrimaryLine,
    Secondary,
    SecondaryLight,
    SecondaryDark,
    SecondaryText,
    Column, ///< the ground of a page's side column
};
ImVec4 Colour(Ui which, const ImVec4& upstream);

/// The app's emblem (a disc, as the reader's pointer is) where DuckStation showed its duck.
void DrawEmblem(ImDrawList* draw, const ImVec2& centre, float radius);

} // namespace ReaderHome
