// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMutex>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include "reader/desktop/anki.h"
#include "reader/desktop/anki_index.h"
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;

namespace {

/**
 * A collection behind a pretend AnkiConnect, answering as many requests as it is sent, one
 * per connection, on a thread of its own: findNotes for the searches the reader makes, multi,
 * notesInfo. No Anki is involved.
 */
class FakeAnki : public QThread {
public:
    struct Note {
        QString deck;
        QMap<QString, QString> fields;
    };

    FakeAnki() {
        server.listen(QHostAddress::LocalHost, 0);
        port = server.serverPort();
        server.moveToThread(this);
        start();
    }
    ~FakeAnki() override {
        stop = true;
        wait();
    }

    void Set(qint64 id, const QString& deck, const QString& word) {
        QMutexLocker lock(&mutex);
        notes.insert(id, Note{deck, {{QStringLiteral("Word"), word}}});
    }
    void Remove(qint64 id) {
        QMutexLocker lock(&mutex);
        notes.remove(id);
        edited.remove(id);
    }
    void Edit(qint64 id, const QString& word) {
        QMutexLocker lock(&mutex);
        notes[id].fields[QStringLiteral("Word")] = word;
        edited.insert(id);
    }
    int Count(const QString& action) {
        QMutexLocker lock(&mutex);
        return asked.value(action);
    }
    void ResetCounts() {
        QMutexLocker lock(&mutex);
        asked.clear();
    }
    QString Url() const {
        return QStringLiteral("127.0.0.1:%1").arg(port);
    }

    /// Replies a multi request with bare results, as an AnkiConnect older than version 5 does.
    std::atomic<bool> bare_multi{false};

protected:
    void run() override {
        while (!stop) {
            if (!server.waitForNewConnection(50)) {
                continue;
            }
            QTcpSocket* socket = server.nextPendingConnection();
            QByteArray request;
            while (!request.contains("\r\n\r\n") && socket->waitForReadyRead(5000)) {
                request += socket->readAll();
            }
            const qsizetype head = request.indexOf("\r\n\r\n") + 4;
            const qsizetype at = request.toLower().indexOf("content-length:");
            const qsizetype length =
                request.mid(at + 15, request.indexOf('\r', at) - at - 15).trimmed().toLongLong();
            while (request.size() < head + length && socket->waitForReadyRead(5000)) {
                request += socket->readAll();
            }
            const QJsonObject sent = QJsonDocument::fromJson(request.mid(head)).object();
            const QByteArray body = QJsonDocument(Answer(sent)).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->waitForBytesWritten(5000);
            socket->disconnectFromHost();
            delete socket;
        }
        server.close();
    }

private:
    QJsonObject Answer(const QJsonObject& request) {
        const QString action = request.value(QStringLiteral("action")).toString();
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        {
            QMutexLocker lock(&mutex);
            ++asked[action];
        }
        if (action == QStringLiteral("multi")) {
            QJsonArray results;
            for (const QJsonValue& inner : params.value(QStringLiteral("actions")).toArray()) {
                const QJsonObject reply = Answer(inner.toObject());
                results.append(bare_multi ? reply.value(QStringLiteral("result")) : QJsonValue(reply));
            }
            return Reply(results);
        }
        QMutexLocker lock(&mutex);
        if (action == QStringLiteral("findNotes")) {
            return Reply(Find(params.value(QStringLiteral("query")).toString()));
        }
        if (action == QStringLiteral("notesInfo")) {
            QJsonArray out;
            for (const QJsonValue& id : params.value(QStringLiteral("notes")).toArray()) {
                const auto note = notes.constFind(id.toInteger());
                if (note == notes.constEnd()) {
                    out.append(QJsonObject{});
                    continue;
                }
                QJsonObject fields;
                for (auto it = note->fields.constBegin(); it != note->fields.constEnd(); ++it) {
                    fields.insert(it.key(), QJsonObject{{QStringLiteral("value"), it.value()},
                                                        {QStringLiteral("order"), 0}});
                }
                out.append(QJsonObject{{QStringLiteral("noteId"), id.toInteger()},
                                       {QStringLiteral("fields"), fields}});
            }
            return Reply(out);
        }
        return QJsonObject{{QStringLiteral("result"), QJsonValue()},
                           {QStringLiteral("error"), QStringLiteral("unsupported action")}};
    }

