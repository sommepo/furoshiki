// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ocr_source.h"

#include "line_feed.h"

#include "reader/core/audio_tap.h"

namespace Home {

namespace {
ReaderApp::TextLine LineOf(const QString& text, qint64 sequence) {
    ReaderApp::TextLine line;
    line.sequence = sequence;
    // Where it is in the computer's sound, as cards cut it (loopback_audio.h), from when it came.
    line.audio_position = Reader::AudioTap::Instance().IsEnabled() ? std::max<qint64>(0, static_cast<qint64>(Reader::AudioTap::Instance().Position()) - 0) : -1;
    line.channel = ReaderApp::ChannelDialogue();
    line.text = text;
    line.source_id = Lines::SourceId(text);
    return line;
}
} // namespace

OcrSource::OcrSource(QObject* parent) : ReaderUi::ReadingSource(parent) {
    tick.setInterval(250); // four pictures a second, as every feeder of the helper sends
    connect(&tick, &QTimer::timeout, this, &OcrSource::Tick);
    connect(&client, &OcrClient::StatusChanged, this, &OcrSource::StatusChanged);
    connect(&client, &OcrClient::Chosen, this, [this](bool ok, const QString&) {
        if (running && ok) {
            client.Follow();
        }
    });
    connect(&client, &OcrClient::Line, this, [this](const OcrClient::Read& read, const QList<qint64>& replaced) {
        if (!running) {
            return;
        }
        // A line the game was still writing when it was read: the whole one comes now.
        for (const qint64 id : replaced) {
            const auto before = handed.constFind(id);
            if (before != handed.constEnd()) {
                ReaderUi::ReaderSession::Instance().TakeBack(LineOf(*before, 0));
                handed.erase(before);
            }
        }
        handed.insert(read.id, read.text);
        last_id = read.id;
        boxes = read.boxes;
        on_screen = true;
        emit Line(LineOf(read.text, ++sequence));
        emit ScreenChanged();
    });
    connect(&client, &OcrClient::OnScreen, this, [this](qint64 id) {
        const bool now = id >= 0 && id == last_id;
        if (running && now != on_screen) {
            on_screen = now;
            emit ScreenChanged();
        }
    });
}

ReaderUi::ReadingSource::About OcrSource::Describe() const {
    // No game runs in the program's own window: the lines are all there is of it here.
    return {QStringLiteral("ocr"), QStringLiteral("OCR"), false};
}

bool OcrSource::Start(const Lines::Title& title_, const Lines::Helper& helper, QString& error) {
    Stop();
    if (Lines::CleanHelperAddress(helper.address).isEmpty()) {
        error = QStringLiteral("No OCR helper's address is set: Settings, OCR helper");
        return false;
    }
    if (title_.window.isEmpty() || !title_.area.isValid()) {
        error = QStringLiteral("%1 names no window and area to read").arg(title_.title);
        return false;
    }
    title = title_;
    window = Windows::Find(title.window);
    window_there = !window.title.isEmpty();
    running = true;
    sequence = 0;
    handed.clear();
    last_id = -1;
    boxes.clear();
    on_screen = false;
    place = QRect();
    client.Set(helper);
    emit Began(title.id, title.title);
    // This device's pictures are the ones read there, the title the game played, its area the
    // one in use; then its lines are followed (Chosen).
    client.Choose(title.title, title.area);
    tick.start();
    emit StatusChanged();
    return true;
}

void OcrSource::Stop() {
    if (!running) {
        return;
    }
    running = false;
    tick.stop();
    client.Unfollow();
    on_screen = false;
    emit ScreenChanged();
    emit Ended();
    emit StatusChanged();
}

QString OcrSource::Status() const {
    if (!running) {
        return {};
    }
    return window_there ? client.Status() : QStringLiteral("the window \"%1\" is not open").arg(title.window);
}

void OcrSource::Tick() {
    // The same window while it is there; one of its title when it was opened again.
    window = Windows::Find(title.window, window.handle);
    const bool there = !window.title.isEmpty();
    if (there != window_there) {
        window_there = there;
        emit StatusChanged();
    }
    if (!there) {
        return;
    }
    const QRect now = Windows::Place(window);
    if (now != place) {
        place = now;
        emit ScreenChanged();
    }
    const QImage picture = Windows::Grab(window);
    if (!picture.isNull()) {
        client.Push(picture);
    }
}

} // namespace Home
