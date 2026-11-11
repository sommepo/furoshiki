// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "text_strip.h"

#include <algorithm>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include "reader/desktop/text_line.h"
#include "reader/qt/host.h"
#include "reader/qt/lookup_flow.h"

namespace Home {

namespace {
constexpr int HeadHeight = 30;
constexpr int Side = 22;
constexpr int TextTop = HeadHeight + 14;
constexpr int TextBottom = 18;
constexpr int Grip = 10;       // its right edge, which changes its width
constexpr int CloseWidth = 34; // the cross at the head's right
constexpr int MinWidth = 360;
} // namespace

TextStrip::TextStrip(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    // It lies over another program's window, which stays the one in use.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("Furoshiki"));
    look.ground = QColor(0x10, 0x10, 0x10);
    look.ink = QColor(0xf2, 0xf2, 0xee);
    look.muted = QColor(0x8a, 0x8a, 0x86);
    look.accent = QColor(0xff, 0x4d, 0x2e);
    look.rule = QColor(0x3a, 0x3a, 0x38);
    look.text = font();
    look.text.setPixelSize(26);
    look.mono = font();
    look.mono.setPixelSize(11);

    lookups = std::make_unique<ReaderUi::LookupFlow>(this);
    lookups->SetHome(this);
    lookups->mark = [this](qint64 backlog_id, int first, int last) {
        if (first < 0 || backlog_id == line.backlog_id) {
            mark_first = first;
            mark_last = last;
            update();
        }
    };
    lookups->notify = [this](const QString& text) { Say(text); };
    lookups->open_dictionaries = [] {};
    lookups->pointer_inside_now = [this] {
        const QPoint global = ReaderUi::PointerPosition();
        if (const QWidget* popup = lookups->Popup(); popup != nullptr && popup->isVisible() && popup->geometry().contains(global)) {
            return true;
        }
        const int offset = OffsetAt(QPointF(mapFromGlobal(global)));
        return offset >= 0 && offset >= mark_first && offset <= mark_last;
    };
    resize(900, HeadHeight + 90);
}

TextStrip::~TextStrip() = default;

void TextStrip::SetLook(const Look& look_) {
    look = look_;
    LayOut();
    update();
}

void TextStrip::SetHead(const QString& title_, const QString& source_, const QString& status_) {
    title = title_;
    source = source_;
    status = status_;
    update();
}

void TextStrip::SetOpacity(int percent) {
    opacity = percent <= 0 ? 92 : std::clamp(percent, 20, 100);
    update();
}

void TextStrip::SetLine(const ReaderApp::PresentedLine& presented) {
    lookups->Close();
    line = presented;
    mark_first = mark_last = -1;
    hovered = -1;
    LayOut();
    update();
}

void TextStrip::Say(const QString& text) {
    said = text;
    update();
    QTimer::singleShot(4000, this, [this, text] {
        if (said == text) {
            said.clear();
            update();
        }
    });
}

void TextStrip::Place() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    const QRect area = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    QSettings settings;
    const QRect kept = settings.value(QStringLiteral("strip/geometry")).toRect();
    if (kept.isValid() && area.intersects(kept)) {
        setGeometry(kept.x(), kept.y(), std::max(MinWidth, kept.width()), height());
    } else {
        const int width = std::clamp(area.width() * 2 / 3, MinWidth, 1200);
        setGeometry(area.center().x() - width / 2, area.bottom() - height() - 80, width, height());
    }
    LayOut();
}

void TextStrip::Keep() {
    QSettings settings;
    settings.setValue(QStringLiteral("strip/geometry"), geometry());
}

void TextStrip::LayOut() {
    layout = std::make_unique<QTextLayout>(line.line.text, look.text);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAnywhere);
    layout->setTextOption(option);
    const qreal wide = std::max(100, width() - 2 * Side);
    qreal y = 0;
    layout->beginLayout();
    for (;;) {
        QTextLine row = layout->createLine();
        if (!row.isValid()) {
            break;
        }
        row.setLineWidth(wide);
        row.setPosition(QPointF(0, y));
        y += row.height() + 4;
    }
    layout->endLayout();
    text_at = QPointF(Side, TextTop);
    const int rows_tall = static_cast<int>(std::max<qreal>(y, QFontMetricsF(look.text).height()));
    const int tall = TextTop + rows_tall + TextBottom;
    if (tall != height()) {
        // It grows downwards from its head, which stays where it was put.
        setFixedHeight(tall);
    }
}

int TextStrip::OffsetAt(const QPointF& at) const {
    if (!layout || line.line.text.isEmpty()) {
        return -1;
    }
    const QPointF in = at - text_at;
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine row = layout->lineAt(i);
        if (in.y() < row.y() || in.y() >= row.y() + row.height()) {
            continue;
        }
        if (in.x() < 0 || in.x() >= row.naturalTextWidth()) {
            return -1;
        }
        const int offset = row.xToCursor(in.x(), QTextLine::CursorOnCharacter);
        return offset >= row.textStart() && offset < row.textStart() + row.textLength() ? offset : -1;
    }
    return -1;
}

QList<QRectF> TextStrip::RowsOf(int first, int last) const {
    QList<QRectF> rows;
    if (!layout || first < 0) {
        return rows;
    }
    const int end = std::min<int>(last + 1, line.line.text.size());
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine row = layout->lineAt(i);
        const int from = std::max(first, row.textStart());
        const int to = std::min(end, row.textStart() + row.textLength());
        if (from < to) {
            const qreal left = row.cursorToX(from), right = row.cursorToX(to);
            rows.append(QRectF(text_at.x() + left, text_at.y() + row.y(), right - left, row.height()));
        }
    }
    return rows;
}

