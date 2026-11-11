// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string>
#include <vector>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMutexLocker>
#include <QVariant>
#include "reader/desktop/reader_database.h"

namespace ReaderApp {

namespace {

constexpr const char* SumColumns =
    "COALESCE(SUM(active_ms), 0), COALESCE(SUM(chars), 0), COALESCE(SUM(unique_chars), 0), "
    "COALESCE(SUM(reread_chars), 0), COALESCE(SUM(lines), 0), COALESCE(SUM(choice_chars), 0)";

ReadingCounters CountersFrom(const SqliteDb::Statement& statement, int first) {
    return ReadingCounters{statement.Int(first),     statement.Int(first + 1),
                           statement.Int(first + 2), statement.Int(first + 3),
                           statement.Int(first + 4), statement.Int(first + 5)};
}

SessionRecord SessionFrom(const SqliteDb::Statement& statement, const ReadingCounters& counters) {
    SessionRecord record;
    record.id = statement.Int(0);
    record.game = statement.Text(1);
    record.started = statement.Int(2);
    record.last_activity = statement.Int(3);
    if (!statement.IsNull(4)) {
        record.ended = statement.Int(4);
    }
    record.counters = counters;
    return record;
}

QJsonObject CountersJson(const ReadingCounters& c) {
    QJsonObject out;
    out.insert(QStringLiteral("activeMs"), c.active_ms);
    out.insert(QStringLiteral("characters"), c.characters);
    out.insert(QStringLiteral("uniqueCharacters"), c.unique_characters);
    out.insert(QStringLiteral("rereadCharacters"), c.reread_characters);
    out.insert(QStringLiteral("lines"), c.lines);
    out.insert(QStringLiteral("choiceCharacters"), c.choice_characters);
    return out;
}

void BindAll(SqliteDb::Statement& statement, const QList<QString>& arguments) {
    for (int i = 0; i < arguments.size(); ++i) {
        statement.Bind(i + 1, arguments.at(i));
    }
}

} // namespace

ReaderDatabase::ReaderDatabase(const QString& path) {
    db.Open(path);
    if (db.UserVersion() == 0) {
        SqliteDb::Transaction transaction(db);
        db.Exec("CREATE TABLE IF NOT EXISTS lines ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "game TEXT NOT NULL,"
                "session_id INTEGER,"
                "ts INTEGER NOT NULL,"
                "channel TEXT NOT NULL,"
                "speaker TEXT NOT NULL,"
                "text TEXT NOT NULL,"
                "ruby TEXT NOT NULL,"
                "source_id TEXT NOT NULL,"
                "meta TEXT NOT NULL,"
                "chars INTEGER NOT NULL,"
                "reread INTEGER NOT NULL)");
        db.Exec("CREATE INDEX IF NOT EXISTS lines_game ON lines(game, id)");
        db.Exec("CREATE TABLE IF NOT EXISTS seen ("
                "game TEXT NOT NULL,"
                "source_id TEXT NOT NULL,"
                "first_ts INTEGER NOT NULL,"
                "count INTEGER NOT NULL,"
                "PRIMARY KEY (game, source_id)) WITHOUT ROWID");
        db.Exec("CREATE TABLE IF NOT EXISTS sessions ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "game TEXT NOT NULL,"
                "started INTEGER NOT NULL,"
                "last_activity INTEGER NOT NULL,"
                "ended INTEGER)");
        db.Exec("CREATE INDEX IF NOT EXISTS sessions_game ON sessions(game, id)");
        db.Exec("CREATE TABLE IF NOT EXISTS daily ("
                "session_id INTEGER NOT NULL,"
                "day TEXT NOT NULL,"
                "game TEXT NOT NULL,"
                "active_ms INTEGER NOT NULL DEFAULT 0,"
                "chars INTEGER NOT NULL DEFAULT 0,"
                "unique_chars INTEGER NOT NULL DEFAULT 0,"
                "reread_chars INTEGER NOT NULL DEFAULT 0,"
                "lines INTEGER NOT NULL DEFAULT 0,"
                "choice_chars INTEGER NOT NULL DEFAULT 0,"
                "PRIMARY KEY (session_id, day)) WITHOUT ROWID");
        db.Exec("CREATE INDEX IF NOT EXISTS daily_game ON daily(game, day)");
        db.Exec("CREATE TABLE IF NOT EXISTS games ("
                "game TEXT PRIMARY KEY, title TEXT NOT NULL) WITHOUT ROWID");
        transaction.Commit();
        db.SetUserVersion(1);
    }
    if (db.UserVersion() == 1) {
        // Version 2 counts Japanese characters only: the same tables, counted again.
        RecountCharacters();
        db.SetUserVersion(2);
    }
    if (db.UserVersion() == 2) {
        // Version 3 keeps the words looked up, per game.
        db.Exec("CREATE TABLE IF NOT EXISTS lookups ("
                "expression TEXT NOT NULL,"
                "reading TEXT NOT NULL,"
                "game TEXT NOT NULL,"
                "count INTEGER NOT NULL,"
                "first_ts INTEGER NOT NULL,"
                "last_ts INTEGER NOT NULL,"
                "PRIMARY KEY (expression, reading, game)) WITHOUT ROWID");
        db.SetUserVersion(3);
    }
    if (db.UserVersion() == 3) {
        // Version 4 keeps a day's time as counted beside its tidied time (TidyReadingTime):
        // NULL until it is tidied, then the counted time, still added to.
        db.Exec("ALTER TABLE daily ADD COLUMN raw_active_ms INTEGER");
        db.SetUserVersion(4);
    }
}

void ReaderDatabase::CountLookup(const QString& expression, const QString& reading,
                                 const QString& game, qint64 ts) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(
        db, "INSERT INTO lookups(expression, reading, game, count, first_ts, last_ts) "
            "VALUES(?, ?, ?, 1, ?, ?) ON CONFLICT(expression, reading, game) DO UPDATE SET "
            "count = count + 1, last_ts = MAX(last_ts, excluded.last_ts)");
    statement.Bind(1, expression).Bind(2, reading).Bind(3, game).Bind(4, ts).Bind(5, ts).Run();
}