    /// The searches AnkiSearch makes: `"deck:D" "Word:value"`, `"deck:D" "Word:_*"`, the
    /// latter with ` edited:1`.
    QJsonArray Find(const QString& query) const {
        QString deck;
        if (query.startsWith(QStringLiteral("\"deck:"))) {
            const qsizetype end = query.indexOf(u'"', 6);
            deck = query.mid(6, end - 6);
        }
        const qsizetype word_at = query.indexOf(QStringLiteral("\"Word:"));
        QString value = query.mid(word_at + 6, query.indexOf(u'"', word_at + 6) - word_at - 6);
        const bool any = value == QStringLiteral("_*");
        value.remove(u'\\');
        const bool only_edited = query.contains(QStringLiteral("edited:1"));
        QJsonArray out;
        for (auto it = notes.constBegin(); it != notes.constEnd(); ++it) {
            if (!deck.isEmpty() && it->deck != deck) {
                continue;
            }
            if (only_edited && !edited.contains(it.key())) {
                continue;
            }
            const QString word = it->fields.value(QStringLiteral("Word"));
            if (any ? !word.isEmpty() : word == value) {
                out.append(it.key());
            }
        }
        return out;
    }

    static QJsonObject Reply(const QJsonValue& result) {
        return QJsonObject{{QStringLiteral("result"), result}, {QStringLiteral("error"), QJsonValue()}};
    }

    QTcpServer server;
    int port{};
    std::atomic<bool> stop{false};
    QMutex mutex;
    QMap<qint64, Note> notes;
    QSet<qint64> edited;
    QHash<QString, int> asked;
};

const QString Deck = QStringLiteral("Mining");
const QString Field = QStringLiteral("Word");

AnkiIndex::Source SourceAt(const FakeAnki& anki) {
    return {anki.Url(), Deck, Field};
}

} // namespace

TEST_CASE("AnkiIndex: a deck is read once, in pages, and answers at once", "[reader][anki]") {
    FakeAnki anki;
    for (int i = 1; i <= 450; ++i) {
        anki.Set(i, Deck, QStringLiteral("word%1").arg(i));
    }
    anki.Set(1000, Deck, Q(u"<b>死ぬほど</b>&nbsp;"));
    anki.Set(1001, QStringLiteral("Other"), Q(u"食べる"));
    anki.Set(1002, Deck, QString()); // a note with nothing in the field is none of them

    AnkiIndex index;
    const AnkiIndex::Source source = SourceAt(anki);
    REQUIRE(index.Find(source, Q(u"死ぬほど")).answer == AnkiIndex::Answer::NotReady);
    REQUIRE(index.WhatIsDue(source, 1000) == AnkiIndex::Due::Fill);

    AnkiConnectClient client(anki.Url());
    index.Fill(client, source, 1000);
    REQUIRE(index.Size() == 451);
    REQUIRE(anki.Count(QStringLiteral("findNotes")) == 1);
    REQUIRE(anki.Count(QStringLiteral("notesInfo")) == 3); // 451 notes, 200 at a time

    // Markup, spaces and case do not matter.
    const AnkiIndex::Found found = index.Find(source, Q(u"死ぬほど"));
    REQUIRE(found.answer == AnkiIndex::Answer::Present);
    REQUIRE(found.notes == QList<qint64>{1000});
    REQUIRE(index.Find(source, QStringLiteral("WORD7")).answer == AnkiIndex::Answer::Present);
    // Another deck's note is not in this one.
    REQUIRE(index.Find(source, Q(u"食べる")).answer == AnkiIndex::Answer::Absent);
    // Another source is not answered for.
    REQUIRE(index.Find({anki.Url(), {}, Field}, Q(u"食べる")).answer ==
            AnkiIndex::Answer::NotReady);

    REQUIRE(index.WhatIsDue(source, 1000 + 1000) == AnkiIndex::Due::Nothing);
    REQUIRE(index.WhatIsDue(source, 1000 + AnkiIndex::RefreshAfterMs) == AnkiIndex::Due::Refresh);
    REQUIRE(index.WhatIsDue(source, 1000 + AnkiIndex::RefillAfterMs) == AnkiIndex::Due::Fill);
}

