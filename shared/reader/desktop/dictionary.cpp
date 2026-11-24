// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutexLocker>
#include <QSet>
#include <optional>
#include <stdexcept>
#include <mutex>
#include <QElapsedTimer>
#include <zlib.h>
#ifdef READER_DICT_ENGINE
#include "dict/json.h"
#include "dict/library.h"
#else
namespace Dict {
class Library {};
} // namespace Dict
#endif
#include <QFile>
#include <QFileInfo>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/japanese_text.h"
#include "reader/desktop/text_line.h"

namespace ReaderApp {

namespace {

constexpr int MaxParameters = 400;
constexpr char PackRaw = 0;
constexpr char PackDeflate = 1;
constexpr int PackThreshold = 96;

std::string Placeholders(qsizetype count) {
    std::string out;
    for (qsizetype i = 0; i < count; ++i) {
        out += i == 0 ? "?" : ",?";
    }
    return out;
}

} // namespace

DictionaryDatabase::~DictionaryDatabase() {
    stopping = true;
    if (converter.joinable()) {
        converter.join();
    }
}

void DictionaryDatabase::Compact() {
#ifdef READER_DICT_ENGINE
    if (engine) {
        engine->Compact();
    }
#endif
}

#ifdef READER_DICT_ENGINE
namespace {

/// A dictionary of the SQLite file as the engine's source: its rows as Yomitan banks.
class SqliteSource : public Dict::Source {
public:
    SqliteSource(SqliteDb& db_, std::mutex& mutex_, qint64 id_) : db{db_}, mutex{mutex_}, id{id_} {
        std::lock_guard lock(mutex);
        SqliteDb::Statement info(db, "SELECT title, revision, author, description, frequency_mode, scope, styles "
                                     "FROM dictionaries WHERE id = ?");
        info.Bind(1, id);
        if (info.Step()) {
            QJsonArray games;
            for (const QString& game : info.Text(5).split(u'|', Qt::SkipEmptyParts)) {
                games.append(game);
            }
            QJsonObject index{{QStringLiteral("title"), info.Text(0)},
                              {QStringLiteral("revision"), info.Text(1)},
                              {QStringLiteral("author"), info.Text(2)},
                              {QStringLiteral("description"), info.Text(3)},
                              {QStringLiteral("frequencyMode"), info.Text(4)},
                              {QStringLiteral("format"), 3}};
            if (!games.isEmpty()) {
                index.insert(QStringLiteral("furoshikiScope"), games);
            }
            index_json = QJsonDocument(index).toJson(QJsonDocument::Compact).toStdString();
            styles = info.Text(6).toStdString();
        }
        // The rows in runs, by id: a run is a bank.
        const auto runs = [&](const char* sql, std::size_t per, const char* tag, std::vector<std::string>& out) {
            std::vector<qint64> ids;
            SqliteDb::Statement all(db, sql);
            all.Bind(1, id);
            while (all.Step()) {
                ids.push_back(all.Int(0));
            }
            for (std::size_t at = 0; at < ids.size(); at += per) {
                const std::size_t last = std::min(ids.size(), at + per) - 1;
                out.push_back(std::string(tag) + std::to_string(ids[at]) + ":" + std::to_string(ids[last]));
            }
        };
        runs("SELECT id FROM terms WHERE dict = ? ORDER BY id", 10000, "t", terms);
        runs("SELECT id FROM term_meta WHERE dict = ? ORDER BY id", 50000, "m", metas);
        SqliteDb::Statement paths(db, "SELECT path FROM media WHERE dict = ? ORDER BY path");
        paths.Bind(1, id);
        while (paths.Step()) {
            media.push_back("p" + paths.Text(0).toStdString());
        }
    }

    std::string Index() override {
        return index_json;
    }
    std::vector<std::string> TermBanks() override {
        return terms;
    }
    std::vector<std::string> MetaBanks() override {
        return metas;
    }
    std::vector<std::string> Media() override {
        return media;
    }
    std::string MediaPath(const std::string& name) override {
        return name.substr(1);
    }
    std::uint64_t Size(const std::string& name) override {
        return name.front() == 't' ? 10000ull * 1500 : name.front() == 'm' ? 50000ull * 60 : 64 * 1024;
    }
    std::string Read(const std::string& name) override {
        std::lock_guard lock(mutex);
        if (name.front() == 'p') {
            SqliteDb::Statement picture(db, "SELECT data FROM media WHERE dict = ? AND path = ?");
            picture.Bind(1, id).Bind(2, QString::fromStdString(name.substr(1)));
            if (!picture.Step()) {
                throw std::runtime_error("Missing picture in the old dictionaries file: " + name.substr(1));
            }
            return picture.Blob(0).toStdString();
        }
        const std::size_t colon = name.find(':');
        const qint64 from = std::stoll(name.substr(1, colon - 1));
        const qint64 to = std::stoll(name.substr(colon + 1));
        std::string out = "[";
        bool first = true;
        if (name.front() == 't') {
            SqliteDb::Statement rows(db, "SELECT expression, reading, def_tags, rules, score, glossary, sequence, term_tags "
                                         "FROM terms WHERE dict = ? AND id BETWEEN ? AND ? ORDER BY id");
            rows.Bind(1, id).Bind(2, from).Bind(3, to);
            while (rows.Step()) {
                out += first ? "[" : ",[";
                first = false;
                out += Dict::Json::Quote(rows.Text(0).toStdString()) + "," + Dict::Json::Quote(rows.Text(1).toStdString()) +
                       "," + Dict::Json::Quote(rows.Text(2).toStdString()) + "," +
                       Dict::Json::Quote(rows.Text(3).toStdString()) + "," + std::to_string(rows.Int(4)) + "," +
                       DictionaryDatabase::Unpack(rows.Blob(5)).toStdString() + "," + std::to_string(rows.Int(6)) +
                       "," + Dict::Json::Quote(rows.Text(7).toStdString()) + "]";
            }
        } else {
            SqliteDb::Statement rows(db, "SELECT expression, mode, data FROM term_meta WHERE dict = ? AND id BETWEEN ? AND ? "
                                         "ORDER BY id");
            rows.Bind(1, id).Bind(2, from).Bind(3, to);
            while (rows.Step()) {
                out += first ? "[" : ",[";
                first = false;
                const std::string data = rows.Text(2).toStdString();
                out += Dict::Json::Quote(rows.Text(0).toStdString()) + "," + Dict::Json::Quote(rows.Text(1).toStdString()) +
                       "," + (data.empty() ? "null" : data) + "]";
            }
        }
        out += "]";
        return out;
    }
    std::string Styles() override {
        return styles;
    }

private:
    SqliteDb& db;
    std::mutex& mutex;
    qint64 id;
    std::string index_json;
    std::string styles;
    std::vector<std::string> terms;
    std::vector<std::string> metas;
    std::vector<std::string> media;
};

} // namespace
#endif

DictionaryDatabase::DictionaryDatabase(const QString& sqlite_path, const QString& library_folder) {
#ifdef READER_DICT_ENGINE
    engine = std::make_unique<Dict::Library>(library_folder.toStdString());
    const QString stamp = library_folder + QStringLiteral("/converted-from-sqlite");
    old_path = sqlite_path;
    stamp_path = stamp;
    const QString kept = sqlite_path + QStringLiteral(".converted");
    if (QFileInfo::exists(stamp)) {
        // Converted: the old file, if it could not go when the conversion ended, goes now.
        for (const QString& gone : {sqlite_path, sqlite_path + QStringLiteral("-wal"), sqlite_path + QStringLiteral("-shm"),
                                    kept}) {
            QFile::remove(gone);
        }
        Open(QStringLiteral(":memory:"));
        if (engine->Uncovered() > 0) {
            converter = std::thread([this] { engine->Compact(); });
        }
        return;
    }
    Open(sqlite_path);
    bool has_old = false;
    try {
        SqliteDb::Statement any(db, "SELECT 1 FROM dictionaries LIMIT 1");
        has_old = any.Step();
    } catch (const SqliteError&) {
    }
    if (has_old) {
        converting = true;
        converter = std::thread([this, sqlite_path, stamp] { Convert(sqlite_path, stamp); });
    } else {
        QFile file(stamp); // nothing to convert: this library is the dictionaries from now on
        if (file.open(QIODevice::WriteOnly)) {
            file.write("nothing to convert\n");
        }
    }
#else
    Q_UNUSED(library_folder);
    Open(sqlite_path);
#endif
}

QStringList DictionaryDatabase::AbandonConversion() {
    QStringList titles;
#ifdef READER_DICT_ENGINE
    if (!engine || !converting.load()) {
        return titles;
    }
    stopping = true; // between two banks
    if (converter.joinable()) {
        converter.join();
    }
    stopping = false;
    QMutexLocker lock(&mutex);
    if (!converting.load()) {
        // Finished right before we joined: keep the converted library intact.
        return titles;
    }
    try {
        SqliteDb::Statement all(db, "SELECT title FROM dictionaries");
        while (all.Step()) {
            titles.append(all.Text(0));
        }
    } catch (const SqliteError&) {
    }
    std::vector<std::int64_t> moved;
    for (const Dict::DictionaryInfo& info : engine->Dictionaries()) {
        if (titles.contains(QString::fromStdString(info.title))) {
            moved.push_back(info.id);
        }
    }
    engine->Delete(moved);
    Open(QStringLiteral(":memory:"));
    for (const QString& gone : {old_path, old_path + QStringLiteral("-wal"), old_path + QStringLiteral("-shm")}) {
        QFile::remove(gone);
    }
    QFile file(stamp_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QStringLiteral("%1 dictionaries removed by the user, to be imported again\n").arg(titles.size()).toUtf8());
    }
    styles_known = false;
    pictures.clear();
    sqlite_only = false;
    {
        QMutexLocker progress(&conversion_mutex);
        conversion_failed.clear();
    }
    conversion_finished = true;
    converting = false;
    qInfo("[Reader] The old dictionaries file's %lld dictionaries removed by the user, to be imported again",
          static_cast<long long>(titles.size()));
#endif
    return titles;
}