LookupHistory ReaderDatabase::Lookups(const QString& expression, const QString& reading) {
    QMutexLocker lock(&mutex);
    LookupHistory history;
    {
        SqliteDb::Statement total(
            db, "SELECT COALESCE(SUM(count), 0) FROM lookups WHERE expression = ? AND reading = ?");
        if (total.Bind(1, expression).Bind(2, reading).Step()) {
            history.count = static_cast<int>(total.Int(0));
        }
    }
    if (history.count == 0) {
        return history;
    }
    SqliteDb::Statement first(
        db, "SELECT l.first_ts, l.game, COALESCE(g.title, '') FROM lookups l "
            "LEFT JOIN games g ON g.game = l.game WHERE l.expression = ? AND l.reading = ? "
            "ORDER BY l.first_ts LIMIT 1");
    if (first.Bind(1, expression).Bind(2, reading).Step()) {
        history.first_ts = first.Int(0);
        history.first_game = first.Text(1);
        history.first_title = first.Text(2);
    }
    return history;
}

void ReaderDatabase::SetGameTitle(const QString& game, const QString& title) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(db, "INSERT INTO games(game, title) VALUES(?, ?) "
                                      "ON CONFLICT(game) DO UPDATE SET title = excluded.title");
    statement.Bind(1, game).Bind(2, title).Run();
}

qint64 ReaderDatabase::InsertLine(const QString& game, const TextLine& line,
                                  const LineAccounting& accounting) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(
        db, "INSERT INTO lines(game, session_id, ts, channel, speaker, text, ruby, source_id, "
            "meta, chars, reread) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    statement.Bind(1, game);
    if (accounting.session_id) {
        statement.Bind(2, *accounting.session_id);
    } else {
        statement.BindNull(2);
    }
    QJsonObject meta;
    for (auto it = line.meta.constBegin(); it != line.meta.constEnd(); ++it) {
        meta.insert(it.key(), it.value());
    }
    statement.Bind(3, line.timestamp)
        .Bind(4, line.channel)
        .Bind(5, line.speaker)
        .Bind(6, line.text)
        .Bind(7, QString::fromUtf8(
                     QJsonDocument(TextLine::RubyToJson(line.ruby)).toJson(QJsonDocument::Compact)))
        .Bind(8, line.source_id)
        .Bind(9, QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)))
        .Bind(10, accounting.characters)
        .Bind(11, accounting.reread ? 1 : 0)
        .Run();
    return db.LastInsertId();
}

QList<BacklogEntry> ReaderDatabase::RecentLines(const QString& game, int limit,
                                                std::optional<qint64> before_id) {
    QMutexLocker lock(&mutex);
    if (!before_id) {
        return QueryLines("game = ?", {game}, limit);
    }
    return QueryLines("game = ? AND id < ?", {game, QString::number(*before_id)}, limit);
}

QList<BacklogEntry> ReaderDatabase::SearchLines(const QString& game, const QString& query,
                                                int limit) {
    QString escaped = query;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'%', QStringLiteral("\\%"));
    escaped.replace(u'_', QStringLiteral("\\_"));
    const QString pattern = u'%' + escaped + u'%';
    QMutexLocker lock(&mutex);
    return QueryLines("game = ? AND (text LIKE ? ESCAPE '\\' OR speaker LIKE ? ESCAPE '\\')",
                      {game, pattern, pattern}, limit);
}

qint64 ReaderDatabase::LineCount(const QString& game) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(db, "SELECT COUNT(*) FROM lines WHERE game = ?");
    return statement.Bind(1, game).Step() ? statement.Int(0) : 0;
}

