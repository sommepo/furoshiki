// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ws_client.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtEndian>

namespace Home {

namespace {
constexpr char Guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
enum : quint8 { Continuation = 0, TextFrame = 1, BinaryFrame = 2, CloseFrame = 8, PingFrame = 9, PongFrame = 10 };
} // namespace

WsClient::WsClient(QObject* parent) : QObject(parent) {
    retry.setSingleShot(true);
    connect(&retry, &QTimer::timeout, this, &WsClient::Connect);
    connect(&socket, &QTcpSocket::connected, this, [this] {
        // The opening handshake (RFC 6455, 4.1).
        QByteArray random(16, Qt::Uninitialized);
        for (char& byte : random) {
            byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
        key = random.toBase64();
        QString path = url.path(QUrl::FullyEncoded);
        if (path.isEmpty()) {
            path = QStringLiteral("/");
        }
        if (url.hasQuery()) {
            path += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
        }
        const QString host = url.port(-1) > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
        socket.write("GET " + path.toUtf8() + " HTTP/1.1\r\nHost: " + host.toUtf8() +
                     "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
                     "\r\nSec-WebSocket-Version: 13\r\n\r\n");
    });
    connect(&socket, &QTcpSocket::readyRead, this, &WsClient::Read);
    connect(&socket, &QTcpSocket::disconnected, this, [this] { Dropped(QStringLiteral("the other side closed")); });
    connect(&socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { Dropped(socket.errorString()); });
}

bool WsClient::Open(const QString& address) {
    const QUrl parsed(address.trimmed());
    if (!parsed.isValid() || parsed.scheme() != QStringLiteral("ws") || parsed.host().isEmpty()) {
        return false;
    }
    Close();
    url = parsed;
    wanted = true;
    Connect();
    return true;
}

void WsClient::Close() {
    wanted = false;
    retry.stop();
    if (upgraded) {
        Send(CloseFrame, QByteArray());
        socket.flush();
    }
    upgraded = false;
    socket.abort();
    buffer.clear();
    message.clear();
    emit StatusChanged();
}

QString WsClient::Status() const {
    const QString where = url.toString();
    if (upgraded) {
        return QStringLiteral("connected  ·  %1").arg(where);
    }
    if (!wanted) {
        return QStringLiteral("not connected");
    }
    return last_error.isEmpty() ? QStringLiteral("connecting  ·  %1").arg(where)
                                : QStringLiteral("waiting  ·  %1  ·  %2").arg(where, last_error);
}

void WsClient::Connect() {
    if (!wanted || socket.state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    buffer.clear();
    message.clear();
    in_text = false;
    upgraded = false;
    socket.connectToHost(url.host(), static_cast<quint16>(url.port(80)));
}

void WsClient::Dropped(const QString& why) {
    const bool was = upgraded;
    upgraded = false;
    if (!wanted || dropping) {
        return;
    }
    dropping = true; // letting the socket go says "disconnected" again
    if (last_error != why || was) {
        last_error = why;
        emit StatusChanged();
    }
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.abort();
    }
    dropping = false;
    retry.start(retry_ms);
}

void WsClient::Read() {
    buffer.append(socket.readAll());
    if (!upgraded && !ReadHandshake()) {
        return;
    }
    while (upgraded && ReadFrame()) {
    }
}

bool WsClient::ReadHandshake() {
    const qsizetype end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buffer.size() > 16384) {
            Dropped(QStringLiteral("no WebSocket there"));
        }
        return false;
    }
    const QList<QByteArray> lines = buffer.left(end).split('\n');
    buffer.remove(0, end + 4);
    const QByteArray expected =
        QCryptographicHash::hash(key + Guid, QCryptographicHash::Sha1).toBase64();
    bool accepted = false;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines[i].indexOf(':');
        if (colon > 0 && lines[i].left(colon).trimmed().toLower() == "sec-websocket-accept") {
            accepted = lines[i].mid(colon + 1).trimmed() == expected;
        }
    }
    if (!lines.value(0).contains(" 101") || !accepted) {
        Dropped(QStringLiteral("no WebSocket there"));
        return false;
    }
    upgraded = true;
    last_error.clear();
    emit StatusChanged();
    return true;
}

bool WsClient::ReadFrame() {
    if (buffer.size() < 2) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const uchar*>(buffer.constData());
    const bool fin = (bytes[0] & 0x80) != 0;
    const quint8 opcode = bytes[0] & 0x0f;
    const bool masked = (bytes[1] & 0x80) != 0;
    quint64 length = bytes[1] & 0x7f;
    qsizetype head = 2;
    if (length == 126) {
        if (buffer.size() < 4) {
            return false;
        }
        length = qFromBigEndian<quint16>(bytes + 2);
        head = 4;
    } else if (length == 127) {
        if (buffer.size() < 10) {
            return false;
        }
        length = qFromBigEndian<quint64>(bytes + 2);
        head = 10;
    }
    if (length > static_cast<quint64>(MaxMessage)) {
        Dropped(QStringLiteral("a message too large"));
        return false;
    }
    const qsizetype mask_at = head;
    head += masked ? 4 : 0;
    if (buffer.size() < head + static_cast<qsizetype>(length)) {
        return false;
    }
    QByteArray payload = buffer.mid(head, static_cast<qsizetype>(length));
    if (masked) {
        // A server does not mask; one that does is read all the same.
        for (qsizetype i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(payload[i] ^ buffer[mask_at + (i & 3)]);
        }
    }
    buffer.remove(0, head + static_cast<qsizetype>(length));
    switch (opcode) {
    case TextFrame:
    case BinaryFrame:
        in_text = opcode == TextFrame;
        message = payload;
        break;
    case Continuation:
        if (message.size() + payload.size() > MaxMessage) {
            Dropped(QStringLiteral("a message too large"));
            return false;
        }
        message.append(payload);
        break;
    case PingFrame:
        Send(PongFrame, payload);
        return true;
    case CloseFrame:
        Send(CloseFrame, payload.left(2));
        socket.flush();
        Dropped(QStringLiteral("the other side closed"));
        return false;
    default: // a pong, or something this does not know
        return true;
    }
    if (fin) {
        if (in_text) {
            emit Text(QString::fromUtf8(message));
        }
        message.clear();
    }
    return true;
}

void WsClient::Send(quint8 opcode, const QByteArray& payload) {
    // Only control frames go out: at most 125 bytes, masked as a client's must be.
    const QByteArray body = payload.left(125);
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    frame.append(static_cast<char>(0x80 | body.size()));
    char mask[4];
    for (char& byte : mask) {
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    frame.append(mask, 4);
    for (qsizetype i = 0; i < body.size(); ++i) {
        frame.append(static_cast<char>(body[i] ^ mask[i & 3]));
    }
    socket.write(frame);
}

} // namespace Home
