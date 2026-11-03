// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The reader apps' own folders, for the cores that play their consoles here: a core keeps its
// saves where the console's app keeps them (and takes its BIOS, firmware or keys from there
// when the program's own system folder has none), so that the saves made in either are the
// same files, and the app's save sync carries them as before. Nothing is copied: a folder is
// used only where it is there already.

#include <QString>

namespace Home::AppFolders {

struct Folders {
    QString saves;  ///< The saves folder the core is to be given; empty: the program's own
    QString system; ///< The system folder (BIOS, firmware, keys); empty: the program's own
    /// The app's save sync folder (its settings, and its notes on which version each save is),
    /// where `saves` is the app's: save sync in the program goes on from the same notes.
    QString sync;
};

/// For the console `key` ("ps1", "psp", "switch"), whose app's program is `program` (from the
/// app's file in Furoshiki's folder; may be empty): Station Reader's folder with its
/// portable.txt (saves/<serial>/<serial>_1.mcd, bios), PPSSPP Reader's memory stick (its
/// folder's memstick with portable.txt, else %APPDATA%\ppsspp-reader, or on Linux
/// ~/.config/ppsspp when its ppsspp.ini is the reader app's), Eden Reader's folder (its "user"
/// folder, else %APPDATA%\eden-reader or ~/.local/share/eden-reader: keys, nand, the saves in
/// nand), PCSX2
/// Reader's memory cards' reader-saves and its BIOS folder (as its inis/PCSX2.ini names them,
/// in its folder with portable.txt, else Documents\PCSX2Reader or ~/.config/PCSX2Reader).
[[nodiscard]] Folders Of(const QString& key, const QString& program);

/// Whether the program's own system folder for `key` has what the console needs, so that the
/// app's is not taken instead: for the Switch its prod.keys (in it or its keys/), since Eden
/// makes nand/ and title.keys there by itself; for the others anything at all.
[[nodiscard]] bool HasSystemFiles(const QString& key, const QString& folder);

} // namespace Home::AppFolders