TEST_CASE("AnkiIndex: what changed is read, what Anki says is kept", "[reader][anki]") {
    FakeAnki anki;
    anki.Set(1, Deck, Q(u"猫"));
    anki.Set(2, Deck, Q(u"犬"));
    AnkiIndex index;
    const AnkiIndex::Source source = SourceAt(anki);
    AnkiConnectClient client(anki.Url());
    index.Fill(client, source, 0);

    // A note added in Anki, and one changed there: a refresh reads only those.
    anki.Set(3, Deck, Q(u"鳥"));
    anki.Edit(3, Q(u"鳥"));
    anki.Edit(2, Q(u"狼"));
    anki.ResetCounts();
    index.Refresh(client, source, AnkiIndex::RefreshAfterMs);
    REQUIRE(index.Find(source, Q(u"鳥")).answer == AnkiIndex::Answer::Present);
    REQUIRE(index.Find(source, Q(u"狼")).notes == QList<qint64>{2});
    REQUIRE(index.Find(source, Q(u"犬")).answer == AnkiIndex::Answer::Absent);
    REQUIRE(anki.Count(QStringLiteral("notesInfo")) == 1);
    REQUIRE(index.WhatIsDue(source, AnkiIndex::RefreshAfterMs + 1) == AnkiIndex::Due::Nothing);

    // A deleted note is not seen by a refresh; Anki's answer about the word takes it out.
    anki.Remove(1);
    REQUIRE(index.Find(source, Q(u"猫")).answer == AnkiIndex::Answer::Present);
    index.Learn(source, Q(u"猫"), {});
    REQUIRE(index.Find(source, Q(u"猫")).answer == AnkiIndex::Answer::Absent);

    // A card the reader just made is in it at once.
    index.Add(source, Q(u"魚"), 77);
    REQUIRE(index.Find(source, Q(u"魚")).notes == QList<qint64>{77});
    // So is a note Anki names that it had not read yet.
    index.Learn(source, Q(u"虫"), {88, 89});
    REQUIRE(index.Find(source, Q(u"虫")).notes == QList<qint64>{88, 89});
}

TEST_CASE("AnkiIndex: kept in a file, and stopped part way", "[reader][anki]") {
    FakeAnki anki;
    anki.Set(1, Deck, Q(u"猫"));
    const AnkiIndex::Source source = SourceAt(anki);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("anki_index.json"));
    {
        AnkiIndex index;
        REQUIRE_FALSE(index.Save(path)); // nothing read yet: nothing to keep
        AnkiConnectClient client(anki.Url());
        index.Fill(client, source, 5000);
        REQUIRE(index.Save(path));
    }
    AnkiIndex again;
    REQUIRE(again.Load(path));
    REQUIRE(again.Find(source, Q(u"猫")).notes == QList<qint64>{1});
    REQUIRE(again.WhatIsDue(source, 5000 + 1) == AnkiIndex::Due::Nothing);
    REQUIRE(again.WhatIsDue(source, 5000 + AnkiIndex::RefillAfterMs) == AnkiIndex::Due::Fill);
    REQUIRE_FALSE(again.Load(dir.filePath(QStringLiteral("missing.json"))));

    // A fill that is called off leaves what was there.
    anki.Set(2, Deck, Q(u"犬"));
    AnkiConnectClient client(anki.Url());
    again.Fill(client, source, 9000, [] { return true; });
    REQUIRE(again.Find(source, Q(u"犬")).answer == AnkiIndex::Answer::Absent);
    REQUIRE(again.Find(source, Q(u"猫")).answer == AnkiIndex::Answer::Present);
}

TEST_CASE("AnkiConnectClient: several words asked in one request", "[reader][anki]") {
    FakeAnki anki;
    anki.Set(1, Deck, Q(u"死ぬほど"));
    anki.Set(2, Deck, Q(u"死ぬ"));
    anki.Set(3, QStringLiteral("Other"), Q(u"死"));
    for (const bool bare : {false, true}) {
        anki.bare_multi = bare;
        anki.ResetCounts();
        AnkiConnectClient client(anki.Url());
        const QList<QList<qint64>> found = client.FindNotesEach(
            Deck, Field, {Q(u"死ぬほど"), Q(u"死"), Q(u"死ぬ")});
        REQUIRE(found == QList<QList<qint64>>{{1}, {}, {2}});
        REQUIRE(anki.Count(QStringLiteral("multi")) == 1);
    }
    AnkiConnectClient client(anki.Url());
    REQUIRE(client.FindNotesEach(Deck, Field, {}).isEmpty());
    REQUIRE(AnkiSearch::HasField(Deck, QStringLiteral("My:Word")) ==
            QStringLiteral("\"deck:Mining\" \"My\\:Word:_*\""));
}
