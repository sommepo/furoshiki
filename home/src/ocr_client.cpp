// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ocr_client.h"
#include "ocr_helper.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace Home {

namespace {
/// A picture is sent no larger than this: the service reads the area at the size it comes.
constexpr int LongestSide = 1920;
constexpr int RetryMs = 2000;

QString ErrorOf(QNetworkReply* reply, const QByteArray& body) {
    const QString said = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toString();
    if (!said.isEmpty()) {
        return said;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        const QString text = QString::fromUtf8(body.left(200)).trimmed();
        return text.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : text;
    }
    return reply->errorString();
}
} // namespace

OcrClient::OcrClient(QObject* parent) : QObject(parent), net(new QNetworkAccessManager(this)) {
    connect(&OcrHelper::Instance(), &OcrHelper::Changed, this, &OcrClient::StatusChanged);
}

void OcrClient::Set(const Lines::Helper& helper_) {
    Unfollow();
    // A helper installed on this computer says where it answers itself (ocr_helper.h).
    helper = Refreshed(helper_);
    helper.address = Lines::CleanHelperAddress(helper.address);
    pushing = false;
    waking = false;
    sent = taken = 0;
    state.clear();
    last_error.clear();
    emit StatusChanged();
}

QNetworkRequest OcrClient::Request(const QString& path, int timeout_ms) const {
    QNetworkRequest request(QUrl(helper.address + path));
    request.setTransferTimeout(timeout_ms);
    if (!helper.token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + helper.token.toUtf8());
    }
    return request;
}

void OcrClient::Json(const QString& verb, const QString& path, const QJsonObject& body, int timeout_ms,
                     std::function<void(bool, const QJsonObject&, const QString&)> then) {
    if (!Configured()) {
        then(false, {}, QStringLiteral("no OCR helper's address is set"));
        return;
    }
    QNetworkRequest request = Request(path, timeout_ms);
    QNetworkReply* reply = nullptr;
    if (verb == QStringLiteral("GET")) {
        reply = net->get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = net->sendCustomRequest(request, verb.toLatin1(), QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    const int mine = generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, then = std::move(then), mine] {
        reply->deleteLater();
        if (mine != generation) {
            return; // asked of another helper, or before the following stopped
        }
        const QByteArray answer = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 300) {
            then(false, QJsonDocument::fromJson(answer).object(), ErrorOf(reply, answer));
        } else {
            then(true, QJsonDocument::fromJson(answer).object(), QString());
        }
    });
}

void OcrClient::Say(const QString& state_, const QString& error) {
    if (state == state_ && last_error == error) {
        return;
    }
    state = state_;
    last_error = error;
    emit StatusChanged();
}

QString OcrClient::Status() const {
    if (!Configured()) {
        return QStringLiteral("no OCR helper's address is set");
    }
    if (waking && OcrHelper::Instance().Starting()) {
        return QStringLiteral("starting the helper · ") + helper.address;
    }
    QString said = (state.isEmpty() ? QStringLiteral("not asked yet") : state) + QStringLiteral(" · ") + helper.address;
    if (!last_error.isEmpty()) {
        said += QStringLiteral(" · ") + last_error;
    }
    return said;
}

void OcrClient::Wake(std::function<void(bool, const QString&)> then) {
    if (helper.program.isEmpty() || !Configured()) {
        then(true, QString()); // someone else's to run: asked as it is
        return;
    }
    const int mine = generation;
    waking = true;
    OcrHelper::Instance().Ensure(helper, [this, mine, then = std::move(then)](bool ok, const QString& what) {
        if (mine == generation) {
            waking = false;
            then(ok, what);
        }
    });
}

void OcrClient::Ask() {
    Wake([this](bool awake, const QString& why) {
        if (!awake) {
            Say(QStringLiteral("no answer"), why);
            emit Answered(false, why);
            return;
        }
        AskSource();
    });
}

