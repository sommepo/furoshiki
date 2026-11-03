// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The old reader apps' own settings, read once (docs/one-program.md, "Settings": "The old
// files are read once, the first time the program starts, and what differs from the defaults
// becomes console settings; after that they are not used"). What the user changed in an
// app's emulator (its value differs from that emulator's own default) and the program has a
// setting for becomes that console's setting here, unless the console already has one.

#include <QList>
#include <QString>

namespace Home {

class PlaySettings;

namespace OldSettings {

/// A console's setting as the program keeps it ("ps1", "picture.resolution", "3").
struct Value {
    QString console;
    QString key;
    QString value;
};

/// An old app's settings file that is there.
struct Source {
    QString console; ///< "ps1"
    QString app;     ///< "station-reader"
    QString file;
};

/// The old apps' settings files that are there: on Windows those of the apps beside the
/// program's folder (C:\Readers\StationReader\settings.ini, FlycastReader\emu.cfg,
/// PPSSPPReader\memstick\PSP\SYSTEM\ppsspp.ini) or in the user's folders where an app keeps
/// them without its portable.txt; on Linux the apps' folders in the user's.
[[nodiscard]] QList<Source> Sources(const QString& program_folder);

/// What a file's text holds that differs from its emulator's defaults, as the program's
/// settings. DuckStation's settings.ini (Station Reader), Flycast's emu.cfg, PPSSPP's
/// ppsspp.ini.
[[nodiscard]] QList<Value> FromStation(const QString& text);
[[nodiscard]] QList<Value> FromFlycast(const QString& text);
[[nodiscard]] QList<Value> FromPpsspp(const QString& text);
[[nodiscard]] QList<Value> Read(const Source& source);

/// The first time only (the settings remember it): every source read into the consoles'
/// layers, where the console has no value of its own yet, and the settings saved. Returns
/// what was taken (nothing after the first time).
QList<Value> ReadOnce(PlaySettings& settings, const QString& program_folder);

} // namespace OldSettings
} // namespace Home
