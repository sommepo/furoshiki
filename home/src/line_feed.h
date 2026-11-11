// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Lines that come from somewhere other than a core (docs/furoshiki-3.md, "Sources"): what a
// texthooker sends over its WebSocket, text copied to the clipboard. From what arrived to one
// line of Japanese text, and whether it is a new line: the rules Furochibi's cleaner
// (furochibi/server/console_ocr/cleaner.py) has for that, as far as they apply to text that was
// sent, not read off a picture. Its rules for an OCR engine's misreadings (stray characters
// round a line, a character read differently, text scrolling) belong to a window read by OCR.
//
// Qt Core only: tested by reader-home-tests.

#include <optional>
#include <QList>
#include <QRectF>
#include <QString>

namespace Home::Lines {

/// Spaces and line breaks are layout: one line, with no space beside a Japanese character.
[[nodiscard]] QString Normalise(const QString& text);
/// Nothing Japanese in it: stray marks, numbers, letters.
[[nodiscard]] bool IsJunk(const QString& text);
/// The text with spaces and punctuation aside, as two lines are compared.
[[nodiscard]] QString Key(const QString& text);

enum class Verdict {
    New,
    Same,      ///< the previous line again
    CarriesOn, ///< the previous line with more after it: text that was still being written
    PartOf,    ///< the beginning of the previous line
    Junk,      ///< empty, or nothing Japanese
};
/// What `next` is, given the line before it (empty: none).
[[nodiscard]] Verdict Compare(const QString& previous, const QString& next);

/// The text of one message of a texthooker's WebSocket: the frame itself, or the "sentence"
/// (else "text") of a frame that is a JSON object.
[[nodiscard]] QString MessageText(const QString& frame);

/// A source's game ID in the reader: "LINES-<KEY>" for a title with a record (titles/<key>.json),
/// else "LINES-" and eight hex digits made from the title.
[[nodiscard]] QString GameId(const QString& title, const QString& record_key);
/// A line's identity within its title (TextLine::source_id): the same text again is rereading.
[[nodiscard]] QString SourceId(const QString& text);
/// Where each character of `line` (one a code point) stands on the screen, from what OCR read
/// there (`seen`, with a box for each of its code points): the two lined up character by
/// character (their longest common sequence, width and case folded), an empty box for one OCR
/// did not see. `matched` gets how many found a place.
[[nodiscard]] QList<QRectF> PlaceAlong(const QString& line, const QString& seen, const QList<QRectF>& boxes, int* matched = nullptr);

/**
 * The lines of one source in order. A line waits a moment before it is handed over (Settle), so
 * that text sent piece by piece as a game writes it is one line; the same line again is dropped;
 * a line that carries on one already handed over takes that one back.
 */
class Feed {
public:
    struct Out {
        QString text;
        /// A line handed over before, of which this is the whole: to be taken back.
        std::optional<QString> takes_back;
    };

    /// How long a line waits for more of itself.
    static constexpr qint64 SettleMs = 150;
    /// Within this of the line before, a line that carries it on replaces it and a line that is
    /// its beginning is dropped. Later, both are lines of their own: what is sent is exact, and
    /// はい after はい、行こう is a line.
    static constexpr qint64 CarryMs = 8000;

    /// Text arrived at `now` (ms). What must be handed over at once (the line that waited, when
    /// this is another line); the text itself waits (HasPending) until Settle.
    QList<Out> Take(const QString& text, qint64 now);
    /// The wait is over: the line that waited, if any.
    std::optional<Out> Settle(qint64 now);
    [[nodiscard]] bool HasPending() const {
        return !pending.isEmpty();
    }
    void Reset();

private:
    QString pending;
    qint64 pending_at{};
    std::optional<QString> pending_takes_back;
    QString handed;
    qint64 handed_at{};
};

} // namespace Home::Lines