DictionaryDatabase::Conversion DictionaryDatabase::ConversionProgress() const {
    QMutexLocker lock(&conversion_mutex);
    return Conversion{conversion_done.load(),     conversion_total.load(),    conversion_title,
                      conversion_step.load(),     conversion_steps.load(),    conversion_finished.load(),
                      conversion_failed,          conversion_seconds,         conversion_resumed.load()};
}

void DictionaryDatabase::Convert(QString sqlite_path, QString stamp) {
#ifdef READER_DICT_ENGINE
    // A connection of its own to the old file: lookups go on through `db` meanwhile.
    SqliteDb old;
    std::mutex old_mutex;
    try {
        old.Open(sqlite_path);
        struct Old {
            qint64 id;
            QString title;
            bool enabled;
        };
        QList<Old> list;
        {
            SqliteDb::Statement all(old, "SELECT id, title, enabled FROM dictionaries ORDER BY priority, id");
            while (all.Step()) {
                list.append(Old{all.Int(0), all.Text(1), all.Int(2) != 0});
            }
        }
        QElapsedTimer clock;
        clock.start();
        int converted = 0;
        conversion_total = static_cast<int>(list.size());
        conversion_done = 0;
        for (const Old& one : list) {
            if (stopping) {
                return; // the next start goes on where this one stopped
            }
            {
                QMutexLocker lock(&conversion_mutex);
                conversion_title = one.title;
            }
            bool there = false;
            for (const Dict::DictionaryInfo& info : engine->Dictionaries()) {
                there = there || QString::fromStdString(info.title) == one.title;
            }
            if (there) {
                // Moved by an earlier start that was closed halfway: counted as done.
                ++conversion_resumed;
                ++conversion_done;
                continue;
            }
            conversion_step = 0;
            conversion_steps = 0;
            SqliteSource source(old, old_mutex, one.id);
            Dict::ImportOptions options;
            options.keep_going = [this] { return !stopping.load(); };
            options.progress = [this](int done, int total) {
                conversion_step = done;
                conversion_steps = total;
            };
            // Quietly, beside whatever the user does meanwhile: two threads, not every core.
            options.threads = 2;
            const Dict::ImportResult result = engine->ImportFrom(source, options);
            if (stopping) {
                return;
            }
            if (!result.ok) {
                qWarning("[Reader] Converting %s failed: %s", qPrintable(one.title), result.error.c_str());
            }
            ++converted;
            ++conversion_done;
        }
        if (stopping) {
            return;
        }
        // As they were: which are on, and their order.
        std::vector<std::int64_t> order;
        for (const Old& one : list) {
            for (const Dict::DictionaryInfo& info : engine->Dictionaries()) {
                if (QString::fromStdString(info.title) == one.title) {
                    engine->SetEnabled(info.id, one.enabled);
                    order.push_back(info.id);
                }
            }
        }
        engine->SetOrder(order);
        engine->Compact();
        // Every dictionary there, with all its entries? Then the old file goes; else it stays,
        // and the log says which did not come.
        QStringList missing;
        {
            SqliteDb::Statement counts(old, "SELECT title, terms, metas FROM dictionaries");
            while (counts.Step()) {
                const QString title = counts.Text(0);
                bool same = false;
                for (const Dict::DictionaryInfo& info : engine->Dictionaries()) {
                    if (QString::fromStdString(info.title) == title) {
                        same = info.terms == counts.Int(1) && info.metas == counts.Int(2);
                    }
                }
                if (!same) {
                    missing.append(title);
                }
            }
        }
        if (!missing.isEmpty()) {
            qWarning("[Reader] Not converted whole, the old dictionaries file is kept: %s",
                     qPrintable(missing.join(QStringLiteral(", "))));
            // What came short goes, so that the next start moves it again (else it would be
            // passed over as moved, and fail the same way at every start).
            std::vector<std::int64_t> short_ones;
            for (const Dict::DictionaryInfo& info : engine->Dictionaries()) {
                if (missing.contains(QString::fromStdString(info.title))) {
                    short_ones.push_back(info.id);
                }
            }
            engine->Delete(short_ones);
            sqlite_only = true; // this run goes on with the old file
            {
                QMutexLocker lock(&conversion_mutex);
                conversion_failed = missing;
                conversion_seconds = clock.elapsed() / 1000.0;
            }
            conversion_finished = true;
            converting = false;
            return; // no stamp: the next start tries again
        }
        QFile file(stamp);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QStringLiteral("%1 dictionaries converted from %2 in %3 s\n")
                           .arg(converted)
                           .arg(sqlite_path)
                           .arg(clock.elapsed() / 1000.0, 0, 'f', 1)
                           .toUtf8());
        }
        qInfo("[Reader] %d dictionaries converted to the dictionary engine in %.1f s", converted, clock.elapsed() / 1000.0);
        QMutexLocker lock(&conversion_mutex);
        conversion_seconds = clock.elapsed() / 1000.0;
    } catch (const std::exception& error) {
        qWarning("[Reader] Converting the dictionaries stopped: %s", error.what());
        {
            QMutexLocker lock(&conversion_mutex);
            conversion_failed = QStringList{QString::fromUtf8(error.what())};
        }
        // The old file goes on; the next start tries again.
        sqlite_only = true;
        conversion_finished = true;
        converting = false;
        return;
    }
    old.Close();
    {
        // The engine answers from now on; the old file is let go, its data with it.
        QMutexLocker lock(&mutex);
        styles_known = false;
        pictures.clear();
        conversion_finished = true;
        converting = false;
        Open(QStringLiteral(":memory:"));
    }
    for (const QString& gone : {sqlite_path, sqlite_path + QStringLiteral("-wal"), sqlite_path + QStringLiteral("-shm")}) {
        QFile::remove(gone); // what cannot go now (Windows: still open) goes at the next start
    }
