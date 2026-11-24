// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <optional>
#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>
#include "reader/desktop/deinflector.h"
#include "reader/desktop/sqlite_db.h"

namespace Dict {
class Library;
}

namespace ReaderApp {

/// An installed dictionary. Lower `priority` is shown first.
struct DictionaryInfo {
    qint64 id{};
    QString title;
    QString revision;
    QString author;
    QString description;
    QString frequency_mode;
    int priority{};
    bool enabled{};
    qint64 term_count{};
    qint64 meta_count{};
    /// The games it is for (Furoshiki's game IDs, from `furoshikiScope` in its index.json): a
    /// names dictionary is used only while one of them runs. Empty: every game.
    QStringList scope;
    /// Pictures its entries show (the engine's count; 0 where the SQLite file is read).
    qint64 media{};
    /// Characters of its kanji banks (the engine's only).
    qint64 kanji_count{};
};

/// One dictionary row for a term. `glossary_json` is the entry's glossary array as JSON.
struct TermEntry {
    qint64 id{}; ///< its row in the terms table
    qint64 dictionary_id{};
    QString dictionary;
    int priority{};
    QString expression;
    QString reading;
    QString definition_tags;
    QString rules;
    int score{};
    QString glossary_json;
    qint64 sequence{};
    QString term_tags;
    /// From a dictionary for the running game only (a names dictionary): ranked first.
    bool scoped{};
    /// The pictures its glossary shows, by their path in the dictionary's zip, as data
    /// addresses (data:image/...;base64,...). Empty for most entries.
    QHash<QString, QString> media;
};

/// A character of a kanji dictionary (kanji_bank): readings, meanings and its stats as the
/// dictionary gives them (KANJIDIC: strokes, grade, jlpt, freq...).
struct KanjiEntry {
    qint64 dictionary_id{};
    QString dictionary;
    QString character;
    QStringList onyomi;
    QStringList kunyomi;
    QString tags;
    QStringList meanings;
    QJsonObject stats;
};

/// A frequency, pitch or other "meta" row for a term. `data` is its JSON value.
struct TermMeta {
    QString dictionary;
    QString frequency_mode;
    QString expression;
    QString mode;
    QString data;
};

/**
 * Dictionaries imported from Yomitan-format archives, stored for lookup by expression or
 * reading. The file has the Android app's tables and the same packing of glossaries, so a
 * reader_dict.db made by either works in the other.
 */
class DictionaryDatabase {
public:
    static constexpr const char* FileName = "reader_dict.db";

    /// Opens (and creates) the database file, or ":memory:". "engine:FOLDER" opens the
    /// dictionary engine's library in FOLDER instead (docs/dictionary-engine.md), where it is
    /// built (READER_DICT_ENGINE; phase 1: the tests only).
    explicit DictionaryDatabase(const QString& path);
    /**
     * The dictionary engine's library in `library_folder` (docs/dictionary-engine.md, phase 2),
     * where the engine is built. Dictionaries of the SQLite file at `sqlite_path` not in it yet
     * are converted into it on a thread of their own; until that is done, lookups and the list
     * are the SQLite file's, and a stamp in the library says it is done. Without the engine,
     * the SQLite file as before.
     */
    DictionaryDatabase(const QString& sqlite_path, const QString& library_folder);
    ~DictionaryDatabase();
    /// Whether dictionaries are still being converted into the engine's library.
    [[nodiscard]] bool Converting() const {
        return converting.load();
    }
    /// How far the conversion is, for a bar the program shows meanwhile: dictionaries done of
    /// all, and the one being converted.
    struct Conversion {
        int done{};
        int total{};
        QString title;
        /// The one being moved: its parts done and all of them (banks and pictures).
        int step{};
        int steps{};
        /// Over: `failed` names what did not come whole (the old file then stays in use and the
        /// next start tries again), `seconds` how long it took. Before and during, false.
        bool finished{};
        QStringList failed;
        double seconds{};
        /// Dictionaries already moved by an earlier start that was closed halfway.
        int resumed{};
    };
    [[nodiscard]] Conversion ConversionProgress() const;
    /**
     * The user's other way (the maintainer, 2026-11-24: "can we at least give them the option to
     * delete and reimport?"): the move stopped, what it had moved taken out of the library, and
     * the old file deleted, so that the dictionaries are imported again from their own files.
     * Returns the titles the old file had. A conversion over, or none, is left as it is.
     */
    QStringList AbandonConversion();
    /// After a batch of imports: the engine's index of the whole library made again.
    void Compact();
    /// The engine's library when it is active, else nullptr.
    [[nodiscard]] Dict::Library* Engine() {
        return UseEngine() ? engine.get() : nullptr;
    }

