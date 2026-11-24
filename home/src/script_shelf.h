// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <QList>
#include <QString>
#include <QStringList>

namespace Home {

/// A game the Game scripts page lists: one of a console's published list, one with a script of
/// the user's or the program's own, or one of the library.
struct ScriptGame {
    QString id;
    QString title;
    QString console;      ///< The console's key ("ps2"); empty where it is not known
    enum class Script {
        None,         ///< No script anywhere: the list has none, nor the user, nor the program
        NotInstalled, ///< The list has one, nothing here
        Installed,    ///< The list's, installed from it as it is now
        BuiltIn,      ///< The program's own, the same as the list's or not in a list
        Update,       ///< The list has another than the one installed or built in
        Own,          ///< The user's own, not the list's as it is now
        TooNew,       ///< The list's needs a newer program
    } script{Script::None};
    enum class Names {
        None,      ///< Its title has no names dictionary
        Available, ///< The list has one; not here yet
        Ready,     ///< Here (installed with the script, or built in), put in the dictionaries as the game starts
        Installed, ///< In the dictionaries
    } names{Names::None};
    bool in_list{};    ///< A console's published list has it
    bool in_library{}; ///< On the shelf
    QString reads;     ///< What of its text is read, as the list says
};

/// A game of the library, as the page is told of it.
struct ScriptLibraryGame {
    QString id, title, console;
};

/// What the page says while it works.
struct ScriptWork {
    bool busy{};
    QString doing; ///< "Fetching the lists", "Installing 428"
    int done{};
    int total{};
};

/**
 * The Game scripts page's work (Settings › Game scripts): every console's published list of game
 * scripts, what the program and the user have, each game's names dictionary, and installing,
 * updating or removing them, one or many at once. Its network work is done in the background, one
 * file after the other. Made where the reader is built in (script_shelf.cpp).
 */
class ScriptShelf {
public:
    virtual ~ScriptShelf() = default;

    /// Every game, sorted by console and title; read again after each change.
    [[nodiscard]] virtual QList<ScriptGame> Games() = 0;
    /// The consoles' keys and names, in the page's order.
    [[nodiscard]] virtual QList<std::pair<QString, QString>> Consoles() const = 0;
    /// Fetches every console's list again (the page does at its first showing).
    virtual void FetchLists() = 0;
    [[nodiscard]] virtual bool ListsFetched() const = 0;
    [[nodiscard]] virtual ScriptWork Work() const = 0;
    /// The last action in a line ("Installed 12 scripts · 2 couldn't be installed"), and its
    /// problems in full; empty when there is none.
    [[nodiscard]] virtual QString LastAction() const = 0;
    [[nodiscard]] virtual QStringList Problems() const = 0;
    virtual void ClearProblems() = 0;

    /// The list's scripts of these games installed (or updated), each with its data files and
    /// names dictionary; those the list does not have are passed by.
    virtual void Install(const QStringList& ids) = 0;
    /// Their names dictionaries fetched where they are not here, and put in the dictionaries.
    virtual void InstallNames(const QStringList& ids) = 0;
    /// The user's scripts of these games taken out (the program's own and the list's stay).
    virtual void Remove(const QStringList& ids) = 0;
    /// Scripts and data files the user chose ("<ID>.lua", "<ID>.<name>").
    virtual void InstallFiles(const QStringList& paths) = 0;
    virtual void Cancel() = 0;

    /// The library's games (HomeWindow tells them).
    std::function<QList<ScriptLibraryGame>()> library;
    /// Said on the window's thread whenever what the page shows changes.
    std::function<void()> changed;
    std::function<void(const QString&)> say;
    /// Scripts were installed or removed: the program looks for its games again.
    std::function<void()> scripts_changed;
    /// Names dictionaries were installed: the Dictionaries page reloads.
    std::function<void()> dictionaries_changed;
};

/// The reader's scripts and dictionaries; null where the reader is not built in.
std::unique_ptr<ScriptShelf> MakeReaderScriptShelf();

} // namespace Home