#else
    Q_UNUSED(sqlite_path);
    Q_UNUSED(stamp);
#endif
}

DictionaryDatabase::DictionaryDatabase(const QString& path) {
#ifdef READER_DICT_ENGINE
    if (path.startsWith(QStringLiteral("engine:"))) {
        engine = std::make_unique<Dict::Library>(path.mid(7).toStdString());
        Open(QStringLiteral(":memory:"));
        return;
    }
#endif
    Open(path);
}

void DictionaryDatabase::Open(const QString& path) {
    db.Open(path);
    if (db.UserVersion() == 0) {
        SqliteDb::Transaction transaction(db);
        db.Exec("CREATE TABLE IF NOT EXISTS dictionaries ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "title TEXT NOT NULL UNIQUE,"
                "revision TEXT NOT NULL,"
                "author TEXT NOT NULL,"
                "description TEXT NOT NULL,"
                "frequency_mode TEXT NOT NULL,"
                "priority INTEGER NOT NULL,"
                "enabled INTEGER NOT NULL DEFAULT 1,"
                "terms INTEGER NOT NULL DEFAULT 0,"
                "metas INTEGER NOT NULL DEFAULT 0)");
        db.Exec("CREATE TABLE IF NOT EXISTS terms ("
                "id INTEGER PRIMARY KEY,"
                "dict INTEGER NOT NULL,"
                "expression TEXT NOT NULL,"
                "reading TEXT NOT NULL,"
                "def_tags TEXT NOT NULL,"
                "rules TEXT NOT NULL,"
                "score INTEGER NOT NULL,"
                "glossary BLOB NOT NULL,"
                "sequence INTEGER NOT NULL,"
                "term_tags TEXT NOT NULL)");
        db.Exec("CREATE INDEX IF NOT EXISTS terms_expression ON terms(expression)");
        db.Exec("CREATE INDEX IF NOT EXISTS terms_reading ON terms(reading)");
        db.Exec("CREATE TABLE IF NOT EXISTS term_meta ("
                "id INTEGER PRIMARY KEY,"
                "dict INTEGER NOT NULL,"
                "expression TEXT NOT NULL,"
                "mode TEXT NOT NULL,"
                "data TEXT NOT NULL)");
        db.Exec("CREATE INDEX IF NOT EXISTS term_meta_expression ON term_meta(expression)");
        transaction.Commit();
        db.SetUserVersion(1);
    }
    if (db.UserVersion() == 1) {
        // Version 2: the games a dictionary is for (a names dictionary's), "" for every game.
        // The Android app's DictionaryDatabase makes the same change at the same version.
        db.Exec("ALTER TABLE dictionaries ADD COLUMN scope TEXT NOT NULL DEFAULT ''");
        db.SetUserVersion(2);
    }
    if (db.UserVersion() == 2) {
        // Version 3: a dictionary's own stylesheet (styles.css) and the pictures in its zip,
        // which its structured content shows by their paths. The same in the Android app.
        SqliteDb::Transaction transaction(db);
        db.Exec("ALTER TABLE dictionaries ADD COLUMN styles TEXT NOT NULL DEFAULT ''");
        db.Exec("CREATE TABLE IF NOT EXISTS media (dict INTEGER NOT NULL, path TEXT NOT NULL, "
                "data BLOB NOT NULL, PRIMARY KEY(dict, path)) WITHOUT ROWID");
        transaction.Commit();
        db.SetUserVersion(3);
    }
}

void DictionaryDatabase::Changed() {
    QMutexLocker lock(&mutex);
    styles_known = false;
    pictures.clear();
}

QHash<QString, QString> DictionaryDatabase::Styles() {
    auto from_engine = [&] {
        QHash<QString, QString> out;
#ifdef READER_DICT_ENGINE
        for (const auto& [title, css] : engine->Styles()) {
            out.insert(QString::fromStdString(title), QString::fromStdString(css));
        }
#endif
        return out;
    };
    if (UseEngine()) {
        return from_engine();
    }
    QMutexLocker lock(&mutex);
    if (UseEngine()) {
        lock.unlock();
        return from_engine();
    }
    if (!styles_known) {
        styles.clear();
        SqliteDb::Statement statement(
            db, "SELECT title, styles FROM dictionaries WHERE enabled = 1 AND styles != ''");
        while (statement.Step()) {
            styles.insert(statement.Text(0), statement.Text(1));
        }
        styles_known = true;
    }
    return styles;
}

