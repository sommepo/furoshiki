// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// 0xDC00's Agent scripts as Furoshiki uses them (docs/hooks.md): the folder they were fetched
// into (<shared folder>/program/hooks/scripts, never this repository), each script's own header
// read for a list to search, and the one bundle frida-inject is handed: the script, the files
// it requires, what it stored before, Furoshiki's stand-in for Agent's host (hooks/agent_host.js)
// and Agent's own runtime (the scripts' libLoader.js).
//
// Qt Core only: tested by reader-home-tests with a folder of made-up scripts.

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace Home::Hooks::Agent {

/// Whose the scripts are, said wherever they are listed.
inline const QString Credit = QStringLiteral("0xDC00 AND CONTRIBUTORS  ·  MIT  ·  GITHUB.COM/0XDC00/SCRIPTS");
inline const QString Runtime = QStringLiteral("libLoader.js");

struct Script {
    QString file;     ///< "NS_01002AE00F442000_FLOWERS_Les_Quatre_Saisons.js"
    QString name;     ///< Its @name without the ID: "Flowers: Les Quatre Saisons"
    QString id;       ///< The ID its @name begins with: "01002AE00F442000"
    QString version;  ///< The game's version its addresses are for
    QString author;
    QString runs_on;  ///< Its @description's first line: "Yuzu", "PPSSPP x64"
    QString platform; ///< By its file's name: "SWITCH", "PC", "PSP", "PS VITA", "PS2", "3DS"…
};

/// A script's header, from its text ("// ==UserScript== … // @name …").
[[nodiscard]] Script ReadHeader(const QString& file, const QString& source);
/// Every script of the folder (its top level; not the lib*.js, not a file without a @name), by name.
[[nodiscard]] QList<Script> List(const QString& folder);
/// Those with every word of `typed` in their name, ID, platform or file; all for nothing typed.
[[nodiscard]] QList<Script> Search(const QList<Script>& scripts, const QString& typed);
/// The scripts that look written for this program, the likeliest first (at most `most`): by the
/// game's ID in its window's title (an emulator shows it), else by its file's name, its folder's
/// and its window's title against each script's name. A PC game's are PC scripts; an emulator's
/// the scripts of its console. None where nothing is alike enough.
[[nodiscard]] QList<Script> Suggest(const QList<Script>& scripts, const QString& program, const QString& path, const QString& window, int most = 6);
/// Whether Agent's runtime is there: without it no script can run.
[[nodiscard]] bool HasRuntime(const QString& folder);

/// The files `source` requires that are files of the folder: require('./libYuzu.js') -> "libYuzu.js".
[[nodiscard]] QStringList Requires(const QString& source);
/// A path within the folder made plain ("./a/../lib.js" -> "lib.js"); empty when it leaves the folder.
[[nodiscard]] QString CleanPath(const QString& path);

/**
 * The one script frida-inject loads. `host` is hooks/agent_host.js's text; `storage` is
 * {"sessionStorage":{…},"localStorage":{…}} as kept from before. Empty, with `error`, when the
 * script or the runtime is not in the folder.
 */
[[nodiscard]] QString Bundle(const QString& folder, const QString& script, const QString& host, const QJsonObject& storage, QString& error);

/// What scripts stored, kept between attaches: <shared folder>/program/hooks/storage.json,
/// {"<title id>": {"localStorage": {…}, "sessionStorage": {…}, "pid": 1234}}. A session's storage
/// is the process's: another process ID starts it empty.
[[nodiscard]] QJsonObject LoadStorage(const QString& shared, const QString& title, qint64 pid);
/// One change a script made (Event::Storage), into `storage` (as LoadStorage gave it).
void ApplyStorage(QJsonObject& storage, const QString& which, const QString& op, const QStringList& args);
bool SaveStorage(const QString& shared, const QString& title, qint64 pid, const QJsonObject& storage);

} // namespace Home::Hooks::Agent