void OcrClient::AskSource() {
    Json(QStringLiteral("GET"), QStringLiteral("/api/source"), {}, 6000, [this](bool ok, const QJsonObject& answer, const QString& error) {
        if (!ok) {
            Say(QStringLiteral("no answer"), error);
            emit Answered(false, error);
            return;
        }
        for (const QJsonValue& value : answer.value(QStringLiteral("sources")).toArray()) {
            const QJsonObject source = value.toObject();
            if (source.value(QStringLiteral("id")).toString() == helper.source) {
                if (!source.value(QStringLiteral("push")).toBool()) {
                    break;
                }
                Say(QStringLiteral("answers"), QString());
                emit Answered(true, source.value(QStringLiteral("name")).toString());
                return;
            }
        }
        const QString what = QStringLiteral("it has no pushed picture source \"%1\"").arg(helper.source);
        Say(QStringLiteral("answers"), what);
        emit Answered(false, what);
    });
}

QJsonObject OcrClient::WithGame(const QJsonObject& games, const QString& game, const QRectF& area, const QString& source,
                                const QList<Lines::Box>& more) {
    const QString area_id = QStringLiteral("furoshiki");
    const QJsonObject mine{{QStringLiteral("id"), area_id},
                           {QStringLiteral("name"), QStringLiteral("Furoshiki")},
                           {QStringLiteral("rect"), QJsonArray{area.x(), area.y(), area.width(), area.height()}}};
    // Its other boxes, each read with the first or only when asked for (the helper's "read").
    QJsonArray others;
    for (int i = 0; i < more.size(); ++i) {
        const QRectF& box = more[i].rect;
        others.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("%1-%2").arg(area_id).arg(i + 1)},
                                  {QStringLiteral("name"), more[i].ask ? QStringLiteral("Furoshiki, on request") : QStringLiteral("Furoshiki, also")},
                                  {QStringLiteral("rect"), QJsonArray{box.x(), box.y(), box.width(), box.height()}},
                                  {QStringLiteral("read"), more[i].ask ? QStringLiteral("ask") : QStringLiteral("always")}});
    }
    QJsonArray list = games.value(QStringLiteral("games")).toArray();
    QString id;
    QStringList ids;
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject known = list[i].toObject();
        ids.append(known.value(QStringLiteral("id")).toString());
        if (known.value(QStringLiteral("name")).toString() != game || !id.isEmpty()) {
            continue;
        }
        id = ids.last();
        QJsonArray areas;
        for (const QJsonValue& other : known.value(QStringLiteral("areas")).toArray()) {
            if (const QString known_id = other.toObject().value(QStringLiteral("id")).toString();
                known_id != area_id && !known_id.startsWith(area_id + QLatin1Char('-'))) {
                areas.append(other);
            }
        }
        areas.append(mine);
        for (const QJsonValue& other : others) {
            areas.append(other);
        }
        known.insert(QStringLiteral("areas"), areas);
        known.insert(QStringLiteral("active_area"), area_id);
        known.insert(QStringLiteral("source"), source);
        list[i] = known;
    }
    if (id.isEmpty()) {
        // An ID no game there has, made from the name so that it is the same next time.
        id = QStringLiteral("f%1").arg(qHash(game, 0x46524f53u) & 0xffffffffu, 8, 16, QLatin1Char('0'));
        while (ids.contains(id)) {
            id += QLatin1Char('x');
        }
        QJsonArray areas{mine};
        for (const QJsonValue& other : others) {
            areas.append(other);
        }
        list.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("name"), game}, {QStringLiteral("page"), QString()},
                                {QStringLiteral("source"), source}, {QStringLiteral("areas"), areas},
                                {QStringLiteral("active_area"), area_id}});
    }
    return QJsonObject{{QStringLiteral("active"), id}, {QStringLiteral("games"), list}};
}

void OcrClient::ReadAsked(std::function<void(bool ok, const QString& what)> then) {
    Json(QStringLiteral("POST"), QStringLiteral("/api/ocr/now"), QJsonObject{{QStringLiteral("ask"), true}}, 20000,
         [then](bool ok, const QJsonObject& answer, const QString& error) {
             if (then) {
                 const bool added = ok && answer.value(QStringLiteral("added")).toBool();
                 then(added, !ok ? error : added ? QString() : answer.value(QStringLiteral("reason")).toString());
             }
         });
}

