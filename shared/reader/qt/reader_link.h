// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What this app tells Reader Home, the one front for the reader apps: a file of its own in
// the folder they share, <local data>/reader-home/apps/<app>.json, with where the program
// is, how a game is started with it, the games it found whose text it can read, and its
// reading statistics. Reader Home reads the file; nothing here depends on its being there.
//
// The same file is in every reader fork.

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace ReaderUi::HomeLink {

struct About {
    QString app;           ///< "flycast-reader": the file's name, and how the statistics name it
    QString name;          ///< "Flycast Reader"
    QString system;        ///< "Dreamcast"
    QStringList game_args; ///< The program's arguments for a game; "%ROM%" is the game's file
    QStringList menu_args; ///< And for its own menu
    /// Optional. The program's arguments for full screen, which Reader Home adds to a game's
    /// unless games start in a window; "%ID%" in game_args is the game's ID, for an app that
    /// starts a game by it; the console's label on Reader Home's line ("PS2") and its place
    /// there (home/src/library.h). Left empty (and -1), Reader Home goes by its own list.
    QStringList fullscreen_args{};
    QString short_name{};
    int order{-1};
};

struct Game {
    QString id;
    QString title;
    QString path;
    QString cover; ///< A picture's file, or empty
};

/// The folder shared with Reader Home: READER_HOME_DIR when set (tests), else "reader-home"
/// in the user's local data folder.
[[nodiscard]] QString Folder();

/// The settings every reader app takes from Reader Home: <folder>/settings.json, which
/// Reader Home writes. Empty when there is none. `changed`, when given, is set to whether
/// the file is another than at the last call with it (a caller asks now and then).
[[nodiscard]] QJsonObject SharedSettings(bool* changed = nullptr);

/// Writes the file anew. `games` and `stats` may be null: what the file had of them stays.
/// True when the file was written (or needed no writing: nothing had changed but the time).
bool Write(const About& about, const QList<Game>* games, const QJsonObject* stats);

} // namespace ReaderUi::HomeLink
