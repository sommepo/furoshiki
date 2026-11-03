// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include "reader/desktop/anki_index.h"

namespace ReaderApp {

namespace {
constexpr int FileVersion = 1;
} // namespace

QString AnkiIndex::Normal(const QString& value) {
    static const QRegularExpression markup{QStringLiteral("<[^>]*>")};
    QString out = value;
    out.remove(markup);
    out.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return out.trimmed().toCaseFolded();
}

AnkiIndex::Found AnkiIndex::Find(const Source& source, const QString& value) const {
    QMutexLocker lock(&mutex);
    if (!ready || !(held == source)) {
        return {};
    }
    const auto found = by_value.constFind(Normal(value));
    if (found == by_value.constEnd() || found->isEmpty()) {
        return {Answer::Absent, {}};
    }
    return {Answer::Present, *found};
}

AnkiIndex::Due AnkiIndex::WhatIsDue(const Source& source, qint64 now) const {
    QMutexLocker lock(&mutex);
    if (!ready || !(held == source) || now - filled_at >= RefillAfterMs || now < filled_at) {
        return Due::Fill;
    }
    return now - refreshed_at >= RefreshAfterMs ? Due::Refresh : Due::Nothing;
}

QHash<qint64, QString> AnkiIndex::Read(AnkiConnectClient& client, const QList<qint64>& ids,
                                       const QString& field,
                                       const std::function<bool()>& cancelled, bool& stopped) {
    QHash<qint64, QString> out;
    stopped = false;
    for (qsizetype at = 0; at < ids.size(); at += PageSize) {
        if (cancelled && cancelled()) {
            stopped = true;
            return out;
        }
        for (const AnkiConnectClient::NoteFields& note : client.NotesInfo(ids.mid(at, PageSize))) {
            const auto value = note.fields.constFind(field);
            if (value != note.fields.constEnd()) {
                out.insert(note.id, Normal(*value));
            }
        }
    }
    return out;
}

void AnkiIndex::Fill(AnkiConnectClient& client, const Source& source, qint64 now,
                     const std::function<bool()>& cancelled) {
    const QList<qint64> ids = client.FindNotesWhere(AnkiSearch::HasField(source.scope, source.field));
    bool stopped = false;
    const QHash<qint64, QString> values = Read(client, ids, source.field, cancelled, stopped);
    if (stopped) {
        return;
    }
    QMutexLocker lock(&mutex);
    held = source;
    ready = true;
    filled_at = now;
    refreshed_at = now;
    by_value.clear();
    value_of.clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        Put(it.key(), it.value());
    }
}

void AnkiIndex::Refresh(AnkiConnectClient& client, const Source& source, qint64 now,
                        const std::function<bool()>& cancelled) {
    {
        QMutexLocker lock(&mutex);
        if (!ready || !(held == source)) {
            lock.unlock();
            Fill(client, source, now, cancelled);
            return;
        }
    }
    const QList<qint64> ids = client.FindNotesWhere(
        AnkiSearch::HasField(source.scope, source.field) + QStringLiteral(" edited:1"));
    bool stopped = false;
    const QHash<qint64, QString> values = Read(client, ids, source.field, cancelled, stopped);
    if (stopped) {
        return;
    }
    QMutexLocker lock(&mutex);
    if (!(held == source)) {
        return;
    }
    refreshed_at = now;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        Put(it.key(), it.value());
    }
}

void AnkiIndex::Learn(const Source& source, const QString& value, const QList<qint64>& notes) {
    QMutexLocker lock(&mutex);
    if (!ready || !(held == source)) {
        return;
    }
    const QString key = Normal(value);
    // Notes that Anki no longer gives for this value were deleted or changed.
    for (const qint64 id : by_value.value(key)) {
        if (!notes.contains(id)) {
            Drop(id);
        }
    }
    for (const qint64 id : notes) {
        Put(id, key);
    }
}

void AnkiIndex::Add(const Source& source, const QString& value, qint64 note) {
    QMutexLocker lock(&mutex);
    if (ready && held == source) {
        Put(note, Normal(value));
    }
}

void AnkiIndex::Put(qint64 id, const QString& value) {
    Drop(id);
    value_of.insert(id, value);
    QList<qint64>& notes = by_value[value];
    if (!notes.contains(id)) {
        notes.append(id);
    }
}

void AnkiIndex::Drop(qint64 id) {
    const auto old = value_of.constFind(id);
    if (old == value_of.constEnd()) {
        return;
    }
    const auto notes = by_value.find(*old);
    if (notes != by_value.end()) {
        notes->removeAll(id);
        if (notes->isEmpty()) {
            by_value.erase(notes);
        }
    }
    value_of.erase(old);
}

bool AnkiIndex::Save(const QString& path) const {
    QJsonObject notes;
    QJsonObject root;
    {
        QMutexLocker lock(&mutex);
        if (!ready) {
            return false;
        }
        for (auto it = value_of.constBegin(); it != value_of.constEnd(); ++it) {
            notes.insert(QString::number(it.key()), it.value());
        }
        root = QJsonObject{{QStringLiteral("version"), FileVersion},
                           {QStringLiteral("url"), held.url},
                           {QStringLiteral("scope"), held.scope},
                           {QStringLiteral("field"), held.field},
                           {QStringLiteral("filled_at"), filled_at},
                           {QStringLiteral("refreshed_at"), refreshed_at}};
    }
    root.insert(QStringLiteral("notes"), notes);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool AnkiIndex::Load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != FileVersion) {
        return false;
    }
    const QJsonObject notes = root.value(QStringLiteral("notes")).toObject();
    QMutexLocker lock(&mutex);
    held = Source{root.value(QStringLiteral("url")).toString(),
                  root.value(QStringLiteral("scope")).toString(),
                  root.value(QStringLiteral("field")).toString()};
    ready = true;
    filled_at = root.value(QStringLiteral("filled_at")).toInteger();
    refreshed_at = root.value(QStringLiteral("refreshed_at")).toInteger();
    by_value.clear();
    value_of.clear();
    for (auto it = notes.constBegin(); it != notes.constEnd(); ++it) {
        Put(it.key().toLongLong(), it.value().toString());
    }
    return true;
}

void AnkiIndex::Clear() {
    QMutexLocker lock(&mutex);
    held = {};
    ready = false;
    filled_at = 0;
    refreshed_at = 0;
    by_value.clear();
    value_of.clear();
}

qsizetype AnkiIndex::Size() const {
    QMutexLocker lock(&mutex);
    return value_of.size();
}

} // namespace ReaderApp
