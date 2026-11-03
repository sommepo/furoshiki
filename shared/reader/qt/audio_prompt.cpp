// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include "reader/qt/audio_prompt.h"
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

namespace {

constexpr double SignPx = 16;

enum class Sign { Play, Stop, Keep, Drop };

/// The sign a button shows, drawn at `pixels` in `colour`: ▶, ■, a speaker with a tick or with
/// a cross (the Android app's ic_reader_audio_* in the same 24-unit box).
QIcon SignIcon(Sign sign, const QColor& colour, int pixels, qreal ratio) {
    QPixmap pixmap(QSize(pixels, pixels) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(pixels / 24.0, pixels / 24.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    const auto speaker = [&painter] {
        painter.drawPolygon(QPolygonF{{2, 9}, {6, 9}, {11, 5}, {11, 19}, {6, 15}, {2, 15}});
    };
    QPen stroke(colour, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (sign) {
    case Sign::Play:
        painter.drawPolygon(QPolygonF{{8, 5}, {19, 12}, {8, 19}});
        break;
    case Sign::Stop:
        painter.drawRect(QRectF(7, 7, 10, 10));
        break;
    case Sign::Keep:
        speaker();
        painter.setPen(stroke);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(QPolygonF{{14, 12.5}, {16.5, 15}, {21.5, 9}});
        break;
    case Sign::Drop:
        speaker();
        painter.setPen(stroke);
        painter.drawLine(QPointF(15, 9), QPointF(21, 15));
        painter.drawLine(QPointF(21, 9), QPointF(15, 15));
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

QString ButtonStyle(const QColor& colour, int pad_x, int pad_y) {
    QColor edge = colour;
    edge.setAlpha(0xA0);
    return QStringLiteral("QPushButton { background: %1; border: 1px solid %2; "
                          "border-radius: 4px; padding: %3px %4px; }"
                          "QPushButton:pressed { background: %5; }")
        .arg(Style::PopupBox.name(), edge.name(QColor::HexArgb))
        .arg(pad_y)
        .arg(pad_x)
        .arg(Style::PopupBoxPressed.name());
}

} // namespace

// As the popup: out of the window manager's hands where the layer over the game is.
static Qt::WindowFlags PromptFlags() {
    Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                            Qt::NoDropShadowWindowHint;
    if (qEnvironmentVariableIntValue("READER_OVERLAY_UNMANAGED") != 0) {
        flags |= Qt::X11BypassWindowManagerHint;
    }
    return flags;
}

AudioPrompt::AudioPrompt(QWidget* parent) : QWidget(parent, PromptFlags()) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    // Signs, no words: what each does is its tooltip and its name for a screen reader.
    setAccessibleName(tr("Add the line's audio to the card?"));
    listen = new QPushButton(this);
    keep = new QPushButton(this);
    drop = new QPushButton(this);
    keep->setToolTip(tr("With audio"));
    keep->setAccessibleName(tr("With audio"));
    drop->setToolTip(tr("Without"));
    drop->setAccessibleName(tr("Without"));
    for (QPushButton* button : {listen, keep, drop}) {
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Style::PointerCursor(Style::PointerSize(parent), devicePixelRatioF()));
    }
    auto* layout = new QHBoxLayout(this);
    layout->addWidget(listen);
    layout->addWidget(keep);
    layout->addWidget(drop);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    // The question comes up where the pointer has just been: the press that asked for the
    // card, or its bounce, is not an answer.
    const auto guarded = [this](void (AudioPrompt::*answer)()) {
        return [this, answer] {
            if (shown.isValid() && shown.elapsed() >= GuardMs) {
                emit(this->*answer)();
            }
        };
    };
    connect(listen, &QPushButton::clicked, this, guarded(&AudioPrompt::ListenClicked));
    connect(keep, &QPushButton::clicked, this, guarded(&AudioPrompt::KeepClicked));
    connect(drop, &QPushButton::clicked, this, guarded(&AudioPrompt::DropClicked));
    Prepare(100);
}

void AudioPrompt::Prepare(int scale) {
    const double factor = std::clamp(scale, 50, 250) / 100.0;
    sign_px = std::max(12, static_cast<int>(SignPx * factor + 0.5));
    const int pad = static_cast<int>(5 * factor + 0.5);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(static_cast<int>(4 * factor + 0.5));
    const int pad_x = static_cast<int>(8 * factor + 0.5);
    const int pad_y = static_cast<int>(4 * factor + 0.5);
    listen->setStyleSheet(ButtonStyle(Style::PopupText, pad_x, pad_y));
    keep->setStyleSheet(ButtonStyle(Style::PopupAccent, pad_x, pad_y));
    drop->setStyleSheet(ButtonStyle(Style::PopupText, pad_x, pad_y));
    const qreal ratio = devicePixelRatioF();
    keep->setIcon(SignIcon(Sign::Keep, Style::PopupAccent, sign_px, ratio));
    drop->setIcon(SignIcon(Sign::Drop, Style::PopupText, sign_px, ratio));
    for (QPushButton* button : {listen, keep, drop}) {
        button->setIconSize(QSize(sign_px, sign_px));
    }
    SetPlaying(false);
    adjustSize();
    shown.start();
}

void AudioPrompt::SetPlaying(bool playing_) {
    playing = playing_;
    // ▶ or ■, the same size: the buttons beside it keep their places.
    listen->setIcon(SignIcon(playing ? Sign::Stop : Sign::Play, Style::PopupText, sign_px,
                             devicePixelRatioF()));
    const QString name = playing ? tr("Stop") : tr("Listen");
    listen->setToolTip(name);
    listen->setAccessibleName(name);
}

void AudioPrompt::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    Style::PaintPopupGround(painter, rect());
    Style::PaintPopupEdge(painter, rect());
}

void AudioPrompt::mousePressEvent(QMouseEvent* event) {
    event->accept(); // clicks between the buttons answer nothing
}

} // namespace ReaderUi