namespace {

/// The paths of the pictures in a glossary: structured content's "img" and Yomitan's image items.
/// The pictures a glossary names: every "path" value in it, in order, once each. Only a
/// structured-content img and an "image" item have one; a path no picture has finds nothing.
/// The text is searched rather than parsed: a glossary is often long, and most have none.
void PathsIn(const QString& glossary_json, QStringList& out) {
    static const QString key = QStringLiteral("\"path\"");
    for (qsizetype at = glossary_json.indexOf(key); at >= 0; at = glossary_json.indexOf(key, at)) {
        qsizetype p = at + key.size();
        while (p < glossary_json.size() && glossary_json.at(p).isSpace()) {
            ++p;
        }
        if (p >= glossary_json.size() || glossary_json.at(p) != u':') {
            at = p;
            continue;
        }
        ++p;
        while (p < glossary_json.size() && glossary_json.at(p).isSpace()) {
            ++p;
        }
        if (p >= glossary_json.size() || glossary_json.at(p) != u'"') {
            at = p;
            continue;
        }
        const qsizetype begin = p;
        bool escaped = false;
        for (++p; p < glossary_json.size() && glossary_json.at(p) != u'"'; ++p) {
            if (glossary_json.at(p) == u'\\') {
                escaped = true;
                ++p;
            }
        }
        if (p >= glossary_json.size()) {
            break;
        }
        QString path = glossary_json.mid(begin + 1, p - begin - 1);
        if (escaped) {
            const QJsonArray decoded =
                QJsonDocument::fromJson('[' + glossary_json.mid(begin, p - begin + 1).toUtf8() + ']').array();
            path = decoded.isEmpty() ? QString() : decoded.at(0).toString();
        }
        if (!path.isEmpty() && !out.contains(path)) {
            out.append(path);
        }
        at = p + 1;
    }
}

QString MimeOf(const QString& path) {
    const QString extension = path.section(u'.', -1).toLower();
    if (extension == QStringLiteral("svg")) return QStringLiteral("image/svg+xml");
    if (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) return QStringLiteral("image/jpeg");
    if (extension == QStringLiteral("ico")) return QStringLiteral("image/x-icon");
    return QStringLiteral("image/") + extension;
}

} // namespace

QHash<QString, QString> DictionaryDatabase::MediaOf(qint64 dictionary_id, const QString& glossary_json) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        QHash<QString, QString> out;
        QStringList paths;
        PathsIn(glossary_json, paths);
        for (const QString& path : paths) {
            const QString key = QString::number(dictionary_id) + u'\n' + path;
            if (const QString* known = pictures.object(key)) {
                out.insert(path, *known);
                continue;
            }
            if (const std::optional<std::string> bytes = engine->Media(dictionary_id, path.toStdString())) {
                const QString value = QStringLiteral("data:") + MimeOf(path) + QStringLiteral(";base64,") +
                                      QString::fromLatin1(QByteArray::fromStdString(*bytes).toBase64());
                out.insert(path, value);
                pictures.insert(key, new QString(value), std::max<qsizetype>(1, value.size()));
            }
        }
        return out;
    }
#endif
    QHash<QString, QString> out;
    if (!glossary_json.contains(QStringLiteral("\"path\""))) {
        return out;
    }
    QStringList paths;
    PathsIn(glossary_json, paths);
    if (paths.isEmpty()) {
        return out;
    }
    std::optional<SqliteDb::Statement> statement;
    for (const QString& path : paths) {
        const QString key = QString::number(dictionary_id) + u'\n' + path;
        if (const QString* known = pictures.object(key)) {
            out.insert(path, *known);
            continue;
        }
        if (!statement) {
            statement.emplace(db, "SELECT data FROM media WHERE dict = ? AND path = ?");
        }
        statement->Bind(1, dictionary_id).Bind(2, path);
        if (statement->Step()) {
            auto* address = new QString(QStringLiteral("data:") + MimeOf(path) + QStringLiteral(";base64,") +
                                        QString::fromLatin1(statement->Blob(0).toBase64()));
            out.insert(path, *address);
            pictures.insert(key, address, std::max<qsizetype>(1, address->size()));
        }
        statement->Reset();
    }
    return out;
}

QString DictionaryDatabase::ScopeText(const QStringList& games) {
    QStringList ids;
    for (const QString& game : games) {
        const QString id = game.trimmed().toUpper();
        if (!id.isEmpty() && !id.contains(u'|') && !ids.contains(id)) {
            ids.append(id);
        }
    }
    return ids.isEmpty() ? QString() : u'|' + ids.join(u'|') + u'|';
}

QStringList DictionaryDatabase::ScopeList(const QString& text) {
    return text.split(u'|', Qt::SkipEmptyParts);
}

QList<DictionaryInfo> DictionaryDatabase::Dictionaries() {
    auto from_engine = [&] {
        QList<DictionaryInfo> out;
#ifdef READER_DICT_ENGINE
        for (const Dict::DictionaryInfo& info : engine->Dictionaries()) {
            out.append(DictionaryInfo{info.id, QString::fromStdString(info.title), QString::fromStdString(info.revision),
                                      QString::fromStdString(info.author), QString::fromStdString(info.description),
                                      QString::fromStdString(info.frequency_mode), info.priority, info.enabled,
                                      info.terms, info.metas, ScopeList(QString::fromStdString(info.scope)), info.media, info.kanji});
        }
#endif
        return out;
    };
    if (UseEngine()) {
        return from_engine();
    }
    QMutexLocker lock(&mutex);
    if (UseEngine()) {
        lock.unlock();
        return from_engine();
    }
    QList<DictionaryInfo> out;
    SqliteDb::Statement statement(
        db, "SELECT id, title, revision, author, description, frequency_mode, priority, enabled, "
            "terms, metas, scope FROM dictionaries ORDER BY priority, id");
    while (statement.Step()) {
        out.append(DictionaryInfo{
            statement.Int(0), statement.Text(1), statement.Text(2), statement.Text(3),
            statement.Text(4), statement.Text(5), static_cast<int>(statement.Int(6)),
            statement.Int(7) != 0, statement.Int(8), statement.Int(9),
            ScopeList(statement.Text(10))});
    }
    return out;
}

bool DictionaryDatabase::HasEnabledDictionaries() {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        return engine->HasEnabled();
    }
#endif
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        lock.unlock();
        return engine->HasEnabled();
    }
#endif
    SqliteDb::Statement statement(db, "SELECT 1 FROM dictionaries WHERE enabled = 1 LIMIT 1");
    return statement.Step();
}

void DictionaryDatabase::SetEnabled(qint64 id, bool enabled) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        engine->SetEnabled(id, enabled);
        return;
    }
#endif
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        lock.unlock();
        engine->SetEnabled(id, enabled);
        return;
    }
#endif
    SqliteDb::Statement statement(db, "UPDATE dictionaries SET enabled = ? WHERE id = ?");
    statement.Bind(1, enabled ? 1 : 0).Bind(2, id).Run();
    styles_known = false;
}

void DictionaryDatabase::SetEnabled(const QList<qint64>& ids, bool enabled) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        engine->SetEnabled(std::vector<std::int64_t>(ids.begin(), ids.end()), enabled);
        QMutexLocker lock(&mutex);
        styles_known = false;
        return;
    }
#endif
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        styles_known = false;
        lock.unlock();
        engine->SetEnabled(std::vector<std::int64_t>(ids.begin(), ids.end()), enabled);
        return;
    }
#endif
    SqliteDb::Transaction transaction(db);
    SqliteDb::Statement statement(db, "UPDATE dictionaries SET enabled = ? WHERE id = ?");
    for (const qint64 id : ids) {
        statement.Bind(1, enabled ? 1 : 0).Bind(2, id).Run();
    }
    transaction.Commit();
    styles_known = false;
}

void DictionaryDatabase::Delete(const QList<qint64>& ids) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        engine->Delete(std::vector<std::int64_t>(ids.begin(), ids.end()));
        QMutexLocker lock(&mutex);
        styles_known = false;
        pictures.clear();
        return;
    }
#endif
    for (const qint64 id : ids) {
        Delete(id);
    }
}

void DictionaryDatabase::SetOrder(const QList<qint64>& ids) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        engine->SetOrder(std::vector<std::int64_t>(ids.begin(), ids.end()));
        return;
    }
