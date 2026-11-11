// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "line_source.h"

#include <QClipboard>
#include <QDateTime>
#include "reader/core/audio_tap.h"
#include <QGuiApplication>

namespace Home {

namespace {
ReaderApp::TextLine LineOf(const QString& text, qint64 sequence) {
    ReaderApp::TextLine line;
    line.sequence = sequence;
    // Where it is in the computer's sound, as cards cut it (loopback_audio.h), from when it came.
    line.audio_position = Reader::AudioTap::Instance().IsEnabled() ? std::max<qint64>(0, static_cast<qint64>(Reader::AudioTap::Instance().Position()) - static_cast<qint64>(Lines::Feed::SettleMs) * Reader::AudioTap::SampleRate / 1000) : -1;
    line.channel = ReaderApp::ChannelDialogue();
    line.text = text;
    line.source_id = Lines::SourceId(text);
    return line;
}
} // namespace

LineSource::LineSource(QObject* parent) : ReaderUi::ReadingSource(parent) {
    settle.setSingleShot(true);
    settle.setInterval(static_cast<int>(Lines::Feed::SettleMs));
    connect(&settle, &QTimer::timeout, this, [this] {
        if (const auto out = feed.Settle(QDateTime::currentMSecsSinceEpoch())) {
            HandOver(*out);
        }
    });
    connect(&client, &WsClient::Text, this, [this](const QString& frame) { Arrived(Lines::MessageText(frame)); });
    connect(&client, &WsClient::StatusChanged, this, &LineSource::StatusChanged);
    // The clipboard says when it changes; where a system does not say so to a program that is
    // not in front, looking now and then finds it.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &LineSource::ClipboardChanged);
    clipboard_poll.setInterval(400);
    connect(&clipboard_poll, &QTimer::timeout, this, &LineSource::ClipboardChanged);
}

ReaderUi::ReadingSource::About LineSource::Describe() const {
    return {by, by == QStringLiteral("clipboard") ? QStringLiteral("Clipboard") : QStringLiteral("WebSocket"), false};
}

bool LineSource::Start(const QString& by_, const QString& id_, const QString& title_, const QString& address, QString& error) {
    Stop();
    if (by_ == QStringLiteral("websocket")) {
        if (!client.Open(address)) {
            error = QStringLiteral("\"%1\" is no WebSocket address (ws://host:port)").arg(address);
            return false;
        }
    } else if (by_ == QStringLiteral("clipboard")) {
        // What was copied before is not this title's.
        clipboard_seen = QGuiApplication::clipboard()->text();
        clipboard_poll.start();
    } else {
        error = QStringLiteral("Lines cannot be read by \"%1\"").arg(by_);
        return false;
    }
    by = by_;
    id = id_;
    title = title_;
    running = true;
    feed.Reset();
    emit Began(id, title);
    emit StatusChanged();
    return true;
}

void LineSource::Stop() {
    if (!running) {
        return;
    }
    // The line that waited for more of itself is a line.
    settle.stop();
    if (const auto out = feed.Settle(QDateTime::currentMSecsSinceEpoch())) {
        HandOver(*out);
    }
    running = false;
    client.Close();
    clipboard_poll.stop();
    emit Ended();
    emit StatusChanged();
}

QString LineSource::Status() const {
    if (!running) {
        return {};
    }
    return by == QStringLiteral("clipboard") ? QStringLiteral("what is copied") : client.Status();
}

void LineSource::Arrived(const QString& raw) {
    if (!running) {
        return;
    }
    for (const Lines::Feed::Out& out : feed.Take(raw, QDateTime::currentMSecsSinceEpoch())) {
        HandOver(out);
    }
    if (feed.HasPending()) {
        settle.start();
    }
}

void LineSource::HandOver(const Lines::Feed::Out& out) {
    if (out.takes_back) {
        // It was the beginning of this line: not read twice.
        ReaderUi::ReaderSession::Instance().TakeBack(LineOf(*out.takes_back, 0));
    }
    emit Line(LineOf(out.text, ++sequence));
}

void LineSource::ClipboardChanged() {
    if (!running || by != QStringLiteral("clipboard")) {
        return;
    }
    const QString text = QGuiApplication::clipboard()->text();
    if (text == clipboard_seen) {
        return;
    }
    clipboard_seen = text;
    Arrived(text);
}

} // namespace Home
