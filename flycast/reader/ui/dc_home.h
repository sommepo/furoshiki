// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The app's main screen in the manner of the Dreamcast's own menu, and the console's colours
// for the emulator's other screens. Drawn with the emulator's own interface library, so the
// same screens are the Windows, Linux and Android apps'. Nothing here is the console's own:
// no picture, sound or logo of it is used.

#include <string>

#include <vector>

namespace ReaderHome {

/// Whether the Dreamcast-style menu stands in for the emulator's game list.
bool Enabled();
void SetEnabled(bool enabled);
/// Draws the menu for this frame, in place of the emulator's game list.
void Draw();
/// Gives the emulator's interface the console's colours when they are wanted; true when it
/// did. Called where the emulator applies its own theme.
bool ApplyTheme();
/// Whether the console's colours are wanted in the emulator's other screens.
bool LookEnabled();
void SetLookEnabled(bool enabled);

/// A game in the user's folders whose text can be read.
struct ReadableGame {
    std::string id, title, path, cover;
};
/// Those games as far as they are known now: the folders are looked through in the
/// background, and a disc's product number is read a little after the disc is found.
std::vector<ReadableGame> ReadableGames();

} // namespace ReaderHome