    [[nodiscard]] QList<DictionaryInfo> Dictionaries();
    [[nodiscard]] bool HasEnabledDictionaries();
    void SetEnabled(qint64 id, bool enabled);
    /// Many at once (a page's mass edit): one save, one change.
    void SetEnabled(const QList<qint64>& ids, bool enabled);
    void Delete(const QList<qint64>& ids);
    /// Stores a new display order: the first id becomes the highest priority.
    void SetOrder(const QList<qint64>& ids);
    void Delete(qint64 id);
    /// Takes over the dictionaries of another such file (an app's own, from before the reader
    /// apps shared one) that this one has not (by title), after its own and in their order,
    /// switched on or off as they were. The other file is left as it is. Returns how many came.
    int TakeFrom(const QString& other_path);

    /**
     * Term rows of enabled dictionaries whose expression or reading is one of `terms`: of the
     * dictionaries for every game, and of those scoped to `game` (a game ID) when one is given.
     */
    [[nodiscard]] QList<TermEntry> FindTerms(const QStringList& terms, const QString& game = {},
                                             bool with_glossaries = true);
    /**
     * Reads the glossaries (and their pictures) of entries found without them. A lookup ranks
     * every row its text matches, in every dictionary, by their short fields alone, and reads
     * the glossaries of those it keeps: most of a hover's time was in unpacking the others.
     */
    void ReadGlossaries(QList<TermEntry*> entries);
    /// A character in the enabled kanji dictionaries (the engine's; none from the SQLite file).
    [[nodiscard]] QList<KanjiEntry> FindKanji(const QString& character);
    /// A character's frequencies in the enabled dictionaries (kanji_meta_bank).
    [[nodiscard]] QList<TermMeta> FindKanjiMeta(const QString& character);
    /// Frequency and pitch rows of enabled dictionaries for the given expressions.
    [[nodiscard]] QList<TermMeta> FindMeta(const QStringList& expressions);
    /// The stylesheets (styles.css) of the enabled dictionaries that have one, by title.
    [[nodiscard]] QHash<QString, QString> Styles();
    /// Called by the importer after a dictionary came in (what is kept of the list is stale).
    void Changed();

    /// A dictionary's scope as stored: "|ID|ID|" (upper case), or "" for every game.
    [[nodiscard]] static QString ScopeText(const QStringList& games);
    [[nodiscard]] static QStringList ScopeList(const QString& text);

    /// Compresses a glossary for storage; short ones are stored as they are.
    [[nodiscard]] static QByteArray Pack(const QString& json);
    /// The same for JSON that is UTF-8 already (an importer's slice of a bank).
    [[nodiscard]] static QByteArray Pack(const QByteArray& utf8_json);
    [[nodiscard]] static QString Unpack(const QByteArray& blob);

    /// For the importer, which writes under one transaction of its own.
    [[nodiscard]] SqliteDb& Db() {
        return db;
    }
    /// Held around every use of the connection.
    [[nodiscard]] QMutex& Mutex() {
        return mutex;
    }

private:
    /// The pictures a glossary names ("path"), read from the media table; the lock is held.
    [[nodiscard]] QHash<QString, QString> MediaOf(qint64 dictionary_id, const QString& glossary_json);

    /// The engine answers (it exists, and nothing is being converted into it).
    [[nodiscard]] bool UseEngine() const {
        return engine && !converting.load() && !sqlite_only.load();
    }
    void Convert(QString sqlite_path, QString stamp);
    /// Opens (and makes, or brings up to date) the SQLite file.
    void Open(const QString& path);

    SqliteDb db;
    std::unique_ptr<Dict::Library> engine;
    std::atomic<bool> converting{false};
    /// A conversion that did not bring every dictionary: the SQLite file answers for this run.
    std::atomic<bool> sqlite_only{false};
    std::atomic<int> conversion_done{0};
    std::atomic<int> conversion_total{0};
    std::atomic<int> conversion_step{0};
    std::atomic<int> conversion_steps{0};
    std::atomic<int> conversion_resumed{0};
    std::atomic<bool> conversion_finished{false};
    double conversion_seconds{};
    QStringList conversion_failed;
    mutable QMutex conversion_mutex;
    QString conversion_title;
    std::atomic<bool> stopping{false};
    QString old_path;   ///< The SQLite file being moved (AbandonConversion)
    QString stamp_path; ///< The library's stamp that says the move is over
    std::thread converter;
    QMutex mutex;
    bool styles_known{};
    QHash<QString, QString> styles;
    /// Pictures lately shown, as data addresses, by dictionary and path: a dictionary's few
    /// icons appear in most of its entries, and each was read and encoded again every time.
    QCache<QString, QString> pictures{32 * 1024 * 1024}; // by their size in characters
};

/// How common a term is according to one frequency dictionary.
struct Frequency {
    QString dictionary;
    QString display;
    std::optional<double> value;
    bool rank_based{};
};

/// Pitch accent of one reading: the mora after which the pitch drops (0 = none).
struct PitchAccent {
    QString dictionary;
    QString reading;
    QList<int> positions;
};

/// One headword found at the looked-up position, with everything the dictionaries say of it.
struct LookupResult {
    QString expression;
    QString reading;
    /// The part of the scanned text this headword covers.
    QString matched_text;
    /// Inflections undone to reach the headword, outermost first.
    QStringList reasons;
    /// Dictionary entries, most preferred dictionary first.
    QList<TermEntry> entries;
    QList<Frequency> frequencies;
    QList<PitchAccent> pitches;

