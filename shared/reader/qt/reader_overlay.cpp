// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QFontMetricsF>
#include <QPainter>
#include <cmath>
#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPixmap>
#include <QPainterPath>
#include <QPen>
#include <QRegularExpression>
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

namespace {
constexpr int MessageMs = 3500;

/// One figure of the session line: "1,186" and "字", "6,910" and "/h", "1h05" and "m".
struct Figure {
    QString number;
    QString unit;
    QString label; ///< For the card: "chars", "speed", "time"
};

QList<Figure> Figures(const QString& line) {
    QList<Figure> out;
    static const QRegularExpression split(QStringLiteral("^(.*?)(\\D*)$"));
    for (const QString& part : line.split(QStringLiteral(" · "), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = split.match(part);
        Figure figure{match.captured(1), match.captured(2), {}};
        if (figure.number.isEmpty()) {
            figure.number = part; // no digits: as it is ("paused")
            figure.unit.clear();
        }
        figure.label = figure.unit.contains(QChar(u'字'))         ? QStringLiteral("chars")
                       : figure.unit == QStringLiteral("/h") ? QStringLiteral("speed")
                                                             : QStringLiteral("time");
        out.append(figure);
    }
    return out;
}

QFont SerifFont(qreal pixels, int weight) {
    QFont font;
    QStringList families{QStringLiteral("Noto Serif JP"), QStringLiteral("Noto Serif CJK JP"),
                         QStringLiteral("Yu Mincho"), QStringLiteral("Hiragino Mincho ProN"),
                         QStringLiteral("MS Mincho")};
    families += Style::JapaneseFamilies();
    font.setFamilies(families);
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixels))));
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

QFont GothicFont(qreal pixels, int weight) {
    QFont font;
    font.setFamilies(Style::JapaneseFamilies());
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixels))));
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

/// Two bars, the statistics paused, standing on `baseline` from `left`; returns their width.
qreal DrawPauseBars(QPainter& painter, qreal left, qreal baseline, qreal height, const QColor& colour,
                    bool shadow) {
    const qreal bar = std::max(2.0, height * 0.28);
    const qreal top = baseline - height;
    for (const qreal x : {left, left + bar * 1.6}) {
        if (shadow) {
            painter.fillRect(QRectF(x + 1, top + 1, bar, height), Style::SessionShadow);
        }
        QPainterPath path;
        path.addRoundedRect(QRectF(x, top, bar, height), bar * 0.35, bar * 0.35);
        painter.fillPath(path, colour);
    }
    return bar * 2.6;
}

/// A slip's shadow: a few soft layers under `box`.
void DrawSoftShadow(QPainter& painter, const QRectF& box, qreal radius, qreal size, int alpha) {
    painter.setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i) {
        const qreal spread = size * 0.12 * i;
        painter.setBrush(QColor(0, 0, 0, alpha / (i + 1)));
        painter.drawRoundedRect(box.adjusted(-spread, -spread + size * 0.18, spread, spread + size * 0.18),
                                radius + spread, radius + spread);
    }
}