#endif
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        lock.unlock();
        engine->SetOrder(std::vector<std::int64_t>(ids.begin(), ids.end()));
        return;
    }
#endif
    SqliteDb::Transaction transaction(db);
    SqliteDb::Statement statement(db, "UPDATE dictionaries SET priority = ? WHERE id = ?");
    for (int index = 0; index < ids.size(); ++index) {
        statement.Bind(1, index).Bind(2, ids.at(index)).Run();
    }
    transaction.Commit();
}

int DictionaryDatabase::TakeFrom(const QString& other_path) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        return 0; // phase 2: converting reader_dict.db
    }
#endif
    QMutexLocker lock(&mutex);
    if (!QFileInfo::exists(other_path)) {
        return 0;
    }
    SqliteDb::Statement(db, "ATTACH DATABASE ? AS other").Bind(1, other_path).Run();
    int taken = 0;
    try {
        struct Theirs {
            qint64 id;
            QString title;
        };
        QList<Theirs> missing;
        {
            SqliteDb::Statement statement(
                db, "SELECT id, title FROM other.dictionaries WHERE title NOT IN "
                    "(SELECT title FROM main.dictionaries) ORDER BY priority, id");
            while (statement.Step()) {
                missing.append(Theirs{statement.Int(0), statement.Text(1)});
            }
        }
        // A file from before scopes has none: its dictionaries are for every game.
        bool scoped = false;
        {
            SqliteDb::Statement column(
                db, "SELECT 1 FROM pragma_table_info('dictionaries', 'other') WHERE name = 'scope'");
            scoped = column.Step();
        }
        // Nor has a file from before version 3 styles or pictures.
        bool styled = false;
        {
            SqliteDb::Statement column(
                db, "SELECT 1 FROM pragma_table_info('dictionaries', 'other') WHERE name = 'styles'");
            styled = column.Step();
        }
        const std::string insert =
            std::string("INSERT INTO main.dictionaries (title, revision, author, description, "
                        "frequency_mode, priority, enabled, terms, metas, scope, styles) SELECT title, "
                        "revision, author, description, frequency_mode, (SELECT "
                        "COALESCE(MAX(priority), -1) + 1 FROM main.dictionaries), enabled, terms, "
                        "metas, ") +
            (scoped ? "scope" : "''") + ", " + (styled ? "styles" : "''") +
            " FROM other.dictionaries WHERE id = ?";
        SqliteDb::Transaction transaction(db);
        for (const Theirs& theirs : missing) {
            SqliteDb::Statement(db, insert.c_str()).Bind(1, theirs.id).Run();
            const qint64 mine = db.LastInsertId();
            SqliteDb::Statement(
                db, "INSERT INTO main.terms (dict, expression, reading, def_tags, rules, score, "
                    "glossary, sequence, term_tags) SELECT ?, expression, reading, def_tags, rules, "
                    "score, glossary, sequence, term_tags FROM other.terms WHERE dict = ?")
                .Bind(1, mine)
                .Bind(2, theirs.id)
                .Run();
            SqliteDb::Statement(db, "INSERT INTO main.term_meta (dict, expression, mode, data) "
                                    "SELECT ?, expression, mode, data FROM other.term_meta WHERE dict = ?")
                .Bind(1, mine)
                .Bind(2, theirs.id)
                .Run();
            if (styled) {
                SqliteDb::Statement(db, "INSERT OR REPLACE INTO main.media (dict, path, data) "
                                        "SELECT ?, path, data FROM other.media WHERE dict = ?")
                    .Bind(1, mine)
                    .Bind(2, theirs.id)
                    .Run();
            }
            ++taken;
        }
        transaction.Commit();
        styles_known = false;
        pictures.clear();
    } catch (...) {
        db.Exec("DETACH DATABASE other");
        throw;
    }
    db.Exec("DETACH DATABASE other");
    return taken;
}

void DictionaryDatabase::Delete(qint64 id) {
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        engine->Delete(id);
        QMutexLocker lock(&mutex);
        styles_known = false;
        pictures.clear();
        return;
    }
#endif
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        styles_known = false;
        pictures.clear();
        lock.unlock();
        engine->Delete(id);
        return;
    }
#endif
    SqliteDb::Transaction transaction(db);
    for (const char* sql : {"DELETE FROM terms WHERE dict = ?", "DELETE FROM term_meta WHERE dict = ?",
                            "DELETE FROM media WHERE dict = ?", "DELETE FROM dictionaries WHERE id = ?"}) {
        SqliteDb::Statement statement(db, sql);
        statement.Bind(1, id).Run();
    }
    transaction.Commit();
    styles_known = false;
    pictures.clear();
}

QList<TermEntry> DictionaryDatabase::FindTerms(const QStringList& terms, const QString& game,
                                               bool with_glossaries) {
    QList<TermEntry> out;
    if (terms.isEmpty()) {
        return out;
    }
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        std::vector<std::string> keys;
        keys.reserve(static_cast<std::size_t>(terms.size()));
        for (const QString& term : terms) {
            keys.push_back(term.toStdString());
        }
        for (const Dict::Term& term : engine->FindTerms(keys, game.trimmed().toStdString())) {
            TermEntry entry;
            entry.id = (term.dictionary_id << 32) | term.entry;
            entry.dictionary_id = term.dictionary_id;
            entry.dictionary = QString::fromStdString(term.dictionary);
            entry.priority = term.priority;
            entry.expression = QString::fromStdString(term.expression);
            entry.reading = QString::fromStdString(term.reading);
            entry.definition_tags = QString::fromStdString(term.definition_tags);
            entry.rules = QString::fromStdString(term.rules);
            entry.score = term.score;
            entry.sequence = term.sequence;
            entry.term_tags = QString::fromStdString(term.term_tags);
            entry.scoped = term.scoped;
            if (with_glossaries) {
                entry.glossary_json = QString::fromStdString(engine->Definition(term.dictionary_id, term.entry));
                entry.media = MediaOf(entry.dictionary_id, entry.glossary_json);
            }
            out.append(entry);
        }
        return out;
    }