QList<BacklogEntry> ReaderDatabase::QueryLines(const char* where, const QList<QString>& arguments,
                                               int limit) {
    const std::string sql =
        std::string("SELECT id, game, ts, channel, speaker, text, ruby, source_id, meta, chars, "
                    "reread FROM lines WHERE ") +
        where + " ORDER BY id DESC LIMIT " + std::to_string(limit);
    SqliteDb::Statement statement(db, sql.c_str());
    BindAll(statement, arguments);
    QList<BacklogEntry> out;
    while (statement.Step()) {
        BacklogEntry entry;
        entry.id = statement.Int(0);
        entry.game = statement.Text(1);
        entry.line.timestamp = statement.Int(2);
        entry.line.audio_position = -1;
        entry.line.channel = statement.Text(3);
        entry.line.speaker = statement.Text(4);
        entry.line.text = statement.Text(5);
        entry.line.ruby = TextLine::RubyFromJson(
            QJsonDocument::fromJson(statement.Text(6).toUtf8()).array(), entry.line.text);
        entry.line.source_id = statement.Text(7);
        const QJsonObject meta = QJsonDocument::fromJson(statement.Text(8).toUtf8()).object();
        for (auto it = meta.constBegin(); it != meta.constEnd(); ++it) {
            entry.line.meta.insert(it.key(), it.value().toVariant().toString());
        }
        entry.characters = static_cast<int>(statement.Int(9));
        entry.reread = statement.Int(10) != 0;
        out.append(entry);
    }
    return out;
}

std::optional<SessionRecord> ReaderDatabase::OpenSession(const QString& game) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(db, "SELECT id, game, started, last_activity, ended FROM "
                                      "sessions WHERE game = ? AND ended IS NULL "
                                      "ORDER BY id DESC LIMIT 1");
    if (!statement.Bind(1, game).Step()) {
        return std::nullopt;
    }
    return SessionFrom(statement, ReadingCounters{});
}

qint64 ReaderDatabase::CreateSession(const QString& game, qint64 started) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(
        db, "INSERT INTO sessions(game, started, last_activity) VALUES(?, ?, ?)");
    statement.Bind(1, game).Bind(2, started).Bind(3, started).Run();
    return db.LastInsertId();
}

ReadingCounters ReaderDatabase::SessionCounters(qint64 id) {
    QMutexLocker lock(&mutex);
    return SumDaily("session_id = ?", {QString::number(id)});
}

void ReaderDatabase::CloseSession(qint64 id, qint64 ended) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(db,
                                  "UPDATE sessions SET ended = ? WHERE id = ? AND ended IS NULL");
    statement.Bind(1, ended).Bind(2, id).Run();
}

void ReaderDatabase::AddActivity(qint64 session_id, const QString& game, const QString& day,
                                 const ReadingCounters& delta, qint64 last_activity) {
    QMutexLocker lock(&mutex);
    SqliteDb::Transaction transaction(db);
    {
        SqliteDb::Statement statement(
            db, "INSERT INTO daily(session_id, day, game, active_ms, chars, unique_chars, "
                "reread_chars, lines, choice_chars) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(session_id, day) DO UPDATE SET "
                "active_ms = active_ms + excluded.active_ms, "
                "raw_active_ms = CASE WHEN raw_active_ms IS NULL THEN NULL "
                "ELSE raw_active_ms + excluded.active_ms END, "
                "chars = chars + excluded.chars, "
                "unique_chars = unique_chars + excluded.unique_chars, "
                "reread_chars = reread_chars + excluded.reread_chars, "
                "lines = lines + excluded.lines, "
                "choice_chars = choice_chars + excluded.choice_chars");
        statement.Bind(1, session_id)
            .Bind(2, day)
            .Bind(3, game)
            .Bind(4, delta.active_ms)
            .Bind(5, delta.characters)
            .Bind(6, delta.unique_characters)
            .Bind(7, delta.reread_characters)
            .Bind(8, delta.lines)
            .Bind(9, delta.choice_characters)
            .Run();
    }
    {
        SqliteDb::Statement statement(
            db, "UPDATE sessions SET last_activity = MAX(last_activity, ?) WHERE id = ?");
        statement.Bind(1, last_activity).Bind(2, session_id).Run();
    }
    transaction.Commit();
}

