// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core_ocr.h"

#include <QDateTime>
#include <QImage>
#include "line_feed.h"
#include "reader/core/audio_tap.h"
#include "reader/desktop/screen_text.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_session.h"

namespace Home {

namespace {
/// The window the lines' places are said for (ReaderApp::ScreenLayout): one text box a game.
const QString Window = QStringLiteral("ocr");
/// The units a line's place is said in, each screen this many across and down.
constexpr float Space = 1000.0f;

ReaderApp::TextLine LineOf(const QString& text, qint64 sequence, qint64 audio) {
    ReaderApp::TextLine line;
    line.sequence = sequence;
    line.audio_position = audio;
    line.channel = ReaderApp::ChannelDialogue();
    line.text = text;
    line.source_id = Lines::SourceId(text);
    line.window = Window;
    return line;
}

qint64 AudioNow() {
    return Reader::AudioTap::Instance().IsEnabled() ? static_cast<qint64>(Reader::AudioTap::Instance().Position()) : -1;
}
} // namespace

CoreOcr::CoreOcr(QObject* parent) : QObject(parent) {
    tick.setInterval(250); // four looks a second, as every feeder of the helper has it
    connect(&tick, &QTimer::timeout, this, &CoreOcr::Tick);
    connect(&client, &OcrClient::StatusChanged, this, &CoreOcr::StatusChanged);
    connect(&client, &OcrClient::Chosen, this, [this](bool ok, const QString&) {
        if (running && ok) {
            client.Follow();
        }
    });
    connect(&client, &OcrClient::Line, this, [this](const OcrClient::Read& read, const QList<qint64>& replaced) {
        if (!running) {
            return;
        }
        auto& session = ReaderUi::ReaderSession::Instance();
        // A line the game was still writing when it was read: the whole one comes now.
        for (const qint64 id : replaced) {
            const auto before = handed.constFind(id);
            if (before != handed.constEnd()) {
                session.TakeBack(LineOf(*before, 0, -1));
                handed.erase(before);
            }
        }
        handed.insert(read.id, read.text);
        last_id = read.id;
        boxes = read.boxes;
        on_screen = true;
        ++count;
        // Its sound begins where the boxes began to change: the reading came a moment later.
        session.PostLine(LineOf(read.text, ++sequence, audio_began >= 0 ? audio_began : AudioNow()));
        Place();
    });
    // Whether the line stands on the screen is known here, not asked of the helper: no picture
    // goes there while the boxes stand still, so it would think the screen gone. The line read
    // last stays until the boxes change again (Took).
}

bool CoreOcr::Start(const Lines::CoreGame& game_, const Lines::Helper& helper, QString& error) {
    Stop();
    if (Lines::CleanHelperAddress(helper.address).isEmpty()) {
        error = QStringLiteral("OCR isn't set up yet. Choose where it runs in Settings, Library");
        return false;
    }
    if (!game_.area.isValid()) {
        error = QStringLiteral("%1 has no text area yet. Press Esc and choose \"Set text area…\"").arg(game_.title);
        return false;
    }
    game = game_;
    watched = {game.area};
    for (const Lines::Box& box : game.more) {
        if (!box.ask) {
            watched.append(box.rect);
        }
    }
    running = true;
    grabbing = false;
    ++generation;
    sequence = 0;
    began_seen = audio_began = -1;
    handed.clear();
    last_id = -1;
    boxes.clear();
    on_screen = placed = false;
    count = 0;
    gate.Reset();
    client.Set(helper);
    // This device's pictures are the ones read there, the game the one played, its area the
    // one in use; then its lines are followed (Chosen).
    client.Choose(game.title.isEmpty() ? game.id : game.title, game.area, game.more);
    tick.start();
    emit StatusChanged();
    return true;
}

void CoreOcr::Stop() {
    if (!running) {
        return;
    }
    running = false;
    ++generation;
    tick.stop();
    client.Unfollow();
    if (placed) {
        on_screen = false;
        Place();
    }
    emit StatusChanged();
}

void CoreOcr::Tick() {
    if (grabbing) {
        return; // the core has not handed the last one over yet (it stands still, say)
    }
    grabbing = true;
    const int mine = generation;
    ReaderUi::GrabPicture(1920, [this, mine](const QImage& picture) {
        if (mine == generation) {
            grabbing = false;
            Took(picture, false);
        }
    });
}

void CoreOcr::Took(const QImage& picture, bool always) {
    if (!running || picture.isNull()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool want = gate.Want(OcrGate::Signature(picture, watched), now);
    if (gate.BeganAt() != began_seen) {
        // Something new comes into the boxes: the line read last is no longer what stands there.
        began_seen = gate.BeganAt();
        audio_began = AudioNow();
        if (on_screen) {
            on_screen = false;
            Place();
        }
    }
    if ((want || always) && !client.Push(picture) && want) {
        gate.Missed();
    }
}

void CoreOcr::ReadAsked(std::function<void(bool ok, const QString& what)> then) {
    if (!running) {
        return;
    }
    // The picture as it is now first: a box read only on request is not looked at otherwise.
    const int mine = generation;
    ReaderUi::GrabPicture(1920, [this, mine, then = std::move(then)](const QImage& picture) {
        if (mine != generation) {
            return;
        }
        Took(picture, true);
        QTimer::singleShot(350, this, [this, mine, then] {
            if (mine == generation) {
                client.ReadAsked(then);
            }
        });
    });
}

void CoreOcr::Place() {
    auto& session = ReaderUi::ReaderSession::Instance();
    ReaderApp::ScreenLayout layout;
    layout.window = Window;
    layout.sequence = sequence;
    const auto close = [&] {
        if (placed) {
            placed = false;
            layout.closed = true;
            session.PostLayout(layout);
        }
    };
    QRectF all;
    for (const QRectF& box : boxes) {
        if (!box.isEmpty()) {
            all = all.isNull() ? box : all.united(box);
        }
    }
    if (!running || !on_screen || all.isNull()) {
        close();
        return;
    }
    // The helper's places are of the whole picture; the reader's are of one of the console's screens.
    const QRectF picture = ReaderUi::PictureArea(), upper = ReaderUi::ScreenArea(0), lower = ReaderUi::ScreenArea(1);
    const QRectF whole = picture.isValid() ? picture : QRectF(0, 0, 1, 1);
    const OcrGate::OnScreen box = OcrGate::Place(all, whole, upper, lower);
    if (box.screen < 0) {
        close();
        return;
    }
    const auto scaled = [](const QRectF& r) { return QRectF(r.x() * Space, r.y() * Space, r.width() * Space, r.height() * Space); };
    layout.screen = box.screen;
    layout.space_width = layout.space_height = Space;
    layout.box = scaled(box.rect);
    for (const QRectF& each : boxes) {
        const OcrGate::OnScreen cell = each.isEmpty() ? OcrGate::OnScreen{} : OcrGate::Place(each, whole, upper, lower);
        layout.cells.append(cell.screen == box.screen ? std::optional<QRectF>(scaled(cell.rect)) : std::nullopt);
    }
    placed = true;
    session.PostLayout(layout);
}

} // namespace Home