    /// The reading to show: empty readings mean the expression is already kana.
    [[nodiscard]] QString DisplayReading() const {
        return (reading.isEmpty() || reading == expression) ? QString() : reading;
    }
};

/**
 * Finds the dictionary words that start at the beginning of a piece of text: the longest
 * stretch first, each stretch tried as written, in the other kana script and deinflected.
 */
class DictionaryLookup {
public:
    /// Longest stretch of text tried as one word.
    static constexpr int MaxScanLength = 20;

    explicit DictionaryLookup(DictionaryDatabase& database_) : database{database_} {}

    /**
     * The words starting at the first character of `text`: at most `max_results`, best first.
     * `game` is the running game's ID: dictionaries scoped to it are used, and their entries
     * come first among words of the same length; other scoped dictionaries are not.
     */
    [[nodiscard]] QList<LookupResult> Lookup(const QString& text, int max_results = 24,
                                             const QString& game = {});

    /**
     * A single rank for sorting cards by frequency: the harmonic mean of the term's ranks in
     * rank-based frequency dictionaries, or nothing when there is none.
     */
    [[nodiscard]] static std::optional<qint64> HarmonicRank(const QList<Frequency>& frequencies);

private:
    DictionaryDatabase& database;
    Deinflector deinflector;
};

/**
 * Imports a dictionary archive in the Yomitan format (index.json plus term_bank_N.json and
 * term_meta_bank_N.json files), with its styles.css and the pictures in it. Kanji and tag
 * banks are skipped.
 */
class DictionaryImporter {
public:
    struct Result {
        bool ok{};
        QString title;
        qint64 terms{};
        qint64 metas{};
        /// Pictures and other files its glossaries show.
        qint64 media{};
        /// Characters of its kanji banks.
        qint64 kanji{};
        /// What went wrong, when not ok.
        QString error;
        /// Not ok because the file is no Yomitan dictionary at all (no index.json): a folder's
        /// import passes such a zip by.
        bool not_dictionary{};
        /// Not ok because a dictionary of that title is installed already: a batch (a folder
        /// imported again) passes it by and says how many, not each as a problem.
        bool already_there{};
        /// How long it took, for the log (Summary): in all, waiting for the banks to be read,
        /// writing them, and the commit; the threads that read and the bank text read.
        qint64 total_ms{};
        qint64 waited_ms{};
        qint64 wrote_ms{};
        qint64 commit_ms{};
        int threads{};
        qint64 read_bytes{};
        /// Imported by the dictionary engine (its parts are not timed one by one).
        bool engine{};

        /// One line for the log: what came in and how long each part took.
        [[nodiscard]] QString Summary() const {
            if (!ok) {
                return QStringLiteral("import failed after %1 s: %2").arg(total_ms / 1000.0, 0, 'f', 2).arg(error);
            }
            if (engine) {
                return QStringLiteral("imported %1: %2 terms, %3 metas, %4 pictures, %5 kanji in %6 s (dictionary engine)")
                    .arg(title)
                    .arg(terms)
                    .arg(metas)
                    .arg(media)
                    .arg(kanji)
                    .arg(total_ms / 1000.0, 0, 'f', 2);
            }
            return QStringLiteral("imported %1: %2 terms, %3 metas, %4 pictures in %5 s (banks read %6 MB "
                                  "on %7 threads; waited for them %8 s, wrote %9 s, commit %10 s)")
                .arg(title)
                .arg(terms)
                .arg(metas)
                .arg(media)
                .arg(total_ms / 1000.0, 0, 'f', 2)
                .arg(read_bytes / 1048576.0, 0, 'f', 0)
                .arg(threads)
                .arg(waited_ms / 1000.0, 0, 'f', 2)
                .arg(wrote_ms / 1000.0, 0, 'f', 2)
                .arg(commit_ms / 1000.0, 0, 'f', 2);
        }
    };

    /// Called with the number of banks done, the total and the dictionary's title.
    using Progress = std::function<void(int done, int total, const QString& title)>;
    /// Returning false abandons the import and leaves the database unchanged.
    using KeepGoing = std::function<bool()>;

    explicit DictionaryImporter(DictionaryDatabase& database_) : database{database_} {}

    [[nodiscard]] Result Import(const QString& archive_path, const Progress& progress = {},
                                const KeepGoing& keep_going = {});

private:
    [[nodiscard]] Result ImportTimed(const QString& archive_path, const Progress& progress,
                                     const KeepGoing& keep_going);

public:

private:
    DictionaryDatabase& database;
};

} // namespace ReaderApp
