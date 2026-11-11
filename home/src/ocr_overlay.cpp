// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ocr_overlay.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include "reader/desktop/text_line.h"
#include "reader/qt/host.h"
#include "reader/qt/lookup_flow.h"

namespace Home {

OcrOverlay::OcrOverlay(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                          Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setWindowTitle(QStringLiteral("Furoshiki"));
    lookups = std::make_unique<ReaderUi::LookupFlow>(this);
    lookups->SetHome(this);
    lookups->mark = [this](qint64 backlog_id, int first, int last) {
        if (first < 0 || backlog_id == line.backlog_id) {
            mark_first = first;
            mark_last = last;
            update();
        }
    };
    lookups->notify = [](const QString&) {};
    lookups->open_dictionaries = [] {};
    lookups->pointer_inside_now = [this] {
        const QPoint global = ReaderUi::PointerPosition();
        if (const QWidget* popup = lookups->Popup(); popup != nullptr && popup->isVisible() && popup->geometry().contains(global)) {
            return true;
        }
        const int offset = OffsetAt(global);
        return offset >= 0 && offset >= mark_first && offset <= mark_last;
    };
    // The window takes no pointer events (they are the game's), so the pointer is asked for.
    pointer.setInterval(40);
    connect(&pointer, &QTimer::timeout, this, &OcrOverlay::Hover);
}

OcrOverlay::~OcrOverlay() = default;

void OcrOverlay::SetAccent(const QColor& colour) {
    accent = colour;
    update();
}

void OcrOverlay::SetLine(const ReaderApp::PresentedLine& presented, const QList<QRectF>& boxes_) {
    lookups->Close();
    line = presented;
    boxes = boxes_;
    box_of.clear();
    const QString& text = line.line.text;
    int box = 0;
    for (int i = 0; i < text.size(); ++i) {
        box_of.append(box);
        if (!(text[i].isHighSurrogate() && i + 1 < text.size())) {
            ++box;
        }
    }
    mark_first = mark_last = -1;
    hovered = -1;
    update();
}

void OcrOverlay::SetPlace(const QRect& place, bool on_screen) {
    const bool show = place.isValid() && on_screen && !boxes.isEmpty();
    if (place.isValid() && geometry() != place) {
        setGeometry(place);
    }
    if (show && !isVisible()) {
        QWidget::show();
        pointer.start();
    } else if (!show && isVisible()) {
        lookups->Close();
        pointer.stop();
        hide();
    }
}

QRectF OcrOverlay::BoxOf(int offset) const {
    if (offset < 0 || offset >= box_of.size() || box_of[offset] >= boxes.size()) {
        return {};
    }
    const QRectF box = boxes[box_of[offset]];
    if (box.width() <= 0 || box.height() <= 0) {
        return {};
    }
    return QRectF(box.x() * width(), box.y() * height(), box.width() * width(), box.height() * height());
}

int OcrOverlay::OffsetAt(const QPoint& global) const {
    const QPointF at = mapFromGlobal(global);
    for (int offset = 0; offset < box_of.size(); ++offset) {
        // A little past its edges: the gaps between characters belong to them.
        if (BoxOf(offset).adjusted(-1.5, -3, 1.5, 3).contains(at)) {
            return offset;
        }
    }
    return -1;
}

bool OcrOverlay::LookUpAt(int offset, bool quiet) {
    const QRectF box = BoxOf(offset);
    if (box.isEmpty()) {
        return false;
    }
    // The popup stands clear of the whole line, beside the word, anywhere on the screen.
    QRectF text;
    for (int i = 0; i < box_of.size(); ++i) {
        text = text.united(BoxOf(i));
    }
    const QScreen* on = screen();
    const QRect area = on != nullptr ? on->availableGeometry() : QRect(mapToGlobal(QPoint(0, 0)), size());
    ReaderUi::LookupFlow::Anchor anchor;
    anchor.owner = this;
    anchor.bounds = QRect(mapFromGlobal(area.topLeft()), area.size());
    anchor.keep_clear = text.toAlignedRect().adjusted(-4, -6, 4, 6);
    anchor.near_x = static_cast<int>(box.left());
    lookups->Lookup(line, offset, anchor, quiet);
    return true;
}

void OcrOverlay::Hover() {
    if (!isVisible()) {
        return;
    }
    const int offset = OffsetAt(ReaderUi::PointerPosition());
    const bool on_word = offset >= 0 && offset >= mark_first && offset <= mark_last;
    lookups->PointerMoved(on_word);
    if (offset != hovered) {
        hovered = offset;
        if (offset >= 0 && !on_word && !lookups->IsAbout(line.backlog_id, offset)) {
            LookUpAt(offset, true);
        }
    }
}

void OcrOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // The word looked up: a rule in the accent under its characters. The text is the game's.
    for (int offset = std::max(0, mark_first); offset <= mark_last; ++offset) {
        const QRectF box = BoxOf(offset);
        if (!box.isEmpty()) {
            p.fillRect(QRectF(box.left() - 0.5, box.bottom() + 2, box.width() + 1, 2.5), accent);
        }
    }
}

} // namespace Home