void ReaderDatabase::RecountCharacters() {
    QMutexLocker lock(&mutex);
    struct Change {
        qint64 id;
        qint64 session;
        QString day;
        bool choice;
        bool reread;
        int was;
        int now;
    };
    QList<Change> changes;
    {
        SqliteDb::Statement statement(db, "SELECT id, session_id, ts, channel, text, chars, reread "
                                          "FROM lines WHERE session_id IS NOT NULL");
        while (statement.Step()) {
            const int now = ReadingRules::CountCharacters(statement.Text(4));
            const int was = static_cast<int>(statement.Int(5));
            if (now != was) {
                changes.append(Change{statement.Int(0), statement.Int(1),
                                      ReadingRules::ReadingDay(statement.Int(2)),
                                      statement.Text(3) == QStringLiteral("choice"),
                                      statement.Int(6) != 0, was, now});
            }
        }
    }
    if (changes.isEmpty()) {
        return;
    }
    SqliteDb::Transaction transaction(db);
    for (const Change& change : changes) {
        const qint64 less = change.was - change.now;
        SqliteDb::Statement(db, "UPDATE lines SET chars = ? WHERE id = ?")
            .Bind(1, static_cast<qint64>(change.now))
            .Bind(2, change.id)
            .Run();
        if (change.choice) {
            SqliteDb::Statement(db, "UPDATE daily SET choice_chars = MAX(0, choice_chars - ?) "
                                    "WHERE session_id = ? AND day = ?")
                .Bind(1, less)
                .Bind(2, change.session)
                .Bind(3, change.day)
                .Run();
        } else {
            const std::string sql =
                std::string("UPDATE daily SET chars = MAX(0, chars - ?), ") +
                (change.reread ? "reread_chars = MAX(0, reread_chars - ?)"
                               : "unique_chars = MAX(0, unique_chars - ?)") +
                ", lines = MAX(0, lines - ?) WHERE session_id = ? AND day = ?";
            SqliteDb::Statement(db, sql.c_str())
                .Bind(1, less)
                .Bind(2, less)
                .Bind(3, static_cast<qint64>(change.now == 0 ? 1 : 0))
                .Bind(4, change.session)
                .Bind(5, change.day)
                .Run();
        }
    }
    transaction.Commit();
}

qint64 ReaderDatabase::StripOverlaps(bool apply) {
    QMutexLocker lock(&mutex);
    struct Change {
        qint64 id;
        qint64 session;
        QString day;
        bool reread;
        int was;
        int now;
    };
    QList<Change> changes;
    qint64 taken = 0;
    {
        SqliteDb::Statement statement(db, "SELECT id, session_id, ts, channel, text, chars, reread "
                                          "FROM lines WHERE session_id IS NOT NULL ORDER BY session_id, id");
        qint64 session = -1;
        QStringList before;
        while (statement.Step()) {
            if (statement.Int(1) != session) {
                session = statement.Int(1);
                before.clear();
            }
            const QString text = statement.Text(4);
            if (statement.Text(3) != QStringLiteral("choice")) {
                const int was = static_cast<int>(statement.Int(5));
                const int now = std::min(
                    was, ReadingRules::CountCharacters(ReadingRules::WithoutOverlap(text, before)));
                if (now < was) {
                    taken += was - now;
                    changes.append(Change{statement.Int(0), session, ReadingRules::ReadingDay(statement.Int(2)),
                                          statement.Int(6) != 0, was, now});
                }
            }
            // The same text again says nothing new about what came before.
            if (!before.contains(text)) {
                before.append(text);
                if (before.size() > ReadingRules::FreshWindow) {
                    before.removeFirst();
                }
            }
        }
    }
    if (!apply || changes.isEmpty()) {
        return taken;
    }
    SqliteDb::Transaction transaction(db);
    for (const Change& change : changes) {
        const qint64 less = change.was - change.now;
        SqliteDb::Statement(db, "UPDATE lines SET chars = ? WHERE id = ?")
            .Bind(1, static_cast<qint64>(change.now))
            .Bind(2, change.id)
            .Run();
        const std::string sql =
            std::string("UPDATE daily SET chars = MAX(0, chars - ?), ") +
            (change.reread ? "reread_chars = MAX(0, reread_chars - ?)"
                           : "unique_chars = MAX(0, unique_chars - ?)") +
            ", lines = MAX(0, lines - ?) WHERE session_id = ? AND day = ?";
        SqliteDb::Statement(db, sql.c_str())
            .Bind(1, less)
            .Bind(2, less)
            .Bind(3, static_cast<qint64>(change.now == 0 ? 1 : 0))
            .Bind(4, change.session)
            .Bind(5, change.day)
            .Run();
    }
    transaction.Commit();
    return taken;
}

