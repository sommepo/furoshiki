// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The CRT picture's values (../shared/reader/core/crt.h) in the program's layers
// (play_settings.h): for all consoles, one console or one game, as picture.crt in
// program/settings.json, one line of text as the reader keeps it ("warp=0.300;frame=2.000").
// A layer keeps only what differs from the layers under it, so a console that changes the warp
// still follows all consoles' scan lines. Under every layer: the table's first values, then the
// reader's own (reader.ini's picture/crt, what was set before there were layers).

#include <QMap>
#include <QString>
#include "play_settings.h"

namespace Home::CrtLayers {

/// The key in program/settings.json (READER_CORE_SETTING_CRT).
inline const QString Key = QStringLiteral("picture.crt");
/// The reader's own key (ReaderCrtPage::SettingsKey).
inline const QString ReaderKey = QStringLiteral("picture/crt");

using Values = QMap<QString, double>;

/// A line of text as values; a key the table does not know is skipped.
[[nodiscard]] Values Parse(const QString& text);
/// Values as a line of text, in the table's order (only the keys given).
[[nodiscard]] QString Join(const Values& values);
/// Every key of the table at its first value.
[[nodiscard]] Values Initial();

/// Every value for a game of a console (either may be empty: all consoles, or a console).
[[nodiscard]] Values Resolved(const PlaySettings& play, const QString& base, const QString& console,
                              const QString& game = {});
/// What that layer shows where it has nothing of its own: the layers under it.
[[nodiscard]] Values Below(const PlaySettings& play, const QString& base, const QString& console,
                           const QString& game = {});
/// Keeps `values` in that layer as what differs from the layers under it; takes the layer's
/// own out when nothing does. Not saved: the caller saves.
void Store(PlaySettings& play, const QString& base, const Values& values, const QString& console,
           const QString& game = {});

/// The layer a change made over a game goes to (the Reader menu's CRT page): the game's when it
/// has values of its own, else its console's when that has, else all consoles'.
struct Layer {
    QString console;
    QString game;
};
[[nodiscard]] Layer Target(const PlaySettings& play, const QString& console, const QString& game);

} // namespace Home::CrtLayers