void OcrClient::Choose(const QString& game, const QRectF& area, const QList<Lines::Box>& more) {
    Wake([this, game, area, more](bool awake, const QString& why) {
        if (!awake) {
            Say(QStringLiteral("no answer"), why);
            emit Chosen(false, why);
            return;
        }
        ChooseNow(game, area, more);
    });
}

void OcrClient::ChooseNow(const QString& game, const QRectF& area, const QList<Lines::Box>& more) {
    // The engine this device chose reads there (none chosen: the helper's own stays).
    if (!helper.engine.isEmpty()) {
        Json(QStringLiteral("PUT"), QStringLiteral("/api/settings"), QJsonObject{{QStringLiteral("engine"), helper.engine}}, 8000,
             [](bool, const QJsonObject&, const QString&) {});
    }
    Json(QStringLiteral("PUT"), QStringLiteral("/api/source"), QJsonObject{{QStringLiteral("source"), helper.source}}, 8000,
         [this, game, area, more](bool ok, const QJsonObject&, const QString& error) {
             if (!ok) {
                 Say(QStringLiteral("no answer"), error);
                 emit Chosen(false, error);
                 return;
             }
             if (game.isEmpty()) {
                 Say(QStringLiteral("answers"), QString());
                 emit Chosen(true, QString());
                 return;
             }
             Json(QStringLiteral("GET"), QStringLiteral("/api/regions"), {}, 8000,
                  [this, game, area, more](bool got, const QJsonObject& games, const QString& why) {
                      if (!got) {
                          Say(QStringLiteral("no answer"), why);
                          emit Chosen(false, why);
                          return;
                      }
                      Json(QStringLiteral("PUT"), QStringLiteral("/api/regions"), WithGame(games, game, area, helper.source, more), 8000,
                           [this](bool put, const QJsonObject&, const QString& problem) {
                               Say(put ? QStringLiteral("reading") : QStringLiteral("no answer"), problem);
                               emit Chosen(put, problem);
                           });
                  });
         });
}