TidyReport ReaderDatabase::TidyReadingTime(qint64 idle_timeout_ms, std::optional<qint64> only_session,
                                           std::optional<qint64> skip_session,
                                           const std::function<QString(qint64)>& day_of_) {
    const auto day_of = day_of_ ? day_of_ : std::function<QString(qint64)>(ReadingRules::ReadingDay);
    const qint64 idle = std::clamp<qint64>(idle_timeout_ms, 1000, ReadingRules::MaxIdleTimeoutMs);
    QMutexLocker lock(&mutex);
    TidyReport report;
    // The games to look at, and their sessions.
    QHash<QString, QList<qint64>> sessions_of;
    QStringList games;
    {
        SqliteDb::Statement statement(
            db, only_session ? "SELECT id, game FROM sessions WHERE id = ?" : "SELECT id, game FROM sessions ORDER BY id");
        if (only_session) {
            statement.Bind(1, *only_session);
        }
        while (statement.Step()) {
            if (skip_session && statement.Int(0) == *skip_session) {
                continue;
            }
            const QString game = statement.Text(1);
            if (!sessions_of.contains(game)) {
                games.append(game);
            }
            sessions_of[game].append(statement.Int(0));
        }
    }
    SqliteDb::Transaction transaction(db);
    for (const QString& game : games) {
        // Every counted line of the game, by session, and the moments words were looked up.
        QHash<qint64, QList<ReadingTime::Event>> lines_of;
        QList<ReadingTime::Event> all;
        {
            SqliteDb::Statement statement(
                db, "SELECT session_id, ts, chars FROM lines WHERE game = ? AND session_id IS NOT NULL "
                    "ORDER BY ts, id");
            statement.Bind(1, game);
            while (statement.Step()) {
                const ReadingTime::Event event{statement.Int(1), static_cast<int>(statement.Int(2)), true};
                lines_of[statement.Int(0)].append(event);
                all.append(event);
            }
        }
        std::vector<qint64> lookups;
        {
            SqliteDb::Statement statement(db, "SELECT first_ts, last_ts FROM lookups WHERE game = ?");
            statement.Bind(1, game);
            while (statement.Step()) {
                lookups.push_back(statement.Int(0));
                if (statement.Int(1) != statement.Int(0)) {
                    lookups.push_back(statement.Int(1));
                }
            }
        }
        std::sort(lookups.begin(), lookups.end());
        const ReadingTime::Pace game_pace = ReadingTime::PaceOf(all, idle);
        for (const qint64 session : sessions_of.value(game)) {
            QList<ReadingTime::Event> events = lines_of.value(session);
            if (events.isEmpty()) {
                continue;
            }
            ++report.sessions;
            const qint64 first = events.first().ts;
            const qint64 last = events.last().ts + idle;
            for (auto it = std::lower_bound(lookups.begin(), lookups.end(), first + 1);
                 it != lookups.end() && *it <= last; ++it) {
                events.append(ReadingTime::Event{*it, 0, false});
            }
            std::stable_sort(events.begin(), events.end(),
                             [](const auto& a, const auto& b) { return a.ts < b.ts; });
            const ReadingTime::Pace pace = ReadingTime::PaceOf(events, idle, game_pace);
            QHash<QString, qint64> credited;
            for (const auto& [ts, ms] : ReadingTime::Credit(events, pace, idle)) {
                credited[day_of(ts)] += ms;
            }
            struct Row {
                QString day;
                qint64 active;
                qint64 raw;
                qint64 chars;
            };
            QList<Row> rows;
            {
                SqliteDb::Statement statement(
                    db, "SELECT day, active_ms, COALESCE(raw_active_ms, active_ms), chars + choice_chars "
                        "FROM daily WHERE session_id = ?");
                statement.Bind(1, session);
                while (statement.Step()) {
                    rows.append(Row{statement.Text(0), statement.Int(1), statement.Int(2), statement.Int(3)});
                }
            }
            qint64 before = 0;
            qint64 after = 0;
            for (const Row& row : rows) {
                const qint64 now = std::min(row.raw, credited.value(row.day));
                before += row.active;
                after += now;
                report.characters += row.chars;
                SqliteDb::Statement(db, "UPDATE daily SET active_ms = ?, raw_active_ms = ? WHERE session_id = ? AND day = ?")
                    .Bind(1, now)
                    .Bind(2, row.raw)
                    .Bind(3, session)
                    .Bind(4, row.day)
                    .Run();
            }
            report.before_ms += before;
            report.after_ms += after;
            if (after < before) {
                ++report.changed;
            }
        }
    }
    transaction.Commit();
    return report;
}

int ReaderDatabase::UntidyReadingTime() {
    QMutexLocker lock(&mutex);
    int sessions = 0;
    {
        SqliteDb::Statement statement(db, "SELECT COUNT(DISTINCT session_id) FROM daily WHERE raw_active_ms IS NOT NULL");
        if (statement.Step()) {
            sessions = static_cast<int>(statement.Int(0));
        }
    }
    db.Exec("UPDATE daily SET active_ms = raw_active_ms, raw_active_ms = NULL WHERE raw_active_ms IS NOT NULL");
    return sessions;
}

QStringList ReaderDatabase::SessionSources(qint64 id) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(
        db, "SELECT DISTINCT source_id FROM lines WHERE session_id = ? AND source_id != ''");
    statement.Bind(1, id);
    QStringList out;
    while (statement.Step()) {
        out.append(statement.Text(0));
    }
    return out;
}

