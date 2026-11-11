// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>
#include <utility>
#include <vector>
#include "reader/desktop/stats.h"

namespace ReaderApp {

std::optional<double> ReadingCounters::CharactersPerHour() const {
    return ReadingRules::CharactersPerHour(characters, active_ms);
}

int ReadingRules::CountCharacters(const QString& text) {
    int count = 0;
    for (int i = 0; i < text.size(); i += CharCountAt(text, i)) {
        if (IsJapanese(CodePointAt(text, i))) {
            ++count;
        }
    }
    return count;
}

namespace {
/// The longest stretch that stands in both: where it begins in `a` and how long it is.
std::pair<qsizetype, qsizetype> LongestShared(const QString& a, const QString& b) {
    std::vector<qsizetype> last(static_cast<std::size_t>(b.size()) + 1, 0), now(last.size(), 0);
    qsizetype best = 0, best_end = 0;
    for (qsizetype i = 1; i <= a.size(); ++i) {
        for (qsizetype j = 1; j <= b.size(); ++j) {
            if (a[i - 1] == b[j - 1]) {
                now[j] = last[j - 1] + 1;
                if (now[j] > best) {
                    best = now[j];
                    best_end = i;
                }
            } else {
                now[j] = 0;
            }
        }
        std::swap(last, now);
    }
    return {best_end - best, best};
}

/// Whether the two may share a stretch of FreshOverlap characters: a quick look before the
/// slow one. Every such stretch holds one of the pieces looked for.
bool MayShare(const QString& a, const QString& b) {
    constexpr qsizetype Piece = 8, Step = ReadingRules::FreshOverlap - Piece + 1;
    for (qsizetype i = 0; i + Piece <= a.size(); i += Step) {
        if (b.contains(QStringView{a}.mid(i, Piece))) {
            return true;
        }
    }
    return false;
}
} // namespace

QString ReadingRules::WithoutOverlap(const QString& text, const QStringList& before) {
    QString fresh = text;
    if (text.size() < FreshOverlap || before.isEmpty()) {
        return fresh;
    }
    const qsizetype first = std::max<qsizetype>(0, before.size() - FreshWindow);
    // A stretch at a time, the longest first, until nothing long enough is left.
    for (int round = 0; round < 8; ++round) {
        qsizetype at = 0, length = 0;
        for (qsizetype i = first; i < before.size(); ++i) {
            if (!MayShare(fresh, before[i])) {
                continue;
            }
            const auto [where, shared] = LongestShared(fresh, before[i]);
            if (shared > length) {
                at = where;
                length = shared;
            }
        }
        if (length < FreshOverlap) {
            break;
        }
        // A line break in its place keeps what stood on either side apart.
        fresh.replace(at, length, QStringLiteral("\n"));
    }
    return fresh;
}

bool ReadingRules::IsJapanese(char32_t cp) {
    const auto in = [cp](char32_t first, char32_t last) { return cp >= first && cp <= last; };
    return in(0x3041, 0x3096) || in(0x309D, 0x309F) ||   // hiragana
           in(0x30A1, 0x30FA) || in(0x30FC, 0x30FF) ||   // katakana, ー
           in(0x31F0, 0x31FF) ||                         // small katakana for Ainu
           in(0xFF66, 0xFF9D) ||                         // half-width katakana
           in(0x3005, 0x3007) ||                         // 々 〆 〇
           in(0x3400, 0x4DBF) || in(0x4E00, 0x9FFF) || in(0xF900, 0xFAFF) || // kanji
           in(0x20000, 0x323AF);                         // kanji beyond the basic plane
}

QString ReadingRules::ReadingDay(qint64 epoch_ms) {
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(epoch_ms).toLocalTime();
    // Wall-clock arithmetic, as the Android app does it: the date of the local time moved
    // back by four hours and a minute.
    const QDateTime shifted = QDateTime(local.date(), local.time(), QTimeZone::UTC)
                                  .addSecs(-static_cast<qint64>(DayStartMinutes) * 60);
    return shifted.date().toString(Qt::ISODate);
}

std::optional<double> ReadingRules::CharactersPerHour(qint64 characters, qint64 active_ms) {
    if (active_ms < 1000) {
        return std::nullopt;
    }
    return static_cast<double>(characters) * 3'600'000.0 / static_cast<double>(active_ms);
}

ActivityTracker::ActivityTracker(ActivityStore& store_, QString game_, DayOf day_of_,
                                 qint64 idle_timeout_ms_)
    : idle_timeout_ms{idle_timeout_ms_}, store{store_}, game{std::move(game_)},
      day_of{day_of_ ? std::move(day_of_) : DayOf{ReadingRules::ReadingDay}} {}

LineAccounting ActivityTracker::OnLine(const TextLine& line, qint64 now) {
    const int characters = ReadingRules::CountCharacters(line.text);
    // What stood in the lines just before (a long text scrolled a screenful at a time) was
    // read then: only the rest counts. A choice is what it is each time.
    const int fresh = line.IsChoice() || paused || characters == 0
                          ? characters
                          : ReadingRules::CountCharacters(ReadingRules::WithoutOverlap(line.text, recent_texts));
    if (paused || characters == 0) {
        return LineAccounting{characters, false, false, std::nullopt};
    }
    // Coming back from a pause in reading: time before this line is not reading time.
    const bool resuming = !session_id || now - last_activity > idle_timeout_ms;
    const qint64 id = EnsureSession(now);
    if (IsRepeat(line) || fresh == 0) {
        // Shown again (a menu closed over it, a save loaded, the game repeating itself): the
        // reader is still there, and nothing new was read.
        last_activity = now;
        if (resuming || last_heartbeat == 0) {
            last_heartbeat = now;
        }
        return LineAccounting{characters, true, false, std::nullopt};
    }
    const bool reread = store.MarkSeen(game, line.source_id, now);
    ReadingCounters delta;
    if (line.IsChoice()) {
        delta.choice_characters = fresh;
    } else {
        has_narrative = true;
        delta.characters = fresh;
        delta.unique_characters = reread ? 0 : fresh;
        delta.reread_characters = reread ? fresh : 0;
        delta.lines = 1;
    }
    Add(now, delta);
    last_activity = now;
    if (resuming || last_heartbeat == 0) {
        last_heartbeat = now;
    }
    return LineAccounting{fresh, reread, true, id};
}

LineAccounting ActivityTracker::Count(const TextLine& line, qint64 now) {
    const bool was_paused = std::exchange(paused, false);
    const LineAccounting accounting = OnLine(line, now);
    paused = was_paused;
    return accounting;
}

int ActivityTracker::TakeBack(const QString& source_id, const QString& text) {
    Flush(); // what it counted may not be saved yet
    const std::optional<UncountedLine> line = store.UncountLine(game, source_id, text);
    if (!line) {
        return 0;
    }
    ReadingCounters delta;
    if (line->choice) {
        delta.choice_characters = -line->characters;
    } else {
        delta.characters = -line->characters;
        delta.unique_characters = line->reread ? 0 : -line->characters;
        delta.reread_characters = line->reread ? -line->characters : 0;
        delta.lines = -1;
    }
    store.AddActivity(line->session_id, game, day_of(line->timestamp), delta, line->timestamp);
    if (session && session_id == line->session_id) {
        session = *session + delta;
    }
    // It can be counted again.
    counted_sources.remove(line->source_id);
    recent_texts.removeAll(line->text.trimmed());
    return line->characters;
}

bool ActivityTracker::IsRepeat(const TextLine& line) {
    const QString text = line.text.trimmed();
    const bool known = (!line.source_id.isEmpty() && counted_sources.contains(line.source_id)) ||
                       recent_texts.contains(text);
    if (!line.source_id.isEmpty()) {
        counted_sources.insert(line.source_id);
    }
    if (!known) {
        recent_texts.append(text);
        if (recent_texts.size() > RecentTexts) {
            recent_texts.removeFirst();
        }
    }
    return known;
}

void ActivityTracker::OnInteraction(qint64 now) {
    if (paused || !session_id) {
        return;
    }
    if (now - last_activity <= idle_timeout_ms) {
        last_activity = now;
    }
}

void ActivityTracker::OnHeartbeat(qint64 now, bool reading) {
    const qint64 previous = last_heartbeat;
    last_heartbeat = now;
    if (paused || !reading || !has_narrative || !session_id || previous == 0) {
        return;
    }
    if (now <= previous || now - last_activity > idle_timeout_ms) {
        return;
    }
    if (now - last_activity > ReadingRules::SessionGapMs) {
        return;
    }
    ReadingCounters delta;
    delta.active_ms = std::min(now - previous, ReadingRules::MaxHeartbeatMs);
    Add(now, delta);
}

void ActivityTracker::Flush() {
    if (!session_id || pending.isEmpty()) {
        return;
    }
    for (const auto& [day, delta] : pending) {
        store.AddActivity(*session_id, game, day, delta, last_activity);
    }
    pending.clear();
}

void ActivityTracker::Reset() {
    pending.clear();
    counted_sources.clear();
    recent_texts.clear();
    session_id.reset();
    session.reset();
    has_narrative = false;
    last_activity = 0;
    last_heartbeat = 0;
}

void ActivityTracker::Add(qint64 now, const ReadingCounters& delta) {
    const QString day = day_of(now);
    bool found = false;
    for (auto& [pending_day, counters] : pending) {
        if (pending_day == day) {
            counters = counters + delta;
            found = true;
            break;
        }
    }
    if (!found) {
        pending.append({day, delta});
    }
    session = session.value_or(ReadingCounters{}) + delta;
}

qint64 ActivityTracker::EnsureSession(qint64 now) {
    if (session_id) {
        if (now - last_activity < ReadingRules::SessionGapMs) {
            return *session_id;
        }
        Flush();
        store.CloseSession(*session_id, last_activity);
        session_id.reset();
    }
    const std::optional<SessionRecord> open = store.OpenSession(game);
    qint64 id;
    if (open && now - open->last_activity < ReadingRules::SessionGapMs) {
        id = open->id;
    } else {
        if (open) {
            store.CloseSession(open->id, open->last_activity);
        }
        id = store.CreateSession(game, now);
    }
    session_id = id;
    const QStringList before = store.SessionSources(id);
    counted_sources = QSet<QString>(before.begin(), before.end());
    recent_texts.clear();
    session = store.SessionCounters(id);
    last_activity = now;
    return id;
}

namespace {

double Median(std::vector<double> values) {
    if (values.empty()) {
        return 0;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto at = static_cast<std::size_t>(
        std::clamp(fraction * static_cast<double>(values.size() - 1), 0.0, static_cast<double>(values.size() - 1)));
    return values[at];
}

/// The next line after `i`, or -1.
qsizetype NextLine(const QList<ReadingTime::Event>& events, qsizetype i) {
    for (qsizetype j = i + 1; j < events.size(); ++j) {
        if (events[j].line) {
            return j;
        }
    }
    return -1;
}

} // namespace

ReadingTime::Pace ReadingTime::PaceOf(const QList<Event>& events, qint64 idle_timeout_ms, const Pace& fallback) {
    struct Sample {
        double characters;
        double ms;
    };
    std::vector<Sample> samples;
    for (qsizetype i = 0; i < events.size(); ++i) {
        if (!events[i].line || events[i].characters <= 0) {
            continue;
        }
        const qsizetype next = NextLine(events, i);
        if (next < 0 || next != i + 1) {
            continue; // the last line, or a word looked up before the next: not plain reading
        }
        const qint64 gap = events[next].ts - events[i].ts;
        if (gap < 300 || gap > idle_timeout_ms) {
            continue;
        }
        samples.push_back({static_cast<double>(events[i].characters), static_cast<double>(gap)});
    }
    if (static_cast<int>(samples.size()) < MinSamples) {
        return fallback;
    }
    // At most 400 of them, spread over the whole, for the pairs below.
    if (samples.size() > 400) {
        std::vector<Sample> fewer;
        const double step = static_cast<double>(samples.size()) / 400.0;
        for (int k = 0; k < 400; ++k) {
            fewer.push_back(samples[static_cast<std::size_t>(k * step)]);
        }
        samples = std::move(fewer);
    }
    // A line's time as a fixed part and a part per character (Theil-Sen: the median slope of
    // every pair of lines of clearly different lengths, so long gaps do not pull it).
    std::vector<double> slopes;
    for (std::size_t a = 0; a < samples.size(); ++a) {
        for (std::size_t b = a + 1; b < samples.size(); ++b) {
            const double dc = samples[b].characters - samples[a].characters;
            if (std::abs(dc) >= 3) {
                slopes.push_back((samples[b].ms - samples[a].ms) / dc);
            }
        }
    }
    Pace pace;
    pace.samples = static_cast<int>(samples.size());
    std::vector<double> per;
    for (const Sample& sample : samples) {
        per.push_back(sample.ms / sample.characters);
    }
    // Between 1,200 and 120,000 characters an hour.
    pace.ms_per_character = std::clamp(slopes.size() >= 10 ? Median(slopes) : Median(per), 30.0, 3000.0);
    std::vector<double> rest;
    for (const Sample& sample : samples) {
        rest.push_back(sample.ms - pace.ms_per_character * sample.characters);
    }
    pace.fixed_ms = std::clamp(Median(rest), 0.0, 8000.0);
    std::vector<double> ratios;
    for (const Sample& sample : samples) {
        ratios.push_back(sample.ms / std::max(500.0, pace.fixed_ms + pace.ms_per_character * sample.characters));
    }
    pace.slow_factor = std::clamp(Percentile(ratios, 0.9), 1.5, 3.0);
    return pace;
}

qint64 ReadingTime::Allowance(const Pace& pace, int characters, qint64 idle_timeout_ms) {
    const double slow = pace.slow_factor * (pace.fixed_ms + pace.ms_per_character * std::max(0, characters));
    const qint64 at_most = std::max<qint64>(1000, idle_timeout_ms);
    return std::clamp(static_cast<qint64>(std::llround(slow)), std::min(MinLineMs, at_most), at_most);
}

QList<std::pair<qint64, qint64>> ReadingTime::Credit(const QList<Event>& events, const Pace& pace,
                                                     qint64 idle_timeout_ms) {
    QList<std::pair<qint64, qint64>> out;
    for (qsizetype i = 0; i < events.size(); ++i) {
        if (!events[i].line) {
            continue;
        }
        const qint64 allowance = Allowance(pace, events[i].characters, idle_timeout_ms);
        const qsizetype next = NextLine(events, i);
        const qsizetype end = next < 0 ? events.size() : next;
        qint64 credit = 0;
        qint64 from = events[i].ts;
        // Words looked up meanwhile: the reader was there up to each of them.
        for (qsizetype k = i + 1; k < end; ++k) {
            credit += std::clamp<qint64>(events[k].ts - from, 0, idle_timeout_ms);
            from = events[k].ts;
        }
        credit += next < 0 ? allowance : std::clamp<qint64>(events[next].ts - from, 0, allowance);
        out.append({events[i].ts, credit});
    }
    return out;
}

QString SessionSummary::Format(const ReadingCounters& counters, bool paused) {
    const QLocale locale;
    QString out;
    if (paused) {
        out += QStringLiteral("paused · ");
    }
    out += locale.toString(counters.characters) + QChar(u'字');
    const std::optional<double> speed = counters.CharactersPerHour();
    if (speed && counters.active_ms >= SpeedAfterMs) {
        // To the nearest ten: the last digit would change with every line.
        out += QStringLiteral(" · ") +
               locale.toString(static_cast<qint64>(std::llround(*speed / 10.0)) * 10) +
               QStringLiteral("/h");
    }
    out += QStringLiteral(" · ") + Duration(counters.active_ms);
    return out;
}

QString SessionSummary::Duration(qint64 active_ms) {
    const qint64 minutes = active_ms / 60000;
    if (minutes < 1) {
        return QStringLiteral("<1m");
    }
    if (minutes < 60) {
        return QStringLiteral("%1m").arg(minutes);
    }
    return QStringLiteral("%1h%2m").arg(minutes / 60).arg(minutes % 60, 2, 10, QChar(u'0'));
}

} // namespace ReaderApp