/// The session line in the top right corner of `picture`, in one of its looks (proposals G,
/// H, F and D of the maintainer's choice): "paper", "paper-dark", "card", "ink".
void DrawSessionLine(QPainter& painter, const QString& style, const QString& line, bool paused,
                     const QRectF& picture) {
    const qreal size = std::clamp(picture.height() / 45.0, 11.0, 22.0);
    const QList<Figure> figures = Figures(line);
    const qreal margin = size * 0.6;

    if (style == QStringLiteral("card")) {
        // The SAVE MENU's detail card, small: a letter-spaced heading with the pause sign,
        // then a row for each figure, an italic label left and the figure right.
        QFont head = GothicFont(size * 0.55, QFont::Bold);
        head.setLetterSpacing(QFont::AbsoluteSpacing, size * 0.28);
        const QFont label = [&] {
            QFont font = SerifFont(size * 0.85, QFont::Bold);
            font.setItalic(true);
            return font;
        }();
        const QFont value = GothicFont(size * 0.9, QFont::ExtraBold);
        const QFontMetricsF head_metrics(head), label_metrics(label), value_metrics(value);
        const QString heading = QStringLiteral("READING");
        qreal label_width = 0;
        qreal value_width = 0;
        for (const Figure& figure : figures) {
            label_width = std::max(label_width, label_metrics.horizontalAdvance(figure.label));
            value_width = std::max(value_width, value_metrics.horizontalAdvance(figure.number + figure.unit));
        }
        const qreal gap = size * 0.7;
        const qreal bars_width = paused ? size * 0.75 : 0;
        const qreal pad_x = size * 0.75;
        const qreal pad_y = size * 0.42;
        const qreal row = std::max(label_metrics.height(), value_metrics.height()) * 0.92;
        const qreal inner = std::max(label_width + gap + value_width,
                                     head_metrics.horizontalAdvance(heading) + size * 0.5 + bars_width);
        const qreal height = pad_y * 2 + head_metrics.height() + row * figures.size();
        const QRectF box(picture.right() - margin - inner - 2 * pad_x, picture.top() + margin, inner + 2 * pad_x, height);
        const qreal radius = size * 0.6;
        DrawSoftShadow(painter, box, radius, size, 70);
        painter.setPen(QPen(QColor(0xF3, 0x9A, 0xCB), std::max(1.0, size * 0.09)));
        painter.setBrush(QColor(0xF4, 0xF0, 0xFF, 0xDC));
        painter.drawRoundedRect(box, radius, radius);
        qreal y = box.top() + pad_y + head_metrics.ascent();
        painter.setFont(head);
        painter.setPen(QColor(0x8A, 0x8C, 0xC0));
        painter.drawText(QPointF(box.left() + pad_x, y), heading);
        if (paused) {
            DrawPauseBars(painter, box.right() - pad_x - size * 0.5, y, head_metrics.ascent(),
                          QColor(0xE0, 0x4F, 0xA8), false);
        }
        y += head_metrics.descent();
        for (const Figure& figure : figures) {
            y += row;
            painter.setFont(label);
            painter.setPen(QColor(0x4B, 0x4F, 0x9A));
            painter.drawText(QPointF(box.left() + pad_x, y - row * 0.18), figure.label);
            painter.setFont(value);
            painter.setPen(QColor(0x1D, 0x2A, 0x6B));
            const QString text = figure.number + figure.unit;
            painter.drawText(QPointF(box.right() - pad_x - value_metrics.horizontalAdvance(text), y - row * 0.18), text);
        }
        return;
    }

    // A row: the pause sign, then each figure with its unit smaller, a separator between.
    const bool ink = style == QStringLiteral("ink");
    const bool dark = style == QStringLiteral("paper-dark");
    const QFont number = ink ? GothicFont(size * 1.05, QFont::ExtraBold) : SerifFont(size, QFont::DemiBold);
    const QFont unit = ink ? GothicFont(size * 0.72, QFont::Medium) : SerifFont(size * 0.72, QFont::DemiBold);
    const QFontMetricsF number_metrics(number), unit_metrics(unit);
    const qreal sep = ink ? size * 1.1 : size * 1.0;
    const qreal bars_height = number_metrics.ascent() * 0.72;
    const qreal bars_width = paused ? std::max(2.0, bars_height * 0.28) * 2.6 + (figures.isEmpty() ? 0 : size * 0.55) : 0;
    qreal width = bars_width;
    for (int i = 0; i < figures.size(); ++i) {
        width += number_metrics.horizontalAdvance(figures[i].number) + unit_metrics.horizontalAdvance(figures[i].unit) +
                 (figures[i].unit.isEmpty() ? 0 : size * 0.1) + (i > 0 ? sep : 0);
    }
    const qreal pad_x = ink ? 0 : size * 0.7;
    const qreal pad_y = ink ? 0 : size * 0.28;
    const QRectF box(picture.right() - margin - width - 2 * pad_x, picture.top() + margin, width + 2 * pad_x,
                     number_metrics.height() + 2 * pad_y);
    const qreal baseline = box.top() + pad_y + number_metrics.ascent();
    QColor text_colour, unit_colour, sep_colour, bars_colour;
    if (ink) {
        text_colour = QColor(0xFF, 0xFF, 0xFF);
        unit_colour = QColor(0xFF, 0xFF, 0xFF, 0xCC);
        sep_colour = QColor(0xB9, 0xB0, 0xF2);
        bars_colour = QColor(0x7F, 0xF0, 0xC8);
    } else if (dark) {
        DrawSoftShadow(painter, box, size * 0.27, size, 110);
        painter.setPen(QPen(QColor(0x5A, 0x46, 0x32), 1));
        painter.setBrush(QColor(0x2B, 0x21, 0x19));
        painter.drawRoundedRect(box, size * 0.27, size * 0.27);
        text_colour = QColor(0xEF, 0xE4, 0xCF);
        unit_colour = QColor(0xB8, 0xA8, 0x8A);
        sep_colour = QColor(0x6E, 0x58, 0x40);
        bars_colour = QColor(0xD9, 0xA5, 0x5A);
    } else {
        DrawSoftShadow(painter, box, size * 0.27, size, 80);
        painter.setPen(QPen(QColor(0xD9, 0xD0, 0xBD), 1));
        painter.setBrush(QColor(0xFB, 0xF8, 0xF1));
        painter.drawRoundedRect(box, size * 0.27, size * 0.27);
        text_colour = QColor(0x2A, 0x26, 0x20);
        unit_colour = QColor(0x7A, 0x6E, 0x5A);
        sep_colour = QColor(0xC9, 0xBD, 0xA4);
        bars_colour = QColor(0xB0, 0x78, 0x2A);
    }
    // Ink holds on any picture by a soft dark halo round its letters, not a box.
    const auto text = [&](const QPointF& at, const QString& what, const QColor& colour) {
        if (ink) {
            painter.setPen(QColor(0, 0, 0, 0x50));
            for (const QPointF offset : {QPointF(-1, 0), QPointF(1, 0), QPointF(0, -1), QPointF(0, 1),
                                         QPointF(1, 1), QPointF(-1, 1), QPointF(0, 2), QPointF(1.5, 1.5)}) {
                painter.drawText(at + offset, what);
            }
        }
        painter.setPen(colour);
        painter.drawText(at, what);
    };
    qreal x = box.left() + pad_x;
    if (paused) {
        DrawPauseBars(painter, x, baseline - number_metrics.ascent() * 0.05, bars_height, bars_colour, ink);
        x += bars_width;
    }
    for (int i = 0; i < figures.size(); ++i) {
        if (i > 0) {
            const qreal middle = x + sep / 2;
            if (ink) {
                const qreal dot = std::max(1.5, size * 0.12);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 0x90));
                painter.drawEllipse(QPointF(middle + 0.5, baseline - number_metrics.xHeight() * 0.5 + 0.8), dot + 0.8, dot + 0.8);
                painter.setBrush(sep_colour);
                painter.drawEllipse(QPointF(middle, baseline - number_metrics.xHeight() * 0.5), dot, dot);
            } else {
                painter.setFont(number);
                text(QPointF(middle - number_metrics.horizontalAdvance(QStringLiteral("·")) / 2, baseline),
                     QStringLiteral("·"), sep_colour);
            }
            x += sep;
        }
        painter.setFont(number);
        text(QPointF(x, baseline), figures[i].number, text_colour);
        x += number_metrics.horizontalAdvance(figures[i].number);
        if (!figures[i].unit.isEmpty()) {
            x += size * 0.1;
            painter.setFont(unit);
            text(QPointF(x, baseline), figures[i].unit, unit_colour);
            x += unit_metrics.horizontalAdvance(figures[i].unit);
        }
    }
}
} // namespace