bool ReaderDatabase::MarkSeen(const QString& game, const QString& source_id, qint64 timestamp) {
    QMutexLocker lock(&mutex);
    bool seen_before;
    {
        SqliteDb::Statement statement(db, "SELECT 1 FROM seen WHERE game = ? AND source_id = ?");
        seen_before = statement.Bind(1, game).Bind(2, source_id).Step();
    }
    SqliteDb::Statement statement(
        db, "INSERT INTO seen(game, source_id, first_ts, count) VALUES(?, ?, ?, 1) "
            "ON CONFLICT(game, source_id) DO UPDATE SET count = count + 1");
    statement.Bind(1, game).Bind(2, source_id).Bind(3, timestamp).Run();
    return seen_before;
}

std::optional<UncountedLine> ReaderDatabase::UncountLine(const QString& game,
                                                         const QString& source_id,
                                                         const QString& text) {
    QMutexLocker lock(&mutex);
    SqliteDb::Transaction transaction(db);
    UncountedLine line;
    qint64 id{};
    TextLine probe;
    {
        SqliteDb::Statement statement(
            db, "SELECT id, session_id, ts, channel, chars, reread, source_id, text FROM lines "
                "WHERE game = ? AND session_id IS NOT NULL "
                "AND ((source_id = ? AND source_id <> '') OR text = ?) ORDER BY id DESC LIMIT 1");
        statement.Bind(1, game).Bind(2, source_id).Bind(3, text);
        if (!statement.Step()) {
            return std::nullopt;
        }
        id = statement.Int(0);
        line.session_id = statement.Int(1);
        line.timestamp = statement.Int(2);
        probe.channel = statement.Text(3);
        line.characters = static_cast<int>(statement.Int(4));
        line.reread = statement.Int(5) != 0;
        line.source_id = statement.Text(6);
        line.text = statement.Text(7);
    }
    line.choice = probe.IsChoice();
    {
        SqliteDb::Statement statement(db, "UPDATE lines SET session_id = NULL WHERE id = ?");
        statement.Bind(1, id).Run();
    }
    {
        SqliteDb::Statement statement(
            db, line.reread ? "UPDATE seen SET count = count - 1 WHERE game = ? AND source_id = ?"
                            : "DELETE FROM seen WHERE game = ? AND source_id = ?");
        statement.Bind(1, game).Bind(2, line.source_id).Run();
    }
    transaction.Commit();
    return line;
}

std::optional<BacklogEntry> ReaderDatabase::UncountedLineOf(const QString& game,
                                                            const QString& source_id,
                                                            const QString& text) {
    QMutexLocker lock(&mutex);
    const QList<BacklogEntry> found = QueryLines(
        "game = ? AND session_id IS NULL AND ((source_id = ? AND source_id <> '') OR text = ?)",
        {game, source_id, text}, 1);
    return found.isEmpty() ? std::nullopt : std::make_optional(found.first());
}

void ReaderDatabase::SetLineCounted(qint64 id, const LineAccounting& accounting) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(
        db, "UPDATE lines SET session_id = ?, chars = ?, reread = ? WHERE id = ?");
    if (accounting.session_id) {
        statement.Bind(1, *accounting.session_id);
    } else {
        statement.BindNull(1);
    }
    statement.Bind(2, accounting.characters).Bind(3, accounting.reread ? 1 : 0).Bind(4, id).Run();
}

bool ReaderDatabase::HasSeen(const QString& game, const QString& source_id) {
    QMutexLocker lock(&mutex);
    SqliteDb::Statement statement(db, "SELECT 1 FROM seen WHERE game = ? AND source_id = ?");
    return statement.Bind(1, game).Bind(2, source_id).Step();
}

ReadingCounters ReaderDatabase::Totals(const QString& game) {
    QMutexLocker lock(&mutex);
    return game.isEmpty() ? SumDaily(nullptr, {}) : SumDaily("game = ?", {game});
}

ReadingCounters ReaderDatabase::DayTotals(const QString& day, const QString& game) {
    QMutexLocker lock(&mutex);
    return game.isEmpty() ? SumDaily("day = ?", {day})
                          : SumDaily("day = ? AND game = ?", {day, game});
}

QList<std::pair<QString, ReadingCounters>> ReaderDatabase::DailyHistory(const QString& game,
                                                                        int limit) {
    QMutexLocker lock(&mutex);
    const std::string sql = std::string("SELECT day, ") + SumColumns + " FROM daily " +
                            (game.isEmpty() ? "" : "WHERE game = ? ") +
                            "GROUP BY day ORDER BY day DESC LIMIT " + std::to_string(limit);
    SqliteDb::Statement statement(db, sql.c_str());
    if (!game.isEmpty()) {
        statement.Bind(1, game);
    }
    QList<std::pair<QString, ReadingCounters>> out;
    while (statement.Step()) {
        out.append({statement.Text(0), CountersFrom(statement, 1)});
    }
    return out;
}

QList<GameTotals> ReaderDatabase::AllGameTotals() {
    QMutexLocker lock(&mutex);
    return AllGameTotalsLocked();
}

