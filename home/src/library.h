// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What the reader apps say of themselves. Each reader app (Eden Reader, Azahar Reader, PPSSPP
// Reader, PCSX2 Reader, Flycast Reader, Station Reader, and any added later) writes one file,
// <shared folder>/apps/<app>.json,
// when it starts, when it knows its games and when a game stops: where its program is, how
// a game is started with it, the games it found whose text it can read, and its reading
// statistics. Furoshiki reads those files and nothing else of the apps: it has no list of
// its own of what can be read, and it never looks into a game's file.

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Home {

struct Game {
    QString app;      ///< The app that reads it ("flycast-reader")
    QString system;   ///< That app's console, as the app names it ("Dreamcast")
    QString id;       ///< The game's ID as that app knows it
    QString title;
    QString path;     ///< The game's file; may be empty for an app that starts a game by its ID
    QString cover;    ///< A picture's file, or empty
    qint64 chars{};   ///< Characters read, all days
    qint64 active_ms{};
    QString last_day; ///< The last day it was read on ("2026-10-10"), or empty
};

struct Day {
    QString day;
    qint64 chars{};
    qint64 active_ms{};
};

struct App {
    QString app;
    QString name;        ///< "Flycast Reader"
    QString system;      ///< "Dreamcast"
    QString program;     ///< The program's file
    QStringList game_args; ///< Its arguments for a game; "%ROM%" stands for the game's file,
                           ///< "%ID%" for its ID (an app whose games are installed titles)
    QStringList menu_args; ///< Its arguments for its own menu
    /// Its arguments for full screen, added for a game unless games start in a window (and
    /// taken out when they do). Optional ("fullscreen_args").
    QStringList fullscreen_args;
    QString short_name;  ///< Its console's label on the line ("PS2"); optional ("short")
    /// Where its console goes on the line; optional ("order"), -1: by Furoshiki's list, where
    /// the consoles it knows are 10 apart (Switch 10, 3DS 20, PlayStation 30, PSP 40, Vita 50,
    /// PS2 60, Dreamcast 70) and one it does not know is at 80.
    int order{-1};
    bool program_found{};
    qint64 written{};    ///< When the file was written (ms since 1970)
    QList<Game> games;
    qint64 chars{};
    qint64 active_ms{};
};

struct Library {
    QList<App> apps;       ///< In a fixed order of consoles, then by name
    QList<Game> games;     ///< Every app's games: the last read first, then by title
    QList<Day> days;       ///< Every app's reading by day, oldest first
    qint64 chars{};
    qint64 active_ms{};
    QStringList problems;  ///< Files that could not be read, for the Readers page
};

/// The newest format of an app's file this Furoshiki reads. A file of a newer one is left
/// out with a problem rather than misread.
inline constexpr int AppFileFormat = 1;

/// The folder the apps and Furoshiki share: READER_HOME_DIR when set (tests), else
/// "reader-home" in the user's local data folder.
[[nodiscard]] QString SharedFolder();
/// Once: the folder of the program's former name (reader-home) becomes Furoshiki's, all of it
/// (settings, dictionaries, statistics, the consoles' folders). Not while the old one is in use
/// elsewhere (a rename then fails, and SharedFolder goes on with the old one). True if moved.
bool MoveOldFolder();

/// Reads every file in <folder>/apps.
[[nodiscard]] Library Load(const QString& folder);

/// The settings every reader app takes from Furoshiki: <folder>/settings.json. The apps
/// read it when they start and when it changes.
[[nodiscard]] QJsonObject LoadShared(const QString& folder);
bool SaveShared(const QString& folder, const QJsonObject& settings);

/// The arguments for a game with "%ROM%" (its file) and "%ID%" filled in; with the app's
/// full-screen arguments added (`windowed` false) or every known word for full screen taken
/// out (`windowed` true).
[[nodiscard]] QStringList GameArguments(const App& app, const Game& game, bool windowed);
/// The arguments without the app's words for full screen (its own and the ones the apps have
/// used: -f, --fullscreen, -fullscreen).
[[nodiscard]] QStringList WithoutFullScreen(const App& app, QStringList arguments);
/// Whether the app's games are started from their file (its arguments name "%ROM%").
[[nodiscard]] bool NeedsFile(const App& app);

/// The console's label on the line: the short name an app of it gives, else Furoshiki's own
/// for the consoles it knows, else the name in capitals.
[[nodiscard]] QString ShortSystem(const Library& library, const QString& system);

/// The games with every ID once (a game's discs, two copies of it, share their figures).
[[nodiscard]] QList<Game> DistinctGames(const QList<Game>& games);

/// "3h 20m", "45m", "under a minute".
[[nodiscard]] QString TimeText(qint64 active_ms);
/// "12,345".
[[nodiscard]] QString CountText(qint64 count);

} // namespace Home