QStringList Style::JapaneseFamilies() {
    return {QStringLiteral("Yu Gothic UI"),      QStringLiteral("Meiryo UI"),
            QStringLiteral("Meiryo"),            QStringLiteral("Noto Sans CJK JP"),
            QStringLiteral("Noto Sans JP"),      QStringLiteral("Hiragino Sans"),
            QStringLiteral("IPAexGothic"),       QStringLiteral("MS UI Gothic"),
            QStringLiteral("sans-serif")};
}

void Style::DrawOpenBook(QPainter& painter, const QRectF& area, const QColor& colour,
                         qreal width) {
    const qreal dip = area.height() * 0.22;
    QPainterPath pages;
    pages.moveTo(area.center().x(), area.top() + dip);
    pages.quadTo(area.left() + area.width() * 0.25, area.top() - dip * 0.4, area.left(),
                 area.top() + dip * 0.5);
    pages.lineTo(area.left(), area.bottom() - dip * 0.5);
    pages.quadTo(area.left() + area.width() * 0.25, area.bottom() - dip * 1.4, area.center().x(),
                 area.bottom());
    pages.quadTo(area.right() - area.width() * 0.25, area.bottom() - dip * 1.4, area.right(),
                 area.bottom() - dip * 0.5);
    pages.lineTo(area.right(), area.top() + dip * 0.5);
    pages.quadTo(area.right() - area.width() * 0.25, area.top() - dip * 0.4, area.center().x(),
                 area.top() + dip);
    pages.lineTo(area.center().x(), area.bottom());
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(pages);
    painter.restore();
}