QList<GameTotals> ReaderDatabase::AllGameTotalsLocked() {
    const std::string sql =
        std::string("SELECT d.game, COALESCE(g.title, d.game), ") + SumColumns +
        " FROM daily d LEFT JOIN games g ON g.game = d.game "
        "GROUP BY d.game ORDER BY SUM(d.chars) DESC";
    SqliteDb::Statement statement(db, sql.c_str());
    QList<GameTotals> out;
    while (statement.Step()) {
        out.append(GameTotals{statement.Text(0), statement.Text(1), CountersFrom(statement, 2)});
    }
    return out;
}

QList<SessionRecord> ReaderDatabase::Sessions(const QString& game) {
    QMutexLocker lock(&mutex);
    return SessionsLocked(game);
}

QList<SessionRecord> ReaderDatabase::SessionsLocked(const QString& game) {
    const std::string sql =
        std::string("SELECT s.id, s.game, s.started, s.last_activity, s.ended, "
                    "COALESCE(SUM(d.active_ms), 0), COALESCE(SUM(d.chars), 0), "
                    "COALESCE(SUM(d.unique_chars), 0), COALESCE(SUM(d.reread_chars), 0), "
                    "COALESCE(SUM(d.lines), 0), COALESCE(SUM(d.choice_chars), 0) "
                    "FROM sessions s LEFT JOIN daily d ON d.session_id = s.id ") +
        (game.isEmpty() ? "" : "WHERE s.game = ? ") + "GROUP BY s.id ORDER BY s.id DESC";
    SqliteDb::Statement statement(db, sql.c_str());
    if (!game.isEmpty()) {
        statement.Bind(1, game);
    }
    QList<SessionRecord> out;
    while (statement.Step()) {
        out.append(SessionFrom(statement, CountersFrom(statement, 5)));
    }
    return out;
}

void ReaderDatabase::DeleteSession(qint64 id) {
    QMutexLocker lock(&mutex);
    SqliteDb::Transaction transaction(db);
    for (const char* sql :
         {"DELETE FROM daily WHERE session_id = ?", "DELETE FROM sessions WHERE id = ?"}) {
        SqliteDb::Statement statement(db, sql);
        statement.Bind(1, id).Run();
    }
    transaction.Commit();
}

void ReaderDatabase::ClearActivity(const QString& game) {
    QMutexLocker lock(&mutex);
    SqliteDb::Transaction transaction(db);
    if (game.isEmpty()) {
        db.Exec("DELETE FROM daily");
        db.Exec("DELETE FROM sessions");
    } else {
        for (const char* sql :
             {"DELETE FROM daily WHERE game = ?", "DELETE FROM sessions WHERE game = ?"}) {
            SqliteDb::Statement statement(db, sql);
            statement.Bind(1, game).Run();
        }
    }
    transaction.Commit();
}

ReadingCounters ReaderDatabase::SumDaily(const char* where, const QList<QString>& arguments) {
    const std::string sql = std::string("SELECT ") + SumColumns + " FROM daily" +
                            (where ? std::string(" WHERE ") + where : std::string());
    SqliteDb::Statement statement(db, sql.c_str());
    BindAll(statement, arguments);
    return statement.Step() ? CountersFrom(statement, 0) : ReadingCounters{};
}

QJsonObject ReaderDatabase::StatsRecord() {
    QMutexLocker lock(&mutex);
    QJsonObject games;
    {
        SqliteDb::Statement statement(
            db, "SELECT game, title FROM games WHERE game IN (SELECT DISTINCT game FROM daily)");
        while (statement.Step()) {
            games.insert(statement.Text(0), statement.Text(1));
        }
    }
    QJsonArray days;
    {
        SqliteDb::Statement statement(
            db, "SELECT day, game, SUM(active_ms), SUM(chars), SUM(unique_chars), "
                "SUM(reread_chars), SUM(lines), SUM(choice_chars), COUNT(DISTINCT session_id) "
                "FROM daily GROUP BY day, game ORDER BY day, game");
        while (statement.Step()) {
            QJsonObject day;
            day.insert(QStringLiteral("day"), statement.Text(0));
            day.insert(QStringLiteral("game"), statement.Text(1));
            day.insert(QStringLiteral("active_ms"), statement.Int(2));
            day.insert(QStringLiteral("chars"), statement.Int(3));
            day.insert(QStringLiteral("unique_chars"), statement.Int(4));
            day.insert(QStringLiteral("reread_chars"), statement.Int(5));
            day.insert(QStringLiteral("lines"), statement.Int(6));
            day.insert(QStringLiteral("choice_chars"), statement.Int(7));
            day.insert(QStringLiteral("sessions"), statement.Int(8));
            days.append(day);
        }
    }
    QJsonObject record;
    record.insert(QStringLiteral("games"), games);
    record.insert(QStringLiteral("days"), days);
    return record;
}