#endif
    QSet<qint64> seen;
    for (qsizetype from = 0; from < terms.size(); from += MaxParameters) {
        const qsizetype count = std::min<qsizetype>(MaxParameters, terms.size() - from);
        for (const char* column : {"expression", "reading"}) {
            const std::string sql =
                std::string("SELECT t.id, t.dict, d.title, d.priority, t.expression, t.reading, "
                            "t.def_tags, t.rules, t.score, " +
                            std::string(with_glossaries ? "t.glossary" : "NULL") +
                            ", t.sequence, t.term_tags, "
                            "d.scope != '' FROM terms t JOIN dictionaries d ON d.id = t.dict WHERE t.") +
                column + " IN (" + Placeholders(count) +
                ") AND d.enabled = 1 AND (d.scope = '' OR instr(d.scope, ?) > 0)";
            SqliteDb::Statement statement(db, sql.c_str());
            for (qsizetype i = 0; i < count; ++i) {
                statement.Bind(static_cast<int>(i) + 1, terms.at(from + i));
            }
            // No game: "||", which no scope holds.
            statement.Bind(static_cast<int>(count) + 1,
                           u'|' + game.trimmed().toUpper() + u'|');
            while (statement.Step()) {
                const qint64 row = statement.Int(0);
                if (seen.contains(row)) {
                    continue;
                }
                seen.insert(row);
                TermEntry entry;
                entry.id = row;
                entry.dictionary_id = statement.Int(1);
                entry.dictionary = statement.Text(2);
                entry.priority = static_cast<int>(statement.Int(3));
                entry.expression = statement.Text(4);
                entry.reading = statement.Text(5);
                entry.definition_tags = statement.Text(6);
                entry.rules = statement.Text(7);
                entry.score = static_cast<int>(statement.Int(8));
                entry.sequence = statement.Int(10);
                entry.term_tags = statement.Text(11);
                entry.scoped = statement.Int(12) != 0;
                if (with_glossaries) {
                    entry.glossary_json = Unpack(statement.Blob(9));
                    entry.media = MediaOf(entry.dictionary_id, entry.glossary_json);
                }
                out.append(entry);
            }
        }
    }
    return out;
}

void DictionaryDatabase::ReadGlossaries(QList<TermEntry*> entries) {
    if (entries.isEmpty()) {
        return;
    }
    QMutexLocker lock(&mutex);
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        for (TermEntry* entry : entries) {
            entry->glossary_json = QString::fromStdString(
                engine->Definition(entry->dictionary_id, static_cast<std::uint32_t>(entry->id & 0xFFFFFFFF)));
            entry->media = MediaOf(entry->dictionary_id, entry->glossary_json);
        }
        return;
    }
#endif
    QHash<qint64, QList<TermEntry*>> by_id;
    QList<qint64> ids;
    for (TermEntry* entry : entries) {
        auto found = by_id.find(entry->id);
        if (found == by_id.end()) {
            found = by_id.insert(entry->id, {});
            ids.append(entry->id);
        }
        found->append(entry);
    }
    for (qsizetype from = 0; from < ids.size(); from += MaxParameters) {
        const qsizetype count = std::min<qsizetype>(MaxParameters, ids.size() - from);
        const std::string sql =
            "SELECT id, glossary FROM terms WHERE id IN (" + Placeholders(count) + ")";
        SqliteDb::Statement statement(db, sql.c_str());
        for (qsizetype i = 0; i < count; ++i) {
            statement.Bind(static_cast<int>(i) + 1, ids.at(from + i));
        }
        while (statement.Step()) {
            const QList<TermEntry*> same = by_id.value(statement.Int(0));
            if (same.isEmpty()) {
                continue;
            }
            const QString glossary = Unpack(statement.Blob(1));
            const QHash<QString, QString> media = MediaOf(same.first()->dictionary_id, glossary);
            for (TermEntry* entry : same) {
                entry->glossary_json = glossary;
                entry->media = media;
            }
        }
    }
}

QList<KanjiEntry> DictionaryDatabase::FindKanji(const QString& character) {
    QList<KanjiEntry> out;
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        for (const Dict::Kanji& kanji : engine->FindKanji(character.toStdString())) {
            KanjiEntry entry;
            entry.dictionary_id = kanji.dictionary_id;
            entry.dictionary = QString::fromStdString(kanji.dictionary);
            entry.character = character;
            entry.onyomi = QString::fromStdString(kanji.onyomi).split(u' ', Qt::SkipEmptyParts);
            entry.kunyomi = QString::fromStdString(kanji.kunyomi).split(u' ', Qt::SkipEmptyParts);
            entry.tags = QString::fromStdString(kanji.tags);
            for (const QJsonValue& meaning : QJsonDocument::fromJson(QByteArray::fromStdString(kanji.meanings)).array()) {
                entry.meanings.append(meaning.toString());
            }
            entry.stats = QJsonDocument::fromJson(QByteArray::fromStdString(kanji.stats)).object();
            out.append(entry);
        }
    }
#else
    Q_UNUSED(character);
#endif
    return out;
}

QList<TermMeta> DictionaryDatabase::FindKanjiMeta(const QString& character) {
    QList<TermMeta> out;
#ifdef READER_DICT_ENGINE
    if (UseEngine()) {
        for (const Dict::Meta& meta : engine->FindKanjiMeta(character.toStdString())) {
            out.append(TermMeta{QString::fromStdString(meta.dictionary), QString::fromStdString(meta.frequency_mode),
                                QString::fromStdString(meta.expression), QString::fromStdString(meta.mode),
                                QString::fromStdString(meta.data)});
        }
    }
#else
    Q_UNUSED(character);
#endif
    return out;
}

QList<TermMeta> DictionaryDatabase::FindMeta(const QStringList& expressions) {
    QList<TermMeta> out;
    if (expressions.isEmpty()) {
        return out;
    }
    auto from_engine = [&] {
        QList<TermMeta> found;
#ifdef READER_DICT_ENGINE
        std::vector<std::string> keys;
        for (const QString& expression : expressions) {
            keys.push_back(expression.toStdString());
        }
        for (const Dict::Meta& meta : engine->FindMeta(keys)) {
            found.append(TermMeta{QString::fromStdString(meta.dictionary), QString::fromStdString(meta.frequency_mode),
                                  QString::fromStdString(meta.expression), QString::fromStdString(meta.mode),
                                  QString::fromStdString(meta.data)});
        }
#endif
        return found;
    };
    if (UseEngine()) {
        return from_engine();
    }
    QMutexLocker lock(&mutex);
    if (UseEngine()) {
        lock.unlock();
        return from_engine();
    }
    for (qsizetype from = 0; from < expressions.size(); from += MaxParameters) {
        const qsizetype count = std::min<qsizetype>(MaxParameters, expressions.size() - from);
        const std::string sql =
            "SELECT d.title, d.frequency_mode, m.expression, m.mode, m.data "
            "FROM term_meta m JOIN dictionaries d ON d.id = m.dict "
            "WHERE m.expression IN (" +
            Placeholders(count) + ") AND d.enabled = 1 ORDER BY d.priority, d.id";
        SqliteDb::Statement statement(db, sql.c_str());
        for (qsizetype i = 0; i < count; ++i) {
            statement.Bind(static_cast<int>(i) + 1, expressions.at(from + i));
        }
        while (statement.Step()) {
            out.append(TermMeta{statement.Text(0), statement.Text(1), statement.Text(2),
                                statement.Text(3), statement.Text(4)});
        }
    }
    return out;
}

QByteArray DictionaryDatabase::Pack(const QString& json) {
    return Pack(json.toUtf8());
}

namespace {

/// One compressor for each thread, reset between glossaries: compress2 made and cleared some
/// 256 kB of zlib's state for every glossary, which took most of an import's time (JA
/// Wikipedia: 37 s of CPU for 1.3 million glossaries) and on Windows, where threads wait for
/// each other to allocate so much, made it wait 83 s. The bytes made are the same.
struct Deflater {
    z_stream stream{};
    bool ready = false;
    Deflater() {
        ready = deflateInit(&stream, Z_BEST_SPEED) == Z_OK;
    }
    ~Deflater() {
        if (ready) {
            deflateEnd(&stream);
        }
    }
};

} // namespace

