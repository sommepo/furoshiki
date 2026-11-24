// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <QList>
#include <QString>
#include <QStringList>

namespace Home {

/// One installed dictionary as the Dictionaries page shows it.
struct DictionaryEntry {
    qint64 id{};
    QString title;
    QString revision;
    QString author;
    QString description;
    QString frequency_mode; ///< Its index.json's frequencyMode, where it has one
    qint64 terms{};
    qint64 metas{};  ///< Frequency and pitch entries
    qint64 media{};  ///< Pictures its entries show
    bool enabled{};
    bool names{}; ///< A game's names dictionary, which Furoshiki installs and keeps up to date itself
    qint64 kanji{}; ///< Characters of its kanji banks
};

/// What the page says while it works: an import, a download or a deletion.
struct DictionaryWork {
    bool busy{};
    QString doing;  ///< "Importing 大辞泉", "Downloading Jitendex"
    int done{};     ///< Banks done (or megabytes downloaded)
    int total{};    ///< Of all; 0 while not known
    QString unit;   ///< "" for banks, "MB" for a download
    int file{};     ///< Which file of the import, from 1
    int files{};    ///< How many files the import has
};

/// The old dictionary file's dictionaries moved into the dictionary engine (once, at the first
/// start with the engine): how far it is.
struct DictionaryConversion {
    int done{};
    int total{};
    QString title; ///< The one being moved
    int step{};    ///< Its parts done (banks and pictures) of `steps`
    int steps{};
    bool finished{};    ///< Over (DictionaryDatabase::Conversion)
    QStringList failed; ///< When over: what did not come whole
    double seconds{};
    int resumed{}; ///< Moved by an earlier start closed halfway
};

/**
 * The Dictionaries page's work (Settings › Dictionaries): the installed dictionaries in their
 * lookup order, and what can be done to them, one or many at once. The page
 * (dictionary_panel.*) asks and draws; this does the work, on a thread of its own where it takes
 * long, and says `changed` when anything the page shows is different. Made where the reader is
 * built in (dictionary_shelf.cpp); without it the page is not there.
 */
class DictionaryShelf {
public:
    virtual ~DictionaryShelf() = default;

    /// The dictionaries, in their lookup order; kept, and read again after each change.
    [[nodiscard]] virtual QList<DictionaryEntry> List() = 0;
    /// Reads the list again at the next List() (the names dictionaries a game installed).
    virtual void Refresh() = 0;
    [[nodiscard]] virtual DictionaryWork Work() const = 0;
    /// The old dictionary file's dictionaries are still being moved into the dictionary engine:
    /// until then the list is the old file's.
    [[nodiscard]] virtual bool Converting() const = 0;
    [[nodiscard]] virtual DictionaryConversion Conversion() const = 0;
    /// The move given up for imports from the dictionaries' own files: what it had moved taken
    /// out and the old file deleted (DictionaryDatabase::AbandonConversion). How many it had.
    virtual int AbandonConversion() = 0;
    /// What went wrong in the last import, one line a file; empty when nothing did.
    [[nodiscard]] virtual QStringList Problems() const = 0;
    /// The last import in a line ("Imported 58 · 12 already installed · 1 problem"), until the
    /// next or until it is put away (ClearProblems); empty when there is none.
    [[nodiscard]] virtual QString LastImport() const = 0;
    virtual void ClearProblems() = 0;

    virtual void SetEnabled(const QList<qint64>& ids, bool on) = 0;
    /// The dictionaries that are not names dictionaries, in their new order: the names
    /// dictionaries keep their places among them.
    virtual void Reorder(const QList<qint64>& order) = 0;
    virtual void Delete(const QList<qint64>& ids) = 0;
    /// Files (.zip) and folders: a folder is every .zip in it and in its folders, at any depth.
    /// A .zip that is no dictionary is passed by without a word.
    virtual void Import(const QStringList& paths) = 0;
    virtual void DownloadJitendex() = 0;
    /// Stops the import or download in progress; what was imported before it stays.
    virtual void Cancel() = 0;

    /// Said on the window's thread whenever the list, the work or the problems change.
    std::function<void()> changed;
    /// A short line for the page's foot ("Imported 3 dictionaries").
    std::function<void(const QString&)> say;
};

/// The reader's dictionaries (ReaderSession's database); null where the reader is not built in.
std::unique_ptr<DictionaryShelf> MakeReaderDictionaryShelf();

} // namespace Home
