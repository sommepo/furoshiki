// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <future>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThread>
#include "reader/desktop/dictionary.h"
#include "dict/zip_reader.h"
#ifndef READER_DICT_ENGINE
// The engine's zip reader, built here where the engine is not (shared/dict/dict.cmake builds it
// with the rest of the engine; phase 2 makes every build do that).
#include "dict/mapped_file.cpp"
#include "dict/zip_reader.cpp"
#endif
#ifdef READER_DICT_ENGINE
#include "dict/library.h"
#endif

namespace ReaderApp {

namespace {

/// Carries the reason an import was given up.
struct ImportFailure {
    QString message;
};

/// An entry of the archive (read whole). The engine's zip reader (shared/dict): the file mapped
/// once and read from every thread; QuaZip cost seconds a dictionary on Windows.
QByteArray ReadEntry(const Dict::ZipArchive& zip, const QString& name) {
    const Dict::ZipArchive::Entry* entry = zip.Find(name.toStdString());
    std::string bytes;
    if (entry == nullptr || !zip.Read(*entry, bytes)) {
        throw ImportFailure{QStringLiteral("Couldn't read %1 in the zip file. Download the dictionary again").arg(name)};
    }
    return QByteArray::fromStdString(bytes);
}

// ---- A bank's rows, read straight from its bytes ----
// A bank is a JSON array of rows, each an array. Only the short fields are decoded; a glossary
// or a meta value is kept as the bytes it was written as (it is stored as JSON anyway), so a
// bank is never built into a tree and written out again. Malformed JSON gives up the import.

struct Malformed {};

const char* SkipSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        ++p;
    }
    return p;
}

/// The end of the string that begins at p (on its opening quote).
const char* StringEnd(const char* p, const char* end) {
    for (++p; p < end; ++p) {
        if (*p == '\\') {
            ++p;
        } else if (*p == '"') {
            return p + 1;
        }
    }
    throw Malformed{};
}

/// The end of the JSON value that begins at p.
const char* ValueEnd(const char* p, const char* end) {
    if (p >= end) {
        throw Malformed{};
    }
    if (*p == '"') {
        return StringEnd(p, end);
    }
    if (*p == '[' || *p == '{') {
        int depth = 0;
        for (; p < end; ++p) {
            const char c = *p;
            if (c == '"') {
                p = StringEnd(p, end) - 1;
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                if (--depth == 0) {
                    return p + 1;
                }
            }
        }
        throw Malformed{};
    }
    // A number, true, false or null.
    const char* start = p;
    while (p < end && *p != ',' && *p != ']' && *p != '}' && *p != ' ' && *p != '\n' && *p != '\r' &&
           *p != '\t') {
        ++p;
    }
    if (p == start) {
        throw Malformed{};
    }
    return p;
}

