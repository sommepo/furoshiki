// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "line_feed.h"

#include <vector>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace Home::Lines {

namespace {

/// Kana, kanji (with the rare and the compatibility ones) and half-width kana.
bool IsJapanese(char32_t c) {
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF66 && c <= 0xFF9F) || (c >= 0x20000 && c <= 0x2FA1F);
}

bool HasJapanese(const QString& text) {
    for (const char32_t c : text.toUcs4()) {
        if (IsJapanese(c)) {
            return true;
        }
    }
    return false;
}

} // namespace

QString Normalise(const QString& text) {
    // Runs of spaces and line breaks become one space, none at the ends.
    const QString spaced = text.simplified();
    if (!HasJapanese(spaced)) {
        return spaced;
    }
    // Japanese has no spaces between words: one beside a character that is not ASCII is layout.
    QString out;
    out.reserve(spaced.size());
    for (qsizetype i = 0; i < spaced.size(); ++i) {
        const QChar c = spaced[i];
        if (c == QLatin1Char(' ')) {
            const bool before = i > 0 && spaced[i - 1].unicode() > 0x7f;
            const bool after = i + 1 < spaced.size() && spaced[i + 1].unicode() > 0x7f;
            if (before || after) {
                continue;
            }
        }
        out.append(c);
    }
    return out;
}

bool IsJunk(const QString& text) {
    return !HasJapanese(text);
}

QString Key(const QString& text) {
    static const QString marks = QStringLiteral("、。，．,.!！?？「」『』…・〜ー-");
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (!c.isSpace() && !marks.contains(c)) {
            out.append(c);
        }
    }
    return out;
}

Verdict Compare(const QString& previous, const QString& next) {
    if (next.isEmpty() || IsJunk(next)) {
        return Verdict::Junk;
    }
    if (previous.isEmpty()) {
        return Verdict::New;
    }
    const QString a = Key(previous), b = Key(next);
    if (a == b) {
        return Verdict::Same;
    }
    if (!a.isEmpty() && b.startsWith(a)) {
        return Verdict::CarriesOn;
    }
    if (!b.isEmpty() && a.startsWith(b)) {
        return Verdict::PartOf;
    }
    return Verdict::New;
}

QString MessageText(const QString& frame) {
    const QString trimmed = frame.trimmed();
    if (trimmed.startsWith(QLatin1Char('{'))) {
        QJsonParseError error{};
        const QJsonDocument json = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && json.isObject()) {
            const QJsonObject object = json.object();
            for (const QString& field : {QStringLiteral("sentence"), QStringLiteral("text")}) {
                if (object.value(field).isString()) {
                    return object.value(field).toString();
                }
            }
            return {}; // a message of another kind: no line
        }
    }
    return frame;
}

QString GameId(const QString& title, const QString& record_key) {
    if (!record_key.isEmpty()) {
        return QStringLiteral("LINES-") + record_key.toUpper();
    }
    const QByteArray hash = QCryptographicHash::hash(title.trimmed().toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("LINES-") + QString::fromLatin1(hash.left(8)).toUpper();
}

QString SourceId(const QString& text) {
    const QByteArray hash = QCryptographicHash::hash(Key(text).toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("l:") + QString::fromLatin1(hash.left(16));
}

QList<Feed::Out> Feed::Take(const QString& raw, qint64 now) {
    QList<Out> out;
    const QString text = Normalise(raw);
    const bool waiting = !pending.isEmpty();
    const QString& before = waiting ? pending : handed;
    const qint64 before_at = waiting ? pending_at : handed_at;
    Verdict verdict = Compare(before, text);
    if ((verdict == Verdict::CarriesOn || verdict == Verdict::PartOf) && now - before_at > CarryMs) {
        verdict = Verdict::New;
    }
    switch (verdict) {
    case Verdict::Junk:
    case Verdict::Same:
    case Verdict::PartOf:
        break;
    case Verdict::CarriesOn:
        if (!waiting) {
            pending_takes_back = handed;
        }
        pending = text;
        pending_at = now;
        break;
    case Verdict::New:
        if (waiting) {
            out.append(*Settle(now));
        }
        pending = text;
        pending_at = now;
        pending_takes_back.reset();
        break;
    }
    return out;
}

std::optional<Feed::Out> Feed::Settle(qint64 now) {
    if (pending.isEmpty()) {
        return std::nullopt;
    }
    Out out{pending, pending_takes_back};
    handed = pending;
    handed_at = now;
    pending.clear();
    pending_takes_back.reset();
    return out;
}

QList<QRectF> PlaceAlong(const QString& line, const QString& seen, const QList<QRectF>& boxes, int* matched) {
    const auto points = [](const QString& text) {
        QList<QString> out;
        for (const uint c : text.toUcs4()) {
            out.append(QString::fromUcs4(reinterpret_cast<const char32_t*>(&c), 1).normalized(QString::NormalizationForm_KC).toCaseFolded());
        }
        return out;
    };
    const QList<QString> a = points(line), b = points(seen);
    const qsizetype n = a.size(), m = b.size();
    QList<QRectF> placed(n);
    int found = 0;
    if (n > 0 && m > 0 && n * m <= 400000) {
        // The longest common sequence's table, then back along it.
        std::vector<int> table(static_cast<std::size_t>((n + 1) * (m + 1)), 0);
        const auto at = [&](qsizetype i, qsizetype j) -> int& { return table[static_cast<std::size_t>(i * (m + 1) + j)]; };
        for (qsizetype i = n - 1; i >= 0; --i) {
            for (qsizetype j = m - 1; j >= 0; --j) {
                at(i, j) = a[i] == b[j] && !a[i].trimmed().isEmpty() ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
            }
        }
        qsizetype i = 0, j = 0;
        while (i < n && j < m) {
            if (a[i] == b[j] && !a[i].trimmed().isEmpty() && at(i, j) == at(i + 1, j + 1) + 1) {
                placed[i] = boxes.value(j);
                found += placed[i].isEmpty() ? 0 : 1;
                ++i;
                ++j;
            } else if (at(i + 1, j) >= at(i, j + 1)) {
                ++i;
            } else {
                ++j;
            }
        }
    }
    if (matched != nullptr) {
        *matched = found;
    }
    return placed;
}

void Feed::Reset() {
    *this = Feed();
}

} // namespace Home::Lines
