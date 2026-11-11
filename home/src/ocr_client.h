// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The OCR helper's client (docs/furoshiki-3.md, "A window read by OCR"). The helper is
// Furochibi's service (../furochibi/server/console_ocr), reached by its own API at an address
// that is a setting of this device (line_titles.h, Lines::Helper): a copy running beside the
// program (started from here when it is installed on this computer and does not answer:
// ocr_helper.h), or the one on a home server. This device feeds one of its pushed picture sources,
// with that source's token:
//
//   PUT  /api/source                 this device's source is the one in use
//   GET, PUT /api/regions            the title is a game there, its text area the one drawn here
//   POST /api/feed/<source>          a picture of the window (a JPEG), about four a second
//   POST /api/ocr/test               the wizard's test read of an area
//   GET  /api/lines?source=&since=&wait=   the lines read from this device's pictures, no one else's
//   GET  /api/overlay?source=&after=&wait= the line on the screen now (its characters' places come
//                                    with the line)
//
// Nothing is read here: the stabiliser, the engines, the cleaner and the furigana filter are
// the service's. Qt Network only (http, and https where Qt has its TLS plugin).

#include <functional>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>
#include "line_titles.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace Home {

class OcrClient : public QObject {
    Q_OBJECT

public:
    /// A line the helper read from this device's pictures.
    struct Read {
        qint64 id{};
        QString text;
        /// Each character's place (one a code point of the text), 0-1 of the picture; an empty
        /// rectangle where it has none.
        QList<QRectF> boxes;
    };

    explicit OcrClient(QObject* parent = nullptr);

    void Set(const Lines::Helper& helper);
    [[nodiscard]] bool Configured() const {
        return !helper.address.isEmpty();
    }
    [[nodiscard]] const Lines::Helper& Helper() const {
        return helper;
    }

    /// Asks the helper whether it is there and has this device's picture source: Answered.
    void Ask();
    /// Makes this device's source the one in use there: Chosen. With a game's name, that game is
    /// the one played there and `area` its text area.
    void Choose(const QString& game = QString(), const QRectF& area = QRectF());
    /// A picture of the window. False when the one before is still on its way: that one counts.
    bool Push(const QImage& picture);
    /// Reads `area` of the picture sent last, keeping nothing: Tested.
    void Test(const QRectF& area);
    /// Takes the lines read from this device's pictures from now on (Line), and which of them
    /// is on the screen (OnScreen), until Unfollow.
    void Follow();
    void Unfollow();
    /// For the user: "reading · http://…", "no answer · http://… · connection refused".
    [[nodiscard]] QString Status() const;
    /// Pictures sent and taken since Set (a test's figures).
    [[nodiscard]] int Sent() const {
        return sent;
    }
    [[nodiscard]] int Taken() const {
        return taken;
    }

    /// A line as the service's JSON tells it ("id", "text", "boxes": [[x, y, w, h], ...]).
    [[nodiscard]] static Read ReadOf(const QJsonObject& line);
    /// The games document with `game` in it as the game played, `area` its text area in use,
    /// played from `source`; every other game and area as it was.
    [[nodiscard]] static QJsonObject WithGame(const QJsonObject& games, const QString& game, const QRectF& area, const QString& source);

signals:
    void Answered(bool ok, const QString& what);
    void Chosen(bool ok, const QString& what);
    void Tested(bool ok, const QString& text);
    /// The same reading, with each character's place in the whole picture (one a code point of
    /// `text`; empty where the helper gave none).
    void Placed(bool ok, const QString& text, const QList<QRectF>& layout);
    /// A line, and the IDs of earlier ones it and later readings replaced (lines the game was
    /// still writing when they were read).
    void Line(const Home::OcrClient::Read& read, const QList<qint64>& replaced);
    /// The line on the screen now; -1: none.
    void OnScreen(qint64 id);
    void StatusChanged();

private:
    QNetworkRequest Request(const QString& path, int timeout_ms) const;
    void Json(const QString& verb, const QString& path, const QJsonObject& body, int timeout_ms,
              std::function<void(bool ok, const QJsonObject& answer, const QString& error)> then);
    /// A helper installed on this computer is started first when nothing answers (ocr_helper.h).
    void Wake(std::function<void(bool ok, const QString& what)> then);
    void AskSource();
    void ChooseNow(const QString& game, const QRectF& area);
    void AskLines();
    void AskScreen();
    void Say(const QString& state, const QString& error);

    Lines::Helper helper;
    QNetworkAccessManager* net{};
    bool pushing{};
    bool waking{}; ///< The helper is asked for, perhaps started: no picture goes meanwhile
    bool following{};
    int generation{}; ///< Set and Unfollow make what is still on its way another's
    qint64 since{-1}; ///< The newest line looked at; -1: not asked yet
    qint64 screen_seq{-1};
    int sent{}, taken{};
    QString state, last_error;
};

} // namespace Home