void PutUtf8(QByteArray& out, char32_t code) {
    if (code < 0x80) {
        out.append(static_cast<char>(code));
    } else if (code < 0x800) {
        out.append(static_cast<char>(0xC0 | (code >> 6)));
        out.append(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.append(static_cast<char>(0xE0 | (code >> 12)));
        out.append(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.append(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.append(static_cast<char>(0xF0 | (code >> 18)));
        out.append(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.append(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.append(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

int Hex4(const char* p, const char* end) {
    if (end - p < 4) {
        throw Malformed{};
    }
    int value = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = p[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value |= c - 'A' + 10;
        } else {
            throw Malformed{};
        }
    }
    return value;
}

/// The text of a JSON string [begin, end) (quotes included), in UTF-8.
QByteArray StringUtf8(const char* begin, const char* end) {
    const char* p = begin + 1;
    const char* stop = end - 1;
    const char* slash = static_cast<const char*>(memchr(p, '\\', static_cast<size_t>(stop - p)));
    if (!slash) {
        return QByteArray(p, stop - p);
    }
    QByteArray out(p, slash - p);
    for (p = slash; p < stop; ++p) {
        if (*p != '\\') {
            out.append(*p);
            continue;
        }
        if (++p >= stop) {
            throw Malformed{};
        }
        switch (*p) {
        case 'n': out.append('\n'); break;
        case 't': out.append('\t'); break;
        case 'r': out.append('\r'); break;
        case 'b': out.append('\b'); break;
        case 'f': out.append('\f'); break;
        case 'u': {
            char32_t code = static_cast<char32_t>(Hex4(p + 1, stop));
            p += 4;
            if (code >= 0xD800 && code < 0xDC00 && stop - p > 6 && p[1] == '\\' && p[2] == 'u') {
                const int low = Hex4(p + 3, stop);
                if (low >= 0xDC00 && low < 0xE000) {
                    code = 0x10000 + ((code - 0xD800) << 10) + static_cast<char32_t>(low - 0xDC00);
                    p += 6;
                }
            }
            PutUtf8(out, code);
            break;
        }
        default: out.append(*p); break; // \" \\ \/
        }
    }
    return out;
}

QString StringText(const char* begin, const char* end) {
    return QString::fromUtf8(StringUtf8(begin, end));
}

/// A string or number as UTF-8 text; anything else as nothing.
QByteArray Utf8(const char* begin, const char* end) {
    if (*begin == '"') {
        return StringUtf8(begin, end);
    }
    if (*begin == '-' || (*begin >= '0' && *begin <= '9')) {
        const double number = QByteArray::fromRawData(begin, end - begin).toDouble();
        if (number == std::floor(number) && std::abs(number) < 9.0e15) {
            return QByteArray::number(static_cast<qint64>(number));
        }
        return QByteArray::number(number, 'g', 15);
    }
    return {};
}

QString Text(const char* begin, const char* end) {
    return QString::fromUtf8(Utf8(begin, end));
}

qint64 Number(const char* begin, const char* end) {
    if (*begin == '"') {
        return static_cast<qint64>(StringText(begin, end).toDouble());
    }
    return static_cast<qint64>(QByteArray::fromRawData(begin, end - begin).toDouble());
}

/// Calls each(fields) for every row of a bank, with the row's values as [begin, end) pairs.
template <typename Each>
void ForEachRow(const QByteArray& bank, Each&& each) {
    const char* p = bank.constData();
    const char* end = p + bank.size();
    if (end - p >= 3 && static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
        static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3; // a byte order mark
    }
    p = SkipSpace(p, end);
    if (p >= end || *p != '[') {
        throw Malformed{};
    }
    std::vector<std::pair<const char*, const char*>> fields;
    p = SkipSpace(p + 1, end);
    while (p < end && *p != ']') {
        if (*p != '[') {
            p = SkipSpace(ValueEnd(p, end), end); // not a row: passed by
        } else {
            fields.clear();
            p = SkipSpace(p + 1, end);
            while (p < end && *p != ']') {
                const char* value_end = ValueEnd(p, end);
                fields.emplace_back(p, value_end);
                p = SkipSpace(value_end, end);
                if (p < end && *p == ',') {
                    p = SkipSpace(p + 1, end);
                }
            }
            if (p >= end) {
                throw Malformed{};
            }
            p = SkipSpace(p + 1, end);
            each(fields);
        }
        if (p < end && *p == ',') {
            p = SkipSpace(p + 1, end);
        }
    }
}

// Every text is kept in UTF-8, as it is stored: the writing thread only binds it.
struct TermRow {
    QByteArray expression;
    QByteArray reading;
    QByteArray definition_tags;
    QByteArray rules;
    qint64 score{};
    QByteArray glossary; // packed
    qint64 sequence{};
    QByteArray term_tags;
};

struct MetaRow {
    QByteArray expression;
    QByteArray mode;
    QByteArray data;
};

/// One bank (or a run of pictures) read and made ready to insert, off the database's thread.
struct Bank {
    std::vector<TermRow> terms;
    std::vector<MetaRow> metas;
    std::vector<std::pair<QString, QByteArray>> media; // path in the zip, bytes
};

Bank ReadBank(const Dict::ZipArchive& zip, const QString& name, bool terms, int format) {
    const QByteArray bytes = ReadEntry(zip, name);
    Bank bank;
    try {
        ForEachRow(bytes, [&](const std::vector<std::pair<const char*, const char*>>& f) {
            if (terms) {
                if (f.size() < 5 || *f[0].first != '"') {
                    return;
                }
                TermRow row;
                row.expression = StringUtf8(f[0].first, f[0].second);
                QByteArray reading = Utf8(f[1].first, f[1].second);
                // An empty reading means the expression is its own reading.
                row.reading = reading == row.expression ? QByteArray() : std::move(reading);
                row.definition_tags = Utf8(f[2].first, f[2].second);
                row.rules = Utf8(f[3].first, f[3].second);
                row.score = Number(f[4].first, f[4].second);
                if (format >= 3) {
                    if (f.size() > 5) {
                        row.glossary = DictionaryDatabase::Pack(
                            QByteArray::fromRawData(f[5].first, f[5].second - f[5].first));
                    } else {
                        row.glossary = DictionaryDatabase::Pack(QByteArrayLiteral("[]"));
                    }
                    if (f.size() > 6) {
                        row.sequence = Number(f[6].first, f[6].second);
                    }
                    if (f.size() > 7) {
                        row.term_tags = Utf8(f[7].first, f[7].second);
                    }
                } else {
                    // Format 1: the glosses are the row's remaining values, kept as strings.
                    QByteArray glosses("[");
                    for (size_t i = 5; i < f.size(); ++i) {
                        if (i > 5) {
                            glosses.append(',');
                        }
                        if (*f[i].first == '"') {
                            glosses.append(f[i].first, f[i].second - f[i].first);
                        } else {
                            glosses.append(QJsonDocument(QJsonArray{Text(f[i].first, f[i].second)})
                                               .toJson(QJsonDocument::Compact)
                                               .mid(1)
                                               .chopped(1));
                        }
                    }
                    glosses.append(']');
                    row.glossary = DictionaryDatabase::Pack(glosses);
                }
                bank.terms.push_back(std::move(row));
            } else {
                if (f.size() < 3 || *f[0].first != '"') {
                    return;
                }
                bank.metas.push_back(MetaRow{StringUtf8(f[0].first, f[0].second),
                                             Utf8(f[1].first, f[1].second),
                                             QByteArray(f[2].first, f[2].second - f[2].first)});
            }
        });
    } catch (const Malformed&) {
        throw ImportFailure{QStringLiteral("Couldn't read %1 in the zip file. Download the dictionary again").arg(name)};
    }
    return bank;
}

/// A run of the zip's pictures, read off the database's thread.
Bank ReadMedia(const Dict::ZipArchive& zip, const QStringList& names) {
    Bank bank;
    bank.media.reserve(static_cast<size_t>(names.size()));
    for (const QString& name : names) {
        bank.media.emplace_back(name, ReadEntry(zip, name));
    }
    return bank;
}

/// A picture or other file a structured-content glossary may show (Yomitan's "path").
bool IsMedia(const QString& name) {
    static const QRegularExpression pictures{QStringLiteral("\\.(png|jpe?g|gif|webp|avif|svg|bmp|ico)$"),
                                             QRegularExpression::CaseInsensitiveOption};
    return pictures.match(name).hasMatch();
}

/**
 * Keeps the database's pages in memory while an import writes (its indexes are written all
 * over the file), and puts the usual size back afterwards.
 */
class LargeCache {
public:
    explicit LargeCache(SqliteDb& db_) : db{db_} {
        db.Exec("PRAGMA cache_size=-131072"); // 128 MB
    }
    ~LargeCache() {
        try {
            db.Exec("PRAGMA cache_size=-2000");
        } catch (...) {
        }
    }

private:
    SqliteDb& db;
};

} // namespace

DictionaryImporter::Result DictionaryImporter::Import(const QString& archive_path,
                                                      const Progress& progress,
                                                      const KeepGoing& keep_going) {
    QElapsedTimer whole;
    whole.start();
    // A zip among the program's own files (":/reader/names/...", a game's names dictionary) is
    // no file on disk, which the zip reader maps: it is read from a copy.
    if (archive_path.startsWith(u':')) {
        // A plain file, closed before it is read. The QTemporaryFile it was could not be opened
        // by the zip reader on Windows (every built-in names dictionary "couldn't be opened as a
        // zip file" on the Z13, 2026-11-23), most likely as it was still held open there: the
        // reader opens no file another handle may write.
        static std::atomic<int> copies{0};
        const QString path = QDir::temp().filePath(QStringLiteral("furoshiki-dictionary-%1-%2.zip")
                                                       .arg(QCoreApplication::applicationPid())
                                                       .arg(copies.fetch_add(1)));
        {
            QFile resource(archive_path);
            QFile copy(path);
            if (!resource.open(QIODevice::ReadOnly) || !copy.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                copy.write(resource.readAll()) != resource.size()) {
                QFile::remove(path);
                Result failed;
                failed.error = QStringLiteral("Couldn't read %1").arg(archive_path);
                failed.total_ms = whole.elapsed();
                return failed;
            }
        }
        Result result = ImportTimed(path, progress, keep_going);
        QFile::remove(path);
        result.total_ms = whole.elapsed();
        return result;
    }
    Result result = ImportTimed(archive_path, progress, keep_going);
    result.total_ms = whole.elapsed();
    return result;
}

DictionaryImporter::Result DictionaryImporter::ImportTimed(const QString& archive_path,
                                                           const Progress& progress,
                                                           const KeepGoing& keep_going) {
#ifdef READER_DICT_ENGINE
    if (Dict::Library* engine = database.Engine()) {
        Dict::ImportOptions options;
        // The dictionary's title for the progress shown, from its index.json (the engine says it
        // only when done): the Dictionaries page said "Importing · 8 of 18 · 0 / 64" without it.
        QString title;
        {
            Dict::ZipArchive zip;
            std::string unused;
            if (zip.Open(archive_path.toStdString(), unused)) {
                QString index_name;
                for (const Dict::ZipArchive::Entry& entry : zip.Entries()) {
                    const QString name = QString::fromStdString(entry.name);
                    if (name == QStringLiteral("index.json") || name.endsWith(QStringLiteral("/index.json"))) {
                        if (index_name.isEmpty() || name.size() < index_name.size()) {
                            index_name = name;
                        }
                    }
                }
                if (!index_name.isEmpty()) {
                    try {
                        const QJsonObject index = QJsonDocument::fromJson(ReadEntry(zip, index_name)).object();
                        title = index.value(QStringLiteral("title")).toString().trimmed();
                    } catch (const ImportFailure&) {
                        // the engine says what is wrong with it
                    }
                }
            }
        }
        options.progress = [&progress, &title](int done, int total) {
            if (progress) {
                progress(done, total, title);
            }
        };
        options.keep_going = keep_going;
        const Dict::ImportResult imported = engine->Import(archive_path.toStdString(), options);
        Result result;
        result.ok = imported.ok;
        result.engine = true;
        result.not_dictionary = imported.not_dictionary;
        result.already_there = imported.already_there;
        result.title = QString::fromStdString(imported.title);
        result.error = QString::fromStdString(imported.error);
        if (imported.ok) {
            result.terms = imported.terms;
            result.metas = imported.metas;
            result.media = imported.media;
            result.kanji = imported.kanji;
        }
        database.Changed();
        return result;
    }
#endif
    Result result;
    try {
        Dict::ZipArchive zip;
        {
            std::string problem;
            if (!zip.Open(archive_path.toStdString(), problem)) {
                throw ImportFailure{problem == "Couldn't open the file"
                                        ? QStringLiteral("Couldn't open this zip file. Choose a Yomitan dictionary (.zip)")
                                        : QString::fromStdString(problem)};
            }
        }
        QStringList entries;
        for (const Dict::ZipArchive::Entry& entry : zip.Entries()) {
            entries.append(QString::fromStdString(entry.name));
        }
        QString index_name;
        for (const QString& name : entries) {
            if (name == QStringLiteral("index.json") || name.endsWith(QStringLiteral("/index.json"))) {
                if (index_name.isEmpty() || name.size() < index_name.size()) {
                    index_name = name;
                }
            }
        }
        if (index_name.isEmpty()) {
            result.not_dictionary = true;
            throw ImportFailure{QStringLiteral("This zip file isn't a Yomitan dictionary")};
        }
        const QString prefix = index_name.left(index_name.size() - 10); // "index.json"
        const QJsonDocument index_document = QJsonDocument::fromJson(ReadEntry(zip, index_name));
        if (!index_document.isObject()) {
            throw ImportFailure{QStringLiteral("Couldn't read the dictionary's index.json. Download the dictionary again")};
        }
        const QJsonObject index = index_document.object();
        const QString title = index.value(QStringLiteral("title")).toString().trimmed();
        if (title.isEmpty()) {
            throw ImportFailure{QStringLiteral("This dictionary has no title and can't be imported")};
        }
        result.title = title;
        const int format = index.value(QStringLiteral("format"))
                               .toInt(index.value(QStringLiteral("version")).toInt(3));
        if (format != 1 && format != 3) {
            throw ImportFailure{QStringLiteral("This dictionary's format (%1) isn't supported. Choose a Yomitan dictionary").arg(format)};
        }

        const auto banks = [&entries, &prefix](const QString& kind) {
            const QRegularExpression pattern(QRegularExpression::anchoredPattern(
                QRegularExpression::escape(prefix) + kind + QStringLiteral("_(\\d+)\\.json")));
            QList<std::pair<int, QString>> found;
            for (const QString& name : entries) {
                const QRegularExpressionMatch match = pattern.match(name);
                if (match.hasMatch()) {
                    found.append({match.captured(1).toInt(), name});
                }
            }
            std::sort(found.begin(), found.end());
            QStringList names;
            for (const auto& [number, name] : found) {
                names.append(name);
            }
            return names;
        };
        const QStringList term_banks = banks(QStringLiteral("term_bank"));
        const QStringList meta_banks = banks(QStringLiteral("term_meta_bank"));
        if (term_banks.isEmpty() && meta_banks.isEmpty()) {
            throw ImportFailure{
                QStringLiteral("\"%1\" has no entries in it").arg(title)};
        }
        // The dictionary's own stylesheet for its structured content, as Yomitan reads it.
        QString styles;
        if (entries.contains(prefix + QStringLiteral("styles.css"))) {
            styles = QString::fromUtf8(ReadEntry(zip, prefix + QStringLiteral("styles.css")));
        }
        QStringList media;
        QHash<QString, qint64> sizes; // unpacked, to bound what is read ahead
        for (const Dict::ZipArchive::Entry& entry : zip.Entries()) {
            const QString name = QString::fromStdString(entry.name);
            sizes.insert(name, static_cast<qint64>(entry.size));
            if (name.startsWith(prefix) && IsMedia(name)) {
                media.append(name);
            }
        }
        const int total = static_cast<int>(term_banks.size() + meta_banks.size());
        const auto wanted = [&keep_going] {
            if (keep_going && !keep_going()) {
                throw ImportFailure{QStringLiteral("Import cancelled")};
            }
        };

        // The connection is held for the whole import, which is one transaction: a lookup made
        // meanwhile waits until it is done.
        {
            QMutexLocker lock(&database.Mutex());
            SqliteDb& db = database.Db();
            {
                SqliteDb::Statement exists(db, "SELECT 1 FROM dictionaries WHERE title = ?");
                if (exists.Bind(1, title).Step()) {
                    result.already_there = true;
                    throw ImportFailure{QStringLiteral("\"%1\" is already installed").arg(title)};
                }
            }

            LargeCache cache(db);
            // Kept in the write-ahead log: a rollback journal (tried 2026-11-23) had to be flushed
            // to disk each time the page cache filled, and Windows' flush made 大辞泉 take minutes.
            SqliteDb::Transaction transaction(db);
        int priority = 0;
        {
            SqliteDb::Statement next(db, "SELECT COALESCE(MAX(priority), -1) + 1 FROM dictionaries");
            if (next.Step()) {
                priority = static_cast<int>(next.Int(0));
            }
        }
        {
            SqliteDb::Statement insert(
                db, "INSERT INTO dictionaries(title, revision, author, description, "
                    "frequency_mode, priority, scope, styles) VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
            // Furoshiki's names dictionaries say which games they are for (docs/titles.md).
            QStringList scope;
            for (const QJsonValue& game : index.value(QStringLiteral("furoshikiScope")).toArray()) {
                scope.append(game.toString());
            }
            insert.Bind(1, title)
                .Bind(2, index.value(QStringLiteral("revision")).toString())
                .Bind(3, index.value(QStringLiteral("author")).toString())
                .Bind(4, index.value(QStringLiteral("description")).toString())
                .Bind(5, index.value(QStringLiteral("frequencyMode")).toString())
                .Bind(6, priority)
                .Bind(7, DictionaryDatabase::ScopeText(scope))
                .Bind(8, styles)
                .Run();
        }
        const qint64 dictionary_id = db.LastInsertId();

        SqliteDb::Statement insert_term(
            db, "INSERT INTO terms(dict, expression, reading, def_tags, rules, score, glossary, "
                "sequence, term_tags) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)");
        SqliteDb::Statement insert_meta(
            db, "INSERT INTO term_meta(dict, expression, mode, data) VALUES(?, ?, ?, ?)");
        int done = 0;
        if (progress) {
            progress(done, total, title);
        }

        // The banks are read, parsed and compressed on a few threads while this one writes them
        // in their order; at most a few are ahead of the writer, so memory stays bounded.
        struct Job {
            QString name;
            bool terms;
            QStringList media; // a run of pictures instead of a bank
            qint64 bytes;
        };
        std::vector<Job> jobs;
        for (const QString& name : term_banks) {
            jobs.push_back({name, true, {}, sizes.value(name)});
        }
        for (const QString& name : meta_banks) {
            jobs.push_back({name, false, {}, sizes.value(name)});
        }
        // Pictures in runs of about 8 MB, so that a few runs ahead of the writer stay small.
        {
            QStringList run;
            qint64 run_bytes = 0;
            for (const QString& name : media) {
                run.append(name);
                run_bytes += sizes.value(name);
                if (run_bytes >= 8 * 1024 * 1024 || run.size() >= 2000) {
                    jobs.push_back({QString(), false, run, run_bytes});
                    run.clear();
                    run_bytes = 0;
                }
            }
            if (!run.isEmpty()) {
                jobs.push_back({QString(), false, run, run_bytes});
            }
        }
        // One thread writes; every other core reads, parses and compresses (most of the time
        // of a big dictionary is in compressing its glossaries).
        const int threads = std::clamp(QThread::idealThreadCount() - 1, 1, 15);
        // What is read ahead of the writer is bounded by its size too: some dictionaries come in
        // a few banks of 60 MB and more, and every one of them read at once took 1.6 GB.
        constexpr qint64 AheadBytes = 384LL * 1024 * 1024;
        std::deque<std::pair<std::future<Bank>, qint64>> ahead;
        qint64 ahead_bytes = 0;
        size_t next_job = 0;
        const auto fill = [&] {
            while (next_job < jobs.size() && static_cast<int>(ahead.size()) < threads + 1 &&
                   (ahead.empty() || ahead_bytes + jobs[next_job].bytes <= AheadBytes)) {
                const Job job = jobs[next_job++];
                ahead_bytes += job.bytes;
                if (!job.media.isEmpty()) {
                    ahead.emplace_back(std::async(std::launch::async, ReadMedia, std::cref(zip), job.media),
                                       job.bytes);
                } else {
                    ahead.emplace_back(std::async(std::launch::async, ReadBank, std::cref(zip), job.name,
                                                  job.terms, format),
                                       job.bytes);
                }
            }
        };
        std::optional<SqliteDb::Statement> insert_media;
        if (!media.isEmpty()) {
            insert_media.emplace(db, "INSERT OR REPLACE INTO media(dict, path, data) VALUES(?, ?, ?)");
        }
        fill();
        QElapsedTimer clock;
        clock.start();
        qint64 waited = 0;
        while (!ahead.empty()) {
            wanted();
            const qint64 asked = clock.nsecsElapsed();
            Bank bank = ahead.front().first.get();
            waited += clock.nsecsElapsed() - asked;
            ahead_bytes -= ahead.front().second;
            ahead.pop_front();
            fill();
            for (TermRow& row : bank.terms) {
                insert_term.Bind(1, dictionary_id)
                    .BindUtf8(2, row.expression)
                    .BindUtf8(3, row.reading)
                    .BindUtf8(4, row.definition_tags)
                    .BindUtf8(5, row.rules)
                    .Bind(6, row.score)
                    .BindBlobInPlace(7, row.glossary)
                    .Bind(8, row.sequence)
                    .BindUtf8(9, row.term_tags)
                    .Run();
                ++result.terms;
            }
            for (const MetaRow& row : bank.metas) {
                insert_meta.Bind(1, dictionary_id)
                    .BindUtf8(2, row.expression)
                    .BindUtf8(3, row.mode)
                    .BindUtf8(4, row.data)
                    .Run();
                ++result.metas;
            }
            // The pictures its glossaries show, by their path in the zip.
            for (const auto& [name, data] : bank.media) {
                insert_media->Bind(1, dictionary_id)
                    .Bind(2, name.mid(prefix.size()))
                    .BindBlobInPlace(3, data)
                    .Run();
                ++result.media;
            }
            if (bank.media.empty() && progress) {
                progress(++done, total, title);
            }
        }

        SqliteDb::Statement counts(db, "UPDATE dictionaries SET terms = ?, metas = ? WHERE id = ?");
        counts.Bind(1, result.terms).Bind(2, result.metas).Bind(3, dictionary_id).Run();
        const qint64 written = clock.nsecsElapsed();
        transaction.Commit();
        result.ok = true;
        result.read_bytes = 0;
        for (const Job& job : jobs) {
            result.read_bytes += job.bytes;
        }
        result.threads = threads;
        result.waited_ms = waited / 1000000;
        result.wrote_ms = (written - waited) / 1000000;
        result.commit_ms = (clock.nsecsElapsed() - written) / 1000000;
        }
        database.Changed();
    } catch (const ImportFailure& failure) {
        result.error = failure.message;
    } catch (const SqliteError& error) {
        result.error = result.title.isEmpty()
                           ? QString::fromUtf8(error.what())
                           : QStringLiteral("Couldn't import \"%1\": %2")
                                 .arg(result.title, QString::fromUtf8(error.what()));
    }
    if (!result.ok) {
        result.terms = 0;
        result.metas = 0;
        result.media = 0;
    }
    return result;
}

} // namespace ReaderApp