// Under X11 a window manager may put a frame of its own around this window without giving
// the frame the window's "lets the pointer through": Openbox does (Batocera), and the frame
// then takes every click on the game, so no word can be clicked. With
// READER_OVERLAY_UNMANAGED=1 in the environment the window manager is kept out of it; the
// layer then lies above every window on the screen for as long as it is shown.
static Qt::WindowFlags OverlayFlags() {
    Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                            Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint;
    if (qEnvironmentVariableIntValue("READER_OVERLAY_UNMANAGED") != 0) {
        flags |= Qt::X11BypassWindowManagerHint;
    }
    return flags;
}

ReaderOverlay::ReaderOverlay(QWidget* parent) : QWidget(parent, OverlayFlags()) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    message_timer.setSingleShot(true);
    connect(&message_timer, &QTimer::timeout, this, [this] {
        message.clear();
        update();
        emit ContentChanged();
    });
    glide_timer.setTimerType(Qt::PreciseTimer);
    glide_timer.setInterval(1000 / 120);
    connect(&glide_timer, &QTimer::timeout, this, &ReaderOverlay::GlideFrame);
    crt_frame.ready = [this] {
        update();
        emit ContentChanged();
    };
    // The CRT settings may change in their page at any time: a look now and then is enough.
    frame_timer.setInterval(400);
    connect(&frame_timer, &QTimer::timeout, this, &ReaderOverlay::RefreshFrame);
    frame_timer.start();
}

void ReaderOverlay::RefreshFrame() {
    if (session_picture.isValid()) {
        crt_frame.Update(size(), session_picture, devicePixelRatioF());
    }
}

void ReaderOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    RefreshFrame();
}

QRect ReaderOverlay::CursorArea() const {
    if (!cursor_visible) {
        return {};
    }
    if (cursor_character) {
        return cursor_character->adjusted(-6, -6, 6, 6).toAlignedRect();
    }
    const qreal reach = Style::PointerSize(this) * 1.5 + 4;
    return QRectF(cursor_position - QPointF(reach, reach), QSizeF(reach * 2, reach * 2)).toAlignedRect();
}