bool OcrClient::Push(const QImage& picture) {
    if (!Configured() || picture.isNull() || pushing || waking) {
        return false;
    }
    QImage small = picture;
    if (std::max(small.width(), small.height()) > LongestSide) {
        small = small.scaled(LongestSide, LongestSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray jpeg;
    QBuffer out(&jpeg);
    out.open(QIODevice::WriteOnly);
    if (!small.save(&out, "JPEG", 88)) {
        return false;
    }
    QNetworkRequest request = Request(QStringLiteral("/api/feed/") + QString::fromLatin1(QUrl::toPercentEncoding(helper.source)), 8000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
    pushing = true;
    ++sent;
    QNetworkReply* reply = net->post(request, jpeg);
    const int mine = generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, mine] {
        reply->deleteLater();
        if (mine != generation) {
            return;
        }
        pushing = false;
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 300) {
            Say(status == 403 ? QStringLiteral("refused") : QStringLiteral("no answer"), ErrorOf(reply, body));
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(body).object();
        if (answer.value(QStringLiteral("taken")).toBool()) {
            ++taken;
            Say(QStringLiteral("reading"), QString());
        } else if (!answer.value(QStringLiteral("in_use")).toBool()) {
            // Another device chose another picture there: the helper reads one at a time.
            Say(QStringLiteral("waiting"), QStringLiteral("the helper reads another picture source now"));
        }
    });
    return true;
}

void OcrClient::Test(const QRectF& area) {
    Json(QStringLiteral("POST"), QStringLiteral("/api/ocr/test"),
         helper.engine.isEmpty()
             ? QJsonObject{{QStringLiteral("rect"), QJsonArray{area.x(), area.y(), area.width(), area.height()}}, {QStringLiteral("source"), helper.source}}
             : QJsonObject{{QStringLiteral("rect"), QJsonArray{area.x(), area.y(), area.width(), area.height()}}, {QStringLiteral("source"), helper.source},
                           {QStringLiteral("engine"), helper.engine}},
         20000, [this](bool ok, const QJsonObject& answer, const QString& error) {
             emit Tested(ok, ok ? answer.value(QStringLiteral("text")).toString() : error);
             QList<QRectF> layout;
             for (const QJsonValue& value : answer.value(QStringLiteral("layout")).toArray()) {
                 const QJsonArray box = value.toArray();
                 layout.append(box.size() == 4 ? QRectF(box[0].toDouble(), box[1].toDouble(), box[2].toDouble(), box[3].toDouble()) : QRectF());
             }
             emit Placed(ok, ok ? answer.value(QStringLiteral("text")).toString() : error, layout);
         });
}

OcrClient::Read OcrClient::ReadOf(const QJsonObject& line) {
    Read read;
    read.id = line.value(QStringLiteral("id")).toInteger();
    read.text = line.value(QStringLiteral("text")).toString();
    for (const QJsonValue& value : line.value(QStringLiteral("boxes")).toArray()) {
        const QJsonArray box = value.toArray();
        read.boxes.append(box.size() == 4 ? QRectF(box[0].toDouble(), box[1].toDouble(), box[2].toDouble(), box[3].toDouble()) : QRectF());
    }
    return read;
}

void OcrClient::Follow() {
    if (following || !Configured()) {
        return;
    }
    following = true;
    since = -1;
    screen_seq = -1;
    AskLines();
    AskScreen();
}

void OcrClient::Unfollow() {
    following = false;
    ++generation; // what is still on its way is no longer wanted
    pushing = false;
}

void OcrClient::AskLines() {
    if (!following) {
        return;
    }
    const QString source = QString::fromLatin1(QUrl::toPercentEncoding(helper.source));
    // At first only how far the lines there go: what was read before is not this reading's.
    const QString path = since < 0 ? QStringLiteral("/api/lines?since=0&limit=1&source=%1").arg(source)
                                   : QStringLiteral("/api/lines?since=%1&wait=%2&source=%3").arg(since).arg(since > 0 ? 20 : 0).arg(source);
    const bool first = since < 0;
    Json(QStringLiteral("GET"), path, {}, 30000, [this, first](bool ok, const QJsonObject& answer, const QString& error) {
        if (!ok) {
            Say(QStringLiteral("no answer"), error);
            QTimer::singleShot(RetryMs, this, [this] { AskLines(); });
            return;
        }
        const qint64 before = since;
        since = std::max<qint64>(std::max<qint64>(since, 0), answer.value(QStringLiteral("upto")).toInteger());
        if (!first) {
            QList<qint64> replaced;
            for (const QJsonValue& id : answer.value(QStringLiteral("replaced")).toArray()) {
                replaced.append(id.toInteger());
            }
            for (const QJsonValue& value : answer.value(QStringLiteral("lines")).toArray()) {
                const Read read = ReadOf(value.toObject());
                if (read.id > before && !read.text.isEmpty()) {
                    emit Line(read, replaced);
                }
            }
        }
        // A helper that has no line at all yet cannot wait for one: asked again in a moment.
        QTimer::singleShot(since > 0 ? 0 : 700, this, [this] { AskLines(); });
    });
}

void OcrClient::AskScreen() {
    if (!following) {
        return;
    }
    const QString source = QString::fromLatin1(QUrl::toPercentEncoding(helper.source));
    const QString path = screen_seq < 0 ? QStringLiteral("/api/overlay?source=%1").arg(source)
                                        : QStringLiteral("/api/overlay?source=%1&after=%2&wait=20").arg(source).arg(screen_seq);
    Json(QStringLiteral("GET"), path, {}, 30000, [this](bool ok, const QJsonObject& answer, const QString&) {
        if (!ok) {
            QTimer::singleShot(RetryMs, this, [this] { AskScreen(); });
            return;
        }
        screen_seq = answer.value(QStringLiteral("seq")).toInteger();
        const QJsonObject line = answer.value(QStringLiteral("line")).toObject();
        const bool ours = answer.value(QStringLiteral("source")).toString() == helper.source && !line.isEmpty() &&
                          line.value(QStringLiteral("source")).toString() == helper.source;
        emit OnScreen(ours ? line.value(QStringLiteral("id")).toInteger() : -1);
        QTimer::singleShot(0, this, [this] { AskScreen(); });
    });
}

} // namespace Home
