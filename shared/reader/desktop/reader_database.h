// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <utility>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>
#include "reader/desktop/sqlite_db.h"
#include "reader/desktop/stats.h"
#include "reader/desktop/text_line.h"

namespace ReaderApp {

/// A line kept in the backlog.
struct BacklogEntry {
    qint64 id{};
    QString game;
    TextLine line;
    int characters{};
    bool reread{};
};

/// Totals of one game, for the statistics screen.
struct GameTotals {
    QString game;
    QString title;
    ReadingCounters counters;
};

/// How often a word was looked up: how many times, and when and in which game first.
struct LookupHistory {
    int count{};
    qint64 first_ts{};
    QString first_game;
    /// The first game's name, as the games table has it (empty when it has none).
    QString first_title;
};

/**
 * Reading history kept on this machine: the backlog of presented lines, which sources were
 * seen, and reading activity per session and reading day. The file has the Android app's
 * tables.
 *
 * Activity is stored as one row per session and day, so a session, a day, a game or
 * everything can be totalled, and deleting a session subtracts exactly what it contributed.
 * Clearing activity never touches the backlog or the seen sources: text met again is still
 * rereading.
 */
class ReaderDatabase : public ActivityStore {
public:
    static constexpr const char* FileName = "reader.db";

    /// Opens (and creates) the database file, or ":memory:".
    explicit ReaderDatabase(const QString& path);

    // ---- Games ----
    void SetGameTitle(const QString& game, const QString& title);

    // ---- Backlog ----
    qint64 InsertLine(const QString& game, const TextLine& line, const LineAccounting& accounting);
    /// The newest lines of `game`, newest first, optionally only those older than `before_id`.
    [[nodiscard]] QList<BacklogEntry> RecentLines(const QString& game, int limit,
                                                  std::optional<qint64> before_id = {});
    /// Lines of `game` whose text or speaker contains `query`, newest first.
    [[nodiscard]] QList<BacklogEntry> SearchLines(const QString& game, const QString& query,
                                                  int limit);
    [[nodiscard]] qint64 LineCount(const QString& game);
    /// The newest line of `game` that counted nothing, by its source, else its text.
    [[nodiscard]] std::optional<BacklogEntry> UncountedLineOf(const QString& game,
                                                              const QString& source_id,
                                                              const QString& text);
    /// A line stored as not counted has been counted after all.
    void SetLineCounted(qint64 id, const LineAccounting& accounting);

    // ---- ActivityStore ----
    std::optional<SessionRecord> OpenSession(const QString& game) override;
    qint64 CreateSession(const QString& game, qint64 started) override;
    ReadingCounters SessionCounters(qint64 id) override;
    void CloseSession(qint64 id, qint64 ended) override;
    void AddActivity(qint64 session_id, const QString& game, const QString& day,
                     const ReadingCounters& delta, qint64 last_activity) override;
    bool MarkSeen(const QString& game, const QString& source_id, qint64 timestamp) override;
    std::optional<UncountedLine> UncountLine(const QString& game, const QString& source_id,
                                             const QString& text) override;
    QStringList SessionSources(qint64 id) override;
    /**
     * Counts every kept line again by today's rule and takes the difference out of the day it
     * was counted in, so that what was read before the rule changed is measured like what is
     * read after it. Run once when a database of the older rule is opened; public for tests.
     */
    void RecountCharacters();
    /// Takes out of the counts what was counted twice: text of a line that already stood in
    /// the lines just before it in its session (ReadingRules::WithoutOverlap), as a long
    /// text scrolled a screenful at a time was counted before that rule. With `apply` false
    /// nothing is changed. Returns how many characters are (or would be) taken out. Doing it
    /// twice takes nothing out the second time.
    qint64 StripOverlaps(bool apply);

    /**
     * Works each session's reading time out again from when its lines came and when words were
     * looked up (ReadingTime in stats.h), with `idle_timeout_ms` as the longest a passage is
     * read, and lowers each of its days to that where it was counted higher. Time is never
     * added. The time as counted is kept (`raw_active_ms`), so tidying again, with another
     * timeout, starts from it, and UntidyReadingTime puts it back. One session, or every one
     * but `skip_session` (one still being added to). `day_of` is the tracker's.
     */
    TidyReport TidyReadingTime(qint64 idle_timeout_ms, std::optional<qint64> only_session = std::nullopt,
                               std::optional<qint64> skip_session = std::nullopt,
                               const std::function<QString(qint64)>& day_of = {});
    /// Every tidied day's time back as it was counted; how many sessions had any.
    int UntidyReadingTime();

    [[nodiscard]] bool HasSeen(const QString& game, const QString& source_id);

    // ---- Words looked up ----
    /// One more lookup of a word (by its expression and reading) in `game`, at `ts`.
    void CountLookup(const QString& expression, const QString& reading, const QString& game,
                     qint64 ts);
    /// How often the word was looked up before, in every game together.
    [[nodiscard]] LookupHistory Lookups(const QString& expression, const QString& reading);

    // ---- Activity queries ----
    /// Totals for `game`, or for every game when it is empty.
    [[nodiscard]] ReadingCounters Totals(const QString& game = {});
    /// Totals of one reading day, for `game` or for every game when it is empty.
    [[nodiscard]] ReadingCounters DayTotals(const QString& day, const QString& game = {});
    /// Totals per reading day, newest first, at most `limit` days.
    [[nodiscard]] QList<std::pair<QString, ReadingCounters>> DailyHistory(const QString& game = {},
                                                                         int limit = 30);
    /// Every game that has activity, with its totals, most read first.
    [[nodiscard]] QList<GameTotals> AllGameTotals();
    /// Sessions of `game` (or all games), newest first, each with its totals.
    [[nodiscard]] QList<SessionRecord> Sessions(const QString& game = {});
    /// Removes one session and everything it contributed to the totals.
    void DeleteSession(qint64 id);
    /**
     * Removes the sessions and daily totals of `game`, or of every game when it is empty. The
     * backlog and the seen sources stay, so passed text still counts as rereading afterwards.
     */
    void ClearActivity(const QString& game = {});

    // ---- Export ----
    /// Activity of every game: sessions with their per-day ledger, plus totals.
    [[nodiscard]] QJsonObject ExportActivityJson();
    /// What has been read, by reading day and game, with the games' names: what this install
    /// sends to the save sync service, whole each time ("days" and "games" of its record).
    [[nodiscard]] QJsonObject StatsRecord();
    /// The most words a record carries (the keeper takes records up to 4 MB; Furochibi's own
    /// record carries as many).
    static constexpr int RecordLookups = 20000;
    /**
     * The words looked up here, for the record this install sends (its "lookups"): each
     * {expression, reading, game, title, count, first, last}, the most looked up first, at most
     * `limit`. Furochibi adds every device's up for its words looked up most
     * (furochibi/server/console_ocr/library.py, merge_lookups).
     */
    [[nodiscard]] QJsonArray LookupsRecord(int limit = RecordLookups);
    /// One CSV row per session, oldest first.
    [[nodiscard]] QString ExportSessionsCsv();

private:
    QList<BacklogEntry> QueryLines(const char* where, const QList<QString>& arguments, int limit);
    ReadingCounters SumDaily(const char* where, const QList<QString>& arguments);
    QList<SessionRecord> SessionsLocked(const QString& game);
    QList<GameTotals> AllGameTotalsLocked();

    SqliteDb db;
    QMutex mutex;
};

} // namespace ReaderApp
