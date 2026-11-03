// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PCSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include "reader/core/console.h"

namespace ReaderApp {

/**
 * Game scripts (text profiles) to install: the list a repository publishes of them
 * (`index.json`, made by tools/reader/make-public.sh), what is installed and how it compares,
 * and installing and removing one in the user's profiles folder. Nothing here goes on the
 * network: the app fetches the list and the files and hands them over.
 */
namespace Scripts {

/// The version of the profile API this app has (what `reader.*` offers a profile). A script
/// says which it needs with the line `-- reader: api N`; one without it needs 1. 2 added
/// reader.progress with mem.w8/w16/w32; 3 reader.action, mem.w64 and a layout's `screen`.
inline constexpr int Api = 3;

/// Where the app fetches the list of scripts unless told otherwise (READER_SCRIPTS_URL). Asked
/// each time: a program that plays several consoles says it at run time.
inline const char* DefaultIndexUrl() {
    return Reader::ConsoleTraits::ScriptIndexUrl;
}

/// A file of the list: its name beside the list, its SHA-256 (lower-case hex) and size.
struct FileEntry {
    QString file;
    QString sha256;
    qint64 size{};
};

/// One game's script in the list.
struct Entry {
    QString id;      ///< The game's ID, as the script's file name has it
    QString title;   ///< The game's name
    QString reads;   ///< What of its text is read, in a line
    QString version; ///< The version of the game the script was written for
    int api = 1;     ///< The profile API the script needs
    FileEntry script;
    QList<FileEntry> data; ///< Data files it asks for with reader.data(name), "<ID>.<name>"
};

struct Index {
    QString system; ///< Reader::ConsoleTraits::ScriptSystem
    QList<Entry> entries;
};

/// Reads a list. Nothing, and `error` set, when it is not one this app understands.
[[nodiscard]] std::optional<Index> ParseIndex(const QByteArray& json, QString* error = nullptr);

/// The SHA-256 of `bytes` in lower-case hex.
[[nodiscard]] QString Sha256(const QByteArray& bytes);

/// The profile API a script's source says it needs (`-- reader: api N`), 1 when it says
/// nothing.
[[nodiscard]] int ApiOf(const QString& source);

/// How a game's script in the list compares with what the app has.
enum class State {
    NotInstalled, ///< Neither the app nor the user's folder has a script for the game
    Installed,    ///< What the app uses is the list's script, byte for byte
    Update,       ///< The list has another script than the one installed from it, or bundled
    Own,          ///< The user's folder has a script of the game that did not come from the list
                  ///< as it is now (written or changed by the user): installing replaces it
    TooNew,       ///< The list's script needs a newer app
};

/**
 * The state of `entry`. `user` is the script in the user's folder, `bundled` the one the app
 * comes with, `recorded` the SHA-256 the list's script had when it was installed from the list
 * (empty when it was not).
 */
[[nodiscard]] State StateOf(const Entry& entry, const std::optional<QByteArray>& user,
                            const std::optional<QByteArray>& bundled, const QString& recorded);

/**
 * The user's profiles folder: what is in it and what came from the list. The record of what
 * was installed from the list is a file of its own in the folder (`.installed.json`).
 */
class Folder {
public:
    explicit Folder(QString path);

    [[nodiscard]] const QString& Path() const {
        return path;
    }
    /// The user's script of the game, if there is one.
    [[nodiscard]] std::optional<QByteArray> Script(const QString& id) const;
    /// The SHA-256 the game's script had when it was installed from the list; empty otherwise.
    [[nodiscard]] QString Recorded(const QString& id) const;

    /**
     * Installs the list's script of `entry` and its data files: `files` has the bytes of each,
     * by file name. Every file is checked against the list (name, size, SHA-256) and the
     * script against this app's API before anything is written. False, and `error` set, when a
     * check fails or a file cannot be written.
     */
    bool Install(const Entry& entry, const QHash<QString, QByteArray>& files, QString* error);

    /**
     * Installs scripts and data files the user chose: a file named `<ID>.lua` is a script, a
     * file named `<ID>.<name>` a data file. The IDs of the scripts installed; `error` says what
     * was not taken and why. What is installed this way is the user's own (State::Own).
     */
    QStringList InstallFiles(const QStringList& paths, QString* error);

    /// Removes the game's script and the data files that came with it from the list. False
    /// when there was no script.
    bool Remove(const QString& id);

private:
    QHash<QString, QString> ReadRecord() const;
    bool WriteRecord(const QHash<QString, QString>& record) const;
    bool WriteFile(const QString& name, const QByteArray& bytes, QString* error) const;

    QString path;
};

/// Whether `name` is a file a script may ask for or be: "<ID>.lua" or "<ID>.<name>" (the ID as
/// Catalog::TitleIdOf knows it), no folders. The ID it is for, or nothing.
[[nodiscard]] std::optional<QString> IdOfFile(const QString& name);

} // namespace Scripts
} // namespace ReaderApp