void ReaderOverlay::SetMarks(const QList<QRectF>& marks_) {
    if (marks == marks_) {
        return;
    }
    // To another word it glides; a mark the window's new size moved is simply there.
    const bool glide = mark_glide.Visible() && size() == marks_size;
    marks = marks_;
    marks_size = size();
    mark_glide.SetTarget(marks, glide);
    if (mark_glide.Moving() && !glide_timer.isActive()) {
        glide_clock.start();
        glide_timer.start();
    }
    update();
    emit ContentChanged();
}

void ReaderOverlay::GlideFrame() {
    // Only where the mark was and is: the rest of the window (the frame) stands still.
    QRectF dirty;
    for (const QRectF& rect : mark_glide.Drawn()) {
        dirty = dirty.united(rect);
    }
    const bool moving = mark_glide.Advance(static_cast<qreal>(glide_clock.restart()) / 1000);
    for (const QRectF& rect : mark_glide.Drawn()) {
        dirty = dirty.united(rect);
    }
    update(dirty.adjusted(-4, -4, 4, 4).toAlignedRect());
    if (!moving) {
        glide_timer.stop();
        // A mark faded out: the window may go.
        emit ContentChanged();
    }
}

void ReaderOverlay::SetMarkStyle(const QString& style) {
    if (mark_style == style) {
        return;
    }
    mark_style = style;
    update();
}

void ReaderOverlay::SetOutlines(const QList<ReaderApp::PlacedText>& texts) {
    QList<QRectF> boxes;
    QList<QRectF> cells;
    for (const ReaderApp::PlacedText& text : texts) {
        boxes.append(text.box);
        for (const ReaderApp::PlacedText::Glyph& character : text.characters) {
            cells.append(character.drawn);
        }
    }
    if (boxes == outline_boxes && cells == outline_cells) {
        return;
    }
    outline_boxes = boxes;
    outline_cells = cells;
    update();
    emit ContentChanged();
}

void ReaderOverlay::SetSessionLine(const QString& text, const QRectF& picture, bool paused,
                                   const QString& style) {
    if (session_line == text && session_picture == picture && session_paused == paused &&
        session_style == style) {
        return;
    }
    session_paused = paused;
    session_style = style;
    session_line = text;
    session_picture = picture;
    RefreshFrame();
    update();
    emit ContentChanged();
}

void ReaderOverlay::SetCursor(bool visible, const QPointF& position,
                              const std::optional<QRectF>& character) {
    if (cursor_visible == visible && cursor_position == position &&
        cursor_character == character) {
        return;
    }
    const QRect before = CursorArea();
    cursor_visible = visible;
    cursor_position = position;
    cursor_character = character;
    update(before.united(CursorArea()));
    emit ContentChanged();
}

void ReaderOverlay::ShowMessage(const QString& text, const QRectF& picture) {
    message = text;
    message_picture = picture;
    message_timer.start(MessageMs);
    update();
    emit ContentChanged();
}

bool ReaderOverlay::HasContent() const {
    return !marks.isEmpty() || mark_glide.Visible() || !crt_frame.Image().isNull() || !outline_boxes.isEmpty() || !session_line.isEmpty() || session_paused ||
           cursor_visible || !message.isEmpty();
}

void ReaderOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (!crt_frame.Image().isNull()) {
        painter.drawImage(QPointF(0, 0), crt_frame.Image());
    }
    painter.setRenderHint(QPainter::Antialiasing);

    if (!outline_boxes.isEmpty()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0x4F, 0xC3, 0xF7, 0xCC), 1.5));
        for (const QRectF& box : std::as_const(outline_boxes)) {
            painter.drawRect(box);
        }
        painter.setPen(QPen(QColor(0xFF, 0x52, 0x52, 0xCC), 1.0));
        for (const QRectF& cell : std::as_const(outline_cells)) {
            painter.drawRect(cell);
        }
    }

    const QList<QRectF> drawn_marks = mark_glide.Drawn();
    painter.setOpacity(mark_glide.Opacity());
    if (mark_style == QStringLiteral("outline")) {
        QColor frame = Style::Highlight;
        frame.setAlpha(0xE6);
        painter.setPen(QPen(frame, 2.0));
        painter.setBrush(Qt::NoBrush);
        for (const QRectF& mark : drawn_marks) {
            painter.drawRoundedRect(mark.adjusted(-1, -1, 1, 1), 3, 3);
        }
    } else if (mark_style != QStringLiteral("none")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Style::Highlight);
        for (const QRectF& mark : drawn_marks) {
            painter.drawRoundedRect(mark, 3, 3);
        }
    }
    painter.setOpacity(1);

    QFont font = painter.font();
    font.setFamilies(Style::JapaneseFamilies());

    if ((!session_line.isEmpty() || session_paused) && session_picture.isValid()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        DrawSessionLine(painter, session_style, session_line, session_paused, session_picture);
        painter.restore();
    }

    if (!message.isEmpty() && message_picture.isValid()) {
        font.setPixelSize(std::clamp(static_cast<int>(message_picture.height() / 36.0), 13, 26));
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        const qreal pad = metrics.height() * 0.5;
        const qreal widest = message_picture.width() * 0.9 - 2 * pad;
        const QRectF text_bounds = metrics.boundingRect(QRectF(0, 0, widest, 10000),
                                                        Qt::AlignCenter | Qt::TextWordWrap, message);
        QRectF box(0, 0, text_bounds.width() + 2 * pad, text_bounds.height() + 2 * pad);
        // At the top: a game's text box is at the bottom as a rule.
        box.moveCenter(QPointF(message_picture.center().x(), 0));
        box.moveTop(message_picture.top() + metrics.height() * 2.2);
        painter.setPen(QPen(Style::Outline, 1));
        QColor background = Style::PopupBackground;
        background.setAlpha(0xF2);
        painter.setBrush(background);
        painter.drawRoundedRect(box, 6, 6);
        painter.setPen(Style::Text);
        painter.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignCenter | Qt::TextWordWrap,
                         message);
    }

    if (cursor_visible) {
        painter.setBrush(Qt::NoBrush);
        if (cursor_character) {
            // On a character the cursor is a frame around it.
            const QRectF frame = cursor_character->adjusted(-2, -2, 2, 2);
            painter.setPen(QPen(Style::CursorEdge, 4));
            painter.drawRoundedRect(frame, 3, 3);
            painter.setPen(QPen(Style::Accent, 2));
            painter.drawRoundedRect(frame, 3, 3);
        } else {
            Style::DrawPointer(painter, cursor_position, Style::PointerSize(this));
        }
    }
}

// The console's look (PaintPopup*, DrawPointer) is in reader_look.cpp.

QCursor Style::PointerCursor(qreal size, qreal ratio) {
    const int side = std::max(8, static_cast<int>(std::ceil(size * ratio)));
    QPixmap picture(side, side);
    picture.fill(Qt::transparent);
    {
        QPainter painter(&picture);
        painter.setRenderHint(QPainter::Antialiasing);
        DrawPointer(painter, QPointF(side / 2.0, side / 2.0), side);
    }
    picture.setDevicePixelRatio(ratio);
    return QCursor(picture, static_cast<int>(size / 2), static_cast<int>(size / 2));
}

qreal Style::PointerSize(const QWidget* over) {
    // seen from a sofa as well as from a desk
    return std::max(30.0, (over != nullptr ? over->height() : 720) / 20.0);
}

void ReaderOverlay::showEvent(QShowEvent* event) {
    // The game's picture is under this window, in the main window: its pointer is the console's.
    if (QWidget* host = parentWidget()) {
        host->setCursor(Style::PointerCursor(Style::PointerSize(this), devicePixelRatioF()));
    }
    QWidget::showEvent(event);
}

void ReaderOverlay::hideEvent(QHideEvent* event) {
    if (QWidget* host = parentWidget()) {
        host->unsetCursor();
    }
    QWidget::hideEvent(event);
}

} // namespace ReaderUi
