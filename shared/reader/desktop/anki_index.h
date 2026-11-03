// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include "reader/desktop/anki.h"

namespace ReaderApp {

/**
 * What the deck cards go to holds, kept on this computer: for each note, what is in the field
 * Anki tells notes apart by. The popup asks it whether a word is in Anki before it is drawn,
 * so the right box is there at once; Anki itself is asked afterwards, to confirm.
 *
 * It is filled by reading every note of the deck once (in pages), brought up to date with
 * the notes changed since (RefreshAfterMs), and read whole again now and then
 * (RefillAfterMs), which is how it learns of notes that were deleted. What Anki says about a
 * word in between goes into it too (Learn, Add). It is kept in a file, so that it answers
 * from the start when the app starts again, also while Anki is closed.
 *
 * Safe to use from several threads.
 */
class AnkiIndex {
public:
    /// Which notes it holds: those at one Anki address, in one deck (or the whole collection
    /// when `scope` is empty), by one field. Another source makes it start over.
    struct Source {
        QString url;
        QString scope;
        QString field;

        bool operator==(const Source&) const = default;
    };

    enum class Answer {
        NotReady, ///< Not filled for this source yet: only Anki can tell.
        Absent,
        Present,
    };
    struct Found {
        Answer answer{Answer::NotReady};
        QList<qint64> notes;
    };

    /// What it should do next: nothing, read what changed, or read everything.
    enum class Due { Nothing, Refresh, Fill };

    static constexpr int PageSize = 200;
    static constexpr qint64 RefreshAfterMs = 3 * 60 * 1000;
    static constexpr qint64 RefillAfterMs = 30 * 60 * 1000;

    [[nodiscard]] Found Find(const Source& source, const QString& value) const;
    [[nodiscard]] Due WhatIsDue(const Source& source, qint64 now) const;

    /// Reads every note of the source from Anki and holds those instead of what it held.
    /// Stops early, keeping what it held, when `cancelled` says so. Throws AnkiError.
    void Fill(AnkiConnectClient& client, const Source& source, qint64 now,
              const std::function<bool()>& cancelled = {});
    /// Reads the notes changed in the last day (Anki's "edited:1") and holds them as they are
    /// now. A source it is not filled for is filled instead. Throws AnkiError.
    void Refresh(AnkiConnectClient& client, const Source& source, qint64 now,
                 const std::function<bool()>& cancelled = {});
    /// What Anki said it has for `value`: no notes, or these.
    void Learn(const Source& source, const QString& value, const QList<qint64>& notes);
    /// A note that was just added with `value`.
    void Add(const Source& source, const QString& value, qint64 note);

    /// Keeps what it holds in `path`, or reads it back from there. False when that failed;
    /// reading a file of another form leaves it as it was.
    bool Save(const QString& path) const;
    bool Load(const QString& path);
    void Clear();

    [[nodiscard]] qsizetype Size() const;
    /// A field's value as it is compared: without markup, spaces at its ends or case.
    [[nodiscard]] static QString Normal(const QString& value);

private:
    void Put(qint64 id, const QString& value);
    void Drop(qint64 id);
    [[nodiscard]] static QHash<qint64, QString> Read(AnkiConnectClient& client,
                                                     const QList<qint64>& ids,
                                                     const QString& field,
                                                     const std::function<bool()>& cancelled,
                                                     bool& stopped);

    mutable QMutex mutex;
    Source held;
    bool ready{};
    qint64 filled_at{};
    qint64 refreshed_at{};
    QHash<QString, QList<qint64>> by_value;
    QHash<qint64, QString> value_of;
};

} // namespace ReaderApp