bool TextStrip::LookUpAt(int offset, bool quiet) {
    if (offset < 0 || offset >= line.line.text.size()) {
        return false;
    }
    const QList<QRectF> rows = RowsOf(offset, offset + ReaderApp::CharCountAt(line.line.text, offset) - 1);
    if (rows.isEmpty()) {
        return false;
    }
    // The popup goes above or below the whole strip, beside the word, anywhere on the screen.
    const QScreen* on = screen();
    const QRect area = on != nullptr ? on->availableGeometry() : QRect(mapToGlobal(QPoint(0, 0)), size());
    ReaderUi::LookupFlow::Anchor anchor;
    anchor.owner = this;
    anchor.bounds = QRect(mapFromGlobal(area.topLeft()), area.size());
    anchor.keep_clear = rect();
    anchor.near_x = static_cast<int>(rows.first().left());
    lookups->Lookup(line, offset, anchor, quiet);
    return true;
}

void TextStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QColor ground = look.ground;
    ground.setAlphaF(opacity / 100.0);
    p.fillRect(rect(), ground);
    p.fillRect(QRectF(0, 0, width(), 1), look.rule);
    p.fillRect(QRectF(0, height() - 1, width(), 1), look.rule);
    p.fillRect(QRectF(Side, HeadHeight, width() - 2 * Side, 1), look.rule);

    // Its head: the title in ink, what reads it, and at the right what the source says.
    p.setFont(look.mono);
    const QFontMetricsF mono(look.mono);
    const qreal base = HeadHeight / 2.0 + mono.capHeight() / 2.0 + 1;
    const qreal right = width() - Side - CloseWidth;
    // What reads it is said whole; the title gives way first, then what the source says.
    const QString by = QStringLiteral("  ·  ") + source.toUpper();
    const qreal by_width = mono.horizontalAdvance(by);
    const QString name = mono.elidedText(title.toUpper(), Qt::ElideRight, std::max<qreal>(60, (right - Side) * 0.45 - by_width));
    const qreal name_width = mono.horizontalAdvance(name);
    p.setPen(look.ink);
    p.drawText(QPointF(Side, base), name);
    p.setPen(look.muted);
    p.drawText(QPointF(Side + name_width, base), by);
    const QString where = said.isEmpty() ? status.toUpper() : said.toUpper();
    const qreal where_width = std::max<qreal>(0, right - (Side + name_width + by_width + 24));
    p.setPen(said.isEmpty() ? look.muted : look.ink);
    p.drawText(QRectF(right - where_width, 0, where_width, HeadHeight), Qt::AlignRight | Qt::AlignVCenter,
               mono.elidedText(where, Qt::ElideRight, where_width));
    p.setPen(look.muted);
    p.drawText(QRectF(width() - Side - CloseWidth, 0, CloseWidth, HeadHeight), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("×"));

    if (line.line.text.isEmpty()) {
        p.setPen(look.muted);
        p.drawText(QPointF(Side, TextTop + QFontMetricsF(look.text).ascent() * 0.7), QStringLiteral("WAITING FOR A LINE"));
        return;
    }
    // The word looked up: a rule in the accent under it.
    for (const QRectF& row : RowsOf(mark_first, mark_last)) {
        p.fillRect(QRectF(row.left(), row.bottom() - 1, row.width(), 2), look.accent);
    }
    p.setPen(look.ink);
    layout->draw(&p, text_at);
}

void TextStrip::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (event->size().width() != event->oldSize().width()) {
        LayOut();
    }
}

void TextStrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPoint at = event->position().toPoint();
    if (at.y() < HeadHeight && at.x() >= width() - Side - CloseWidth) {
        close();
        return;
    }
    if (at.x() >= width() - Grip) {
        sizing = true;
    } else if (at.y() < HeadHeight || OffsetAt(event->position()) < 0) {
        dragging = true;
    } else {
        LookUpAt(OffsetAt(event->position()));
        return;
    }
    lookups->Close();
    drag_from = event->globalPosition().toPoint();
    drag_geometry = geometry();
}

void TextStrip::mouseMoveEvent(QMouseEvent* event) {
    const QPoint by = event->globalPosition().toPoint() - drag_from;
    if (dragging) {
        move(drag_geometry.topLeft() + by);
        return;
    }
    if (sizing) {
        setGeometry(drag_geometry.x(), drag_geometry.y(), std::max(MinWidth, drag_geometry.width() + by.x()), height());
        return;
    }
    const QPoint at = event->position().toPoint();
    setCursor(at.x() >= width() - Grip ? Qt::SizeHorCursor : at.y() < HeadHeight ? Qt::SizeAllCursor : Qt::ArrowCursor);
    // A word under the pointer is looked up at once, as over a game's text; the open popup's
    // own word is left alone.
    const int offset = OffsetAt(event->position());
    const bool on_word = offset >= 0 && offset >= mark_first && offset <= mark_last;
    lookups->PointerMoved(on_word);
    if (offset != hovered) {
        hovered = offset;
        if (offset >= 0 && !on_word && !lookups->IsAbout(line.backlog_id, offset)) {
            LookUpAt(offset, true);
        }
    }
}

void TextStrip::mouseReleaseEvent(QMouseEvent*) {
    if (dragging || sizing) {
        Keep();
    }
    dragging = sizing = false;
}

void TextStrip::leaveEvent(QEvent*) {
    hovered = -1;
    lookups->PointerMoved(false);
}

void TextStrip::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (lookups->IsOpen()) {
            lookups->Close();
        } else {
            close();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

void TextStrip::closeEvent(QCloseEvent* event) {
    // Closing it ends the reading: the one who made it says what that means.
    event->ignore();
    lookups->Close();
    if (close_wanted) {
        close_wanted();
    }
}

} // namespace Home
