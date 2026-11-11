// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A WebSocket client for the lines another program sends (a texthooker's server, such as
// ws://localhost:6677): text frames in, nothing out but the handshake, pongs and the closing
// frame. Plain ws:// only. It goes on trying while it is open: the other program may be started
// later, or again. (Qt's own WebSockets module is not in the Qt the program is built with.)

#include <QByteArray>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace Home {

class WsClient : public QObject {
    Q_OBJECT

public:
    explicit WsClient(QObject* parent = nullptr);

    /// Connects to `address` (ws://host:port/path) and keeps trying. False when it is no such address.
    bool Open(const QString& address);
    void Close();
    [[nodiscard]] bool Connected() const {
        return upgraded;
    }
    /// For the user: "connected · ws://…", "waiting · ws://… · connection refused".
    [[nodiscard]] QString Status() const;
    /// How long after a failure it tries again.
    int retry_ms{2000};

    /// A frame's largest payload; a server that sends more is let go.
    static constexpr qsizetype MaxMessage = 1 << 20;

signals:
    void Text(const QString& text);
    void StatusChanged();

private:
    void Connect();
    void Dropped(const QString& why);
    void Read();
    bool ReadHandshake();
    /// One frame out of the buffer; false when it does not hold a whole one yet.
    bool ReadFrame();
    void Send(quint8 opcode, const QByteArray& payload);

    QTcpSocket socket;
    QTimer retry;
    QUrl url;
    bool wanted{};
    bool upgraded{};
    bool dropping{};
    QByteArray key;
    QByteArray buffer;
    QByteArray message; ///< A text message's frames so far
    bool in_text{};
    QString last_error;
};

} // namespace Home