QJsonArray ReaderDatabase::LookupsRecord(int limit) {
    QMutexLocker lock(&mutex);
    QJsonArray out;
    SqliteDb::Statement statement(
        db, "SELECT l.expression, l.reading, l.game, COALESCE(g.title, ''), l.count, l.first_ts, "
            "l.last_ts FROM lookups l LEFT JOIN games g ON g.game = l.game "
            "ORDER BY l.count DESC, l.last_ts DESC LIMIT ?");
    statement.Bind(1, static_cast<qint64>(limit));
    while (statement.Step()) {
        out.append(QJsonObject{{QStringLiteral("expression"), statement.Text(0)},
                               {QStringLiteral("reading"), statement.Text(1)},
                               {QStringLiteral("game"), statement.Text(2)},
                               {QStringLiteral("title"), statement.Text(3)},
                               {QStringLiteral("count"), statement.Int(4)},
                               {QStringLiteral("first"), statement.Int(5)},
                               {QStringLiteral("last"), statement.Int(6)}});
    }
    return out;
}

QJsonObject ReaderDatabase::ExportActivityJson() {
    QMutexLocker lock(&mutex);
    QJsonObject root;
    // The same format as the 3DS sibling writes, so that one tool reads both.
    root.insert(QStringLiteral("format"), QStringLiteral("azahar-reader.activity"));
    root.insert(QStringLiteral("version"), 1);
    QHash<QString, QString> titles;
    {
        SqliteDb::Statement statement(db, "SELECT game, title FROM games");
        while (statement.Step()) {
            titles.insert(statement.Text(0), statement.Text(1));
        }
    }
    QHash<qint64, QJsonArray> ledger;
    {
        SqliteDb::Statement statement(
            db, "SELECT session_id, day, active_ms, chars, unique_chars, reread_chars, lines, "
                "choice_chars FROM daily ORDER BY session_id, day");
        while (statement.Step()) {
            QJsonObject day = CountersJson(CountersFrom(statement, 2));
            day.insert(QStringLiteral("day"), statement.Text(1));
            ledger[statement.Int(0)].append(day);
        }
    }
    QList<SessionRecord> oldest_first = SessionsLocked({});
    std::reverse(oldest_first.begin(), oldest_first.end());
    QJsonArray sessions;
    for (const SessionRecord& s : oldest_first) {
        QJsonObject session = CountersJson(s.counters);
        session.insert(QStringLiteral("id"), s.id);
        session.insert(QStringLiteral("game"), s.game);
        session.insert(QStringLiteral("title"), titles.value(s.game, s.game));
        session.insert(QStringLiteral("started"), s.started);
        session.insert(QStringLiteral("lastActivity"), s.last_activity);
        session.insert(QStringLiteral("ended"), s.ended ? QJsonValue(*s.ended) : QJsonValue());
        session.insert(QStringLiteral("days"), ledger.value(s.id));
        sessions.append(session);
    }
    root.insert(QStringLiteral("sessions"), sessions);
    QJsonArray games;
    for (const GameTotals& g : AllGameTotalsLocked()) {
        QJsonObject game = CountersJson(g.counters);
        game.insert(QStringLiteral("game"), g.game);
        game.insert(QStringLiteral("title"), g.title);
        games.append(game);
    }
    root.insert(QStringLiteral("games"), games);
    root.insert(QStringLiteral("total"), CountersJson(SumDaily(nullptr, {})));
    return root;
}

QString ReaderDatabase::ExportSessionsCsv() {
    QMutexLocker lock(&mutex);
    QString out = QStringLiteral(
        "session,game,title,started,last_activity,ended,active_seconds,characters,"
        "unique_characters,reread_characters,lines,choice_characters\n");
    QHash<QString, QString> titles;
    {
        SqliteDb::Statement statement(db, "SELECT game, title FROM games");
        while (statement.Step()) {
            titles.insert(statement.Text(0), statement.Text(1));
        }
    }
    QList<SessionRecord> oldest_first = SessionsLocked({});
    std::reverse(oldest_first.begin(), oldest_first.end());
    for (const SessionRecord& s : oldest_first) {
        QString title = titles.value(s.game, s.game);
        title.replace(u'"', QStringLiteral("\"\""));
        out += QString::number(s.id) + u',' + s.game + QStringLiteral(",\"") + title +
               QStringLiteral("\",") + QString::number(s.started) + u',' +
               QString::number(s.last_activity) + u',' +
               (s.ended ? QString::number(*s.ended) : QString()) + u',' +
               QString::number(s.counters.active_ms / 1000) + u',' +
               QString::number(s.counters.characters) + u',' +
               QString::number(s.counters.unique_characters) + u',' +
               QString::number(s.counters.reread_characters) + u',' +
               QString::number(s.counters.lines) + u',' +
               QString::number(s.counters.choice_characters) + u'\n';
    }
    return out;
}

} // namespace ReaderApp