QByteArray DictionaryDatabase::Pack(const QByteArray& raw) {
    if (raw.size() < PackThreshold) {
        return QByteArray(1, PackRaw) + raw;
    }
    thread_local Deflater deflater;
    if (!deflater.ready || deflateReset(&deflater.stream) != Z_OK) {
        return QByteArray(1, PackRaw) + raw;
    }
    z_stream& stream = deflater.stream;
    const uLong bound = deflateBound(&stream, static_cast<uLong>(raw.size()));
    QByteArray out(static_cast<qsizetype>(bound) + 1, Qt::Uninitialized);
    out[0] = PackDeflate;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
    stream.avail_in = static_cast<uInt>(raw.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data() + 1);
    stream.avail_out = static_cast<uInt>(bound);
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
        return QByteArray(1, PackRaw) + raw;
    }
    out.resize(static_cast<qsizetype>(stream.total_out) + 1);
    return out;
}

QString DictionaryDatabase::Unpack(const QByteArray& blob) {
    if (blob.isEmpty()) {
        return QStringLiteral("[]");
    }
    if (blob.at(0) == PackRaw) {
        return QString::fromUtf8(blob.constData() + 1, blob.size() - 1);
    }
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return QStringLiteral("[]");
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(blob.constData() + 1));
    stream.avail_in = static_cast<uInt>(blob.size() - 1);
    QByteArray out;
    char buffer[4096];
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            break;
        }
        out.append(buffer, static_cast<qsizetype>(sizeof(buffer) - stream.avail_out));
    }
    inflateEnd(&stream);
    return QString::fromUtf8(out);
}

// ---- Lookup ----

namespace {

struct Candidate {
    int source_length;
    Deinflection deinflection;
};

bool IsBetter(const Candidate& a, const Candidate& b) {
    if (a.source_length != b.source_length) {
        return a.source_length > b.source_length;
    }
    return a.deinflection.reasons.size() < b.deinflection.reasons.size();
}

/// The leading part of `text` worth scanning: up to the first break, at most a few words.
QString Scannable(const QString& text) {
    int end = 0;
    while (end < text.size() && end < DictionaryLookup::MaxScanLength) {
        const char32_t cp = CodePointAt(text, end);
        if (!JapaneseText::IsTermChar(cp) && cp != U'々' && cp != U'〇' && cp != U'・' &&
            cp != U'～' && cp != U'〜') {
            break;
        }
        end += CharCountAt(text, end);
    }
    return text.left(end);
}

/// `source` as written plus the spellings a dictionary may use for it.
QStringList Variants(const QString& source) {
    QStringList out;
    const auto add = [&out](const QString& value) {
        if (!out.contains(value)) {
            out.append(value);
        }
    };
    add(source);
    add(JapaneseText::KatakanaToHiragana(source));
    const QString ascii = JapaneseText::FullWidthToAscii(source);
    add(ascii);
    if (std::any_of(ascii.begin(), ascii.end(),
                    [](QChar c) { return c.unicode() >= u'A' && c.unicode() <= u'Z'; })) {
        add(ascii.toLower());
    }
    return out;
}

/// A meta row's JSON value, whatever its type (a bare number or string is allowed).
QJsonValue ParseValue(const QString& json) {
    const QByteArray wrapped = "[" + json.trimmed().toUtf8() + "]";
    const QJsonDocument document = QJsonDocument::fromJson(wrapped);
    if (!document.isArray() || document.array().isEmpty()) {
        return QJsonValue(QJsonValue::Undefined);
    }
    return document.array().at(0);
}

QString FormatNumber(double value) {
    if (value == std::floor(value) && std::isfinite(value)) {
        return QString::number(static_cast<qint64>(value));
    }
    return QString::number(value, 'g', 15);
}

QList<Frequency> Frequencies(const QList<TermMeta>& meta, const QString& reading) {
    QList<Frequency> out;
    for (const TermMeta& m : meta) {
        if (m.mode != QStringLiteral("freq")) {
            continue;
        }
        QJsonValue value = ParseValue(m.data);
        if (value.isObject() && value.toObject().contains(QStringLiteral("frequency"))) {
            // {reading, frequency}: only for the matching reading.
            const QJsonObject object = value.toObject();
            const QString for_reading = object.value(QStringLiteral("reading")).toString();
            if (!reading.isEmpty() && !for_reading.isEmpty() &&
                JapaneseText::KatakanaToHiragana(for_reading) !=
                    JapaneseText::KatakanaToHiragana(reading)) {
                continue;
            }
            value = object.value(QStringLiteral("frequency"));
        }
        std::optional<double> number;
        QString display;
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("value")).isDouble()) {
                number = object.value(QStringLiteral("value")).toDouble();
            }
            display = object.value(QStringLiteral("displayValue")).toString();
            if (display.isEmpty() && number) {
                display = FormatNumber(*number);
            }
        } else if (value.isDouble()) {
            number = value.toDouble();
            display = FormatNumber(*number);
        } else if (value.isString()) {
            display = value.toString();
            bool ok = false;
            const double parsed = display.toDouble(&ok);
            if (ok) {
                number = parsed;
            }
        } else {
            continue;
        }
        if (display.isEmpty()) {
            continue;
        }
        out.append(Frequency{m.dictionary, display, number,
                             m.frequency_mode != QStringLiteral("occurrence-based")});
    }
    return out;
}

