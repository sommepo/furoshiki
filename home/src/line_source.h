// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A reading source that is no core (docs/furoshiki-3.md, "Sources"): lines another program
// sends over its WebSocket (a texthooker's, such as ws://localhost:6677), or Japanese text
// copied to the clipboard. Each line goes the way a core's does (ReaderUi::ReadingSource): into
// the reader's session, and from there to the statistics, the Log, the text server, the popup's
// lookups and cards. What arrives is made lines by Lines::Feed (line_feed.h).

#include <QTimer>
#include "line_feed.h"
#include "reader/qt/reader_session.h"
#include "ws_client.h"

namespace Home {

class LineSource : public ReaderUi::ReadingSource {
    Q_OBJECT

public:
    explicit LineSource(QObject* parent = nullptr);

    [[nodiscard]] About Describe() const override;

    /// Begins reading the title `id` by `by` ("websocket", from `address`, or "clipboard").
    /// False, with `error`, when that cannot be (no such way, no such address).
    bool Start(const QString& by, const QString& id, const QString& title, const QString& address, QString& error);
    void Stop();
    [[nodiscard]] bool Running() const {
        return running;
    }
    [[nodiscard]] const QString& By() const {
        return by;
    }
    [[nodiscard]] const QString& GameId() const {
        return id;
    }
    [[nodiscard]] const QString& Title() const {
        return title;
    }
    /// For the user: where the lines come from and whether they can come now.
    [[nodiscard]] QString Status() const;
    /// Text as if it had arrived (a test's, the development channel's).
    void Arrived(const QString& raw);

signals:
    void StatusChanged();

private:
    void HandOver(const Lines::Feed::Out& out);
    void ClipboardChanged();

    WsClient client;
    Lines::Feed feed;
    QTimer settle;
    QTimer clipboard_poll;
    QString clipboard_seen;
    QString by, id, title;
    bool running{};
    qint64 sequence{};
};

} // namespace Home
