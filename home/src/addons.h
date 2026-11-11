// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Game add-ons (AGENTS.md, "Add-ons"): a folder with an addon.json that changes how one or more
// games play here, with nothing to start by hand. It names its games and may bring
//   - game scripts (a folder whose files go into the reader's profiles before the game starts),
//   - settings for those games (put into each game's layer once: what the user changes stays),
//   - a companion: a program started as one of its games starts (and stopped when Furoshiki
//     quits) that drives the game over the companion channel, the reader's control channel
//     opened without a test run's ways (READER_COMPANION_CONTROL).
//
//   { "format": 1, "name": "Kakuremino", "console": "ps2", "games": [ "SLPM-67002" ],
//     "scripts": "profile", "settings": { "picture.aspect": "stretch" },
//     "companion": { "windows": [ "python/python.exe", "-I", "companion/server.py" ],
//                    "linux": [ "python3", "-I", "companion/server.py" ],
//                    "env": { "KAKUREMINO_SOCKET": "{control}" }, "url": "http://127.0.0.1:8813/" } }
//
// A companion's program is the add-on's file where one of that name is in its folder, else a
// program on the PATH; it runs in the add-on's folder. "{control}" in its environment is the
// channel's address (a Windows pipe's \\.\pipe\NAME, a Unix socket's path).

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

class QProcess;

namespace Home {

class PlaySettings;

struct Addon {
    QString folder;   ///< Absolute, without a slash at the end
    QString name;     ///< "Kakuremino"
    QString about;
    QString console;  ///< The console's key ("ps2"), or empty for any
    QStringList games;
    QString scripts;  ///< The folder of its game scripts (absolute), or empty
    QList<QPair<QString, QString>> settings;
    QStringList companion;  ///< This system's program and its arguments, or empty
    QList<QPair<QString, QString>> environment;
    QString url;      ///< The companion's page, or empty

    [[nodiscard]] bool For(const QString& console_key, const QString& game) const;
};

/// The newest format of addon.json read here.
inline constexpr int AddonFormat = 1;

/// Where add-ons are looked for, in order: the shared folder's `addons`, the program's own
/// `addons`, and the folders beside the program's folder (C:\Readers\Kakuremino beside
/// C:\Readers\ReaderHome, as windows-pc/update.ps1 unpacks the packages).
[[nodiscard]] QStringList AddonFolders(const QString& shared_folder, const QString& program_folder);

/// Each add-on in those folders: a folder of theirs with an addon.json (the folder beside the
/// program's: only one with an addon.json). The first of a name wins. What could not be read
/// goes to `problems`.
[[nodiscard]] QList<Addon> FindAddons(const QStringList& folders, QStringList* problems = nullptr);
/// One addon.json; nullopt-like (an empty name) with the reason in `problem`.
[[nodiscard]] Addon ReadAddon(const QString& folder, QString* problem = nullptr);

/// Copies the add-on's game scripts into `profiles` (the reader's user profiles), where they
/// differ; a user's own file of that name that did not come from an add-on is kept once as
/// NAME.before-addon. What came from an add-on is recorded in `state_file` (program/addons.json).
/// The names copied.
QStringList InstallScripts(const Addon& addon, const QString& profiles, const QString& state_file);

/// Puts the add-on's settings into the game's layer, each once (the record is `state_file`,
/// program/addons.json): one already set for the game is left as it is, and one the user
/// later changes or takes out stays so. True when `settings` changed (and is to be saved).
bool ApplySettings(const Addon& addon, PlaySettings& settings, const QString& console_key, const QString& game,
                   const QString& state_file);

/// The companions' channel: its name for the reader's control channel (QLocalServer) and the
/// address a companion opens.
struct CompanionChannel {
    QString listen;
    QString address;
};
[[nodiscard]] CompanionChannel MakeCompanionChannel(const QString& program_folder);

/// The companions that run, one for each add-on whose game was started, until the program ends.
class Companions : public QObject {
public:
    Companions(QString control_address, QString log_folder, QObject* parent = nullptr);
    ~Companions() override;

    /// Starts the add-on's companion unless it runs. False (with the reason) when it could not.
    bool Start(const Addon& addon, QString* problem = nullptr);
    [[nodiscard]] bool Running(const QString& name) const;

private:
    QString control_address;
    QString log_folder;
    QHash<QString, QProcess*> running;
};

} // namespace Home