/// Where a pattern of high and low morae ("LHHL") falls: after which mora, 0 for none; -1 for no pattern.
int PatternPosition(const QString& pattern) {
    if (pattern.isEmpty()) {
        return -1;
    }
    for (const QChar c : pattern) {
        if (c != u'H' && c != u'L') {
            return -1;
        }
    }
    for (qsizetype i = 0; i + 1 < pattern.size(); ++i) {
        if (pattern.at(i) == u'H' && pattern.at(i + 1) == u'L') {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

QList<PitchAccent> Pitches(const QList<TermMeta>& meta, const QString& reading) {
    QList<PitchAccent> out;
    const QString wanted = JapaneseText::KatakanaToHiragana(reading);
    for (const TermMeta& m : meta) {
        if (m.mode != QStringLiteral("pitch")) {
            continue;
        }
        const QJsonValue value = ParseValue(m.data);
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString for_reading = object.value(QStringLiteral("reading")).toString();
        if (JapaneseText::KatakanaToHiragana(for_reading) != wanted) {
            continue;
        }
        QList<int> positions;
        for (const QJsonValue& pitch : object.value(QStringLiteral("pitches")).toArray()) {
            const QJsonValue position = pitch.toObject().value(QStringLiteral("position"));
            if (position.isDouble()) {
                positions.append(position.toInt());
            } else if (const int from_pattern = PatternPosition(position.toString()); from_pattern >= 0) {
                positions.append(from_pattern); // Yomitan's other form: high and low by mora ("LHHL")
            }
        }
        if (!positions.isEmpty()) {
            out.append(PitchAccent{m.dictionary, for_reading, positions});
        }
    }
    return out;
}

/// A headword is one expression + reading. Kept, per headword: the best way it matched.
struct Headword {
    QString expression;
    QString reading;
    int source_length{};
    QStringList reasons;
    bool validated{};
    QList<TermEntry> entries;

    [[nodiscard]] int BestPriority() const {
        int best = std::numeric_limits<int>::max();
        for (const TermEntry& entry : entries) {
            best = std::min(best, entry.priority);
        }
        return best;
    }
    /// Whether a dictionary for the running game only has it: a name in this game.
    [[nodiscard]] bool Scoped() const {
        return std::any_of(entries.begin(), entries.end(),
                           [](const TermEntry& entry) { return entry.scoped; });
    }
    [[nodiscard]] int BestScore() const {
        int best = std::numeric_limits<int>::min();
        for (const TermEntry& entry : entries) {
            best = std::max(best, entry.score);
        }
        return best;
    }
};

} // namespace

QList<LookupResult> DictionaryLookup::Lookup(const QString& text, int max_results,
                                             const QString& game) {
    const QString scan = Scannable(text);
    if (scan.isEmpty()) {
        return {};
    }

    // Every (prefix, deinflection) pair, keyed by the term to look for.
    QHash<QString, QList<Candidate>> by_term;
    QStringList terms; // in the order first met, so that results do not depend on hashing
    for (int length = static_cast<int>(scan.size()); length > 0; --length) {
        if (scan.at(length - 1).isHighSurrogate()) {
            continue;
        }
        const QString source = scan.left(length);
        for (const QString& variant : Variants(source)) {
            for (const Deinflection& deinflection : deinflector.Deinflect(variant)) {
                auto found = by_term.find(deinflection.term);
                if (found == by_term.end()) {
                    found = by_term.insert(deinflection.term, {});
                    terms.append(deinflection.term);
                }
                found->append(Candidate{length, deinflection});
            }
        }
    }

    const QList<TermEntry> rows = database.FindTerms(terms, game, false);
    if (rows.isEmpty()) {
        return {};
    }

    QList<Headword> headwords;
    QHash<QString, int> headword_index;
    for (const TermEntry& row : rows) {
        const int word_class = WordClass::Parse(row.rules);
        const Candidate* best = nullptr;
        bool best_validated = false;
        for (const QString& key : {row.expression, row.reading}) {
            if (key.isEmpty()) {
                continue;
            }
            const auto found = by_term.constFind(key);
            if (found == by_term.constEnd()) {
                continue;
            }
            for (const Candidate& candidate : *found) {
                const int required = candidate.deinflection.conditions;
                const bool exact = required == 0;
                const bool class_matches = exact || (required & word_class) != 0;
                // A dictionary without word classes cannot confirm an inflection itself; such
                // rows are only kept when another dictionary confirms the headword.
                const bool acceptable = class_matches || word_class == 0;
                if (!acceptable) {
                    continue;
                }
                const bool better = best == nullptr || (class_matches && !best_validated) ||
                                    (class_matches == best_validated && IsBetter(candidate, *best));
                if (better) {
                    best = &candidate;
                    best_validated = class_matches;
                }
            }
        }
        if (best == nullptr) {
            continue;
        }
        const QString key = row.expression + QChar(u'\0') + row.reading;
        auto index = headword_index.constFind(key);
        if (index == headword_index.constEnd()) {
            headwords.append(Headword{row.expression, row.reading, 0, {}, false, {}});
            index = headword_index.insert(key, static_cast<int>(headwords.size()) - 1);
        }
        Headword& headword = headwords[*index];
        headword.entries.append(row);
        if (best_validated) {
            const bool improves =
                !headword.validated || best->source_length > headword.source_length ||
                (best->source_length == headword.source_length &&
                 best->deinflection.reasons.size() < headword.reasons.size());
            if (improves) {
                headword.source_length = best->source_length;
                headword.reasons = best->deinflection.reasons;
            }
            headword.validated = true;
        } else if (!headword.validated && best->source_length > headword.source_length) {
            headword.source_length = best->source_length;
            headword.reasons = best->deinflection.reasons;
        }
    }

    QList<Headword> confirmed;
    for (const Headword& headword : headwords) {
        if (headword.validated) {
            confirmed.append(headword);
        }
    }
    if (confirmed.isEmpty()) {
        return {};
    }
    std::stable_sort(confirmed.begin(), confirmed.end(), [](const Headword& a, const Headword& b) {
        if (a.source_length != b.source_length) {
            return a.source_length > b.source_length;
        }
        // A name in the running game before any other word of its length.
        const bool a_scoped = a.Scoped();
        const bool b_scoped = b.Scoped();
        if (a_scoped != b_scoped) {
            return a_scoped;
        }
        if (a.reasons.size() != b.reasons.size()) {
            return a.reasons.size() < b.reasons.size();
        }
        const int a_priority = a.BestPriority();
        const int b_priority = b.BestPriority();
        if (a_priority != b_priority) {
            return a_priority < b_priority;
        }
        const int a_score = a.BestScore();
        const int b_score = b.BestScore();
        if (a_score != b_score) {
            return a_score > b_score;
        }
        return a.expression < b.expression;
    });
    if (confirmed.size() > max_results) {
        confirmed.resize(max_results);
    }

    QStringList expressions;
    for (const Headword& headword : confirmed) {
        if (!expressions.contains(headword.expression)) {
            expressions.append(headword.expression);
        }
    }
    QHash<QString, QList<TermMeta>> metas;
    for (const TermMeta& meta : database.FindMeta(expressions)) {
        metas[meta.expression].append(meta);
    }

    QList<LookupResult> out;
    for (Headword& headword : confirmed) {
        const QList<TermMeta> meta = metas.value(headword.expression);
        std::stable_sort(headword.entries.begin(), headword.entries.end(),
                         [](const TermEntry& a, const TermEntry& b) {
                             if (a.scoped != b.scoped) {
                                 return a.scoped;
                             }
                             if (a.priority != b.priority) {
                                 return a.priority < b.priority;
                             }
                             return a.score > b.score;
                         });
        LookupResult result;
        result.expression = headword.expression;
        result.reading = headword.reading;
        result.matched_text =
            text.left(std::min<qsizetype>(headword.source_length, text.size()));
        result.reasons = headword.reasons;
        result.entries = headword.entries;
        result.frequencies = Frequencies(meta, headword.reading);
        result.pitches =
            Pitches(meta, headword.reading.isEmpty() ? headword.expression : headword.reading);
        out.append(result);
    }
    QList<TermEntry*> kept;
    for (LookupResult& result : out) {
        for (TermEntry& entry : result.entries) {
            kept.append(&entry);
        }
    }
    database.ReadGlossaries(kept);
    return out;
}

std::optional<qint64> DictionaryLookup::HarmonicRank(const QList<Frequency>& frequencies) {
    int count = 0;
    double sum = 0.0;
    for (const Frequency& frequency : frequencies) {
        if (frequency.rank_based && frequency.value && *frequency.value > 0) {
            ++count;
            sum += 1.0 / *frequency.value;
        }
    }
    if (count == 0) {
        return std::nullopt;
    }
    return static_cast<qint64>(std::llround(count / sum));
}

} // namespace ReaderApp
