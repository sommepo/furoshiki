// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include "choice_picker.h"

namespace Home {

namespace {

double Scale = 1; ///< Of the picker open (one at a time)

// On the page's scale (Colours::scale makes them the window's).
constexpr double RowAt1 = 44;
constexpr double HeadAt1 = 56;  ///< The title's band above the rows
constexpr double FootAt1 = 40;  ///< The keys' line below them
constexpr double SideAt1 = 24;  ///< Inside the card, left and right
constexpr qint64 OpenTime = 190, CloseTime = 120;

double RowHeight() {
    return RowAt1 * Scale;
}
double Head() {
    return HeadAt1 * Scale;
}
double Foot() {
    return FootAt1 * Scale;
}
double Side() {
    return SideAt1 * Scale;
}

double EaseOut(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return 1 - std::pow(1 - t, 3);
}

QColor WithAlpha(QColor colour, double alpha) {
    colour.setAlphaF(std::clamp(alpha, 0.0, 1.0) * colour.alphaF());
    return colour;
}

} // namespace

ChoicePicker* ChoicePicker::Open(QWidget* over, const QString& title, const QStringList& choices, int now,
                                 const Colours& colours, std::function<void(int)> picked,
                                 std::function<void(int)> focused, const QRect& keep_clear) {
    auto* picker = new ChoicePicker(over, title, choices, now, colours, std::move(picked), std::move(focused), keep_clear);
    picker->show();
    picker->raise();
    picker->setFocus(Qt::PopupFocusReason);
    return picker;
}

ChoicePicker::ChoicePicker(QWidget* over, const QString& title_, const QStringList& choices_, int now_,
                           const Colours& colours_, std::function<void(int)> picked_,
                           std::function<void(int)> focused_, const QRect& keep_clear_)
    // None in use (-1, a list of actions): no mark, the first in hand.
    : QWidget(over), title(title_), choices(choices_), now(now_ < 0 ? -1 : std::min(now_, std::max(0, static_cast<int>(choices_.size()) - 1))),
      chosen(std::max(now, 0)), colours(colours_), picked(std::move(picked_)), focused(std::move(focused_)), keep_clear(keep_clear_) {
    Scale = colours.scale > 0 ? colours.scale : 1;
    setObjectName(QStringLiteral("choice-picker"));
    setGeometry(over->rect());
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    Blur();
    // The one in use in the middle of the list, where the list is longer than the card.
    top = target = std::clamp(chosen - Visible() / 2.0 + 0.5, 0.0, std::max(0.0, static_cast<double>(choices.size() - Visible())));
    top = target = std::floor(top);
    frames.setInterval(16);
    connect(&frames, &QTimer::timeout, this, [this] {
        const qint64 t = clock.elapsed();
        if (leaving && t - left_at >= CloseTime) {
            frames.stop();
            const bool had_focus = hasFocus();
            hide();
            if (had_focus) {
                if (QWidget* window = parentWidget()) {
                    window->setFocus();
                }
            }
            deleteLater();
            return;
        }
        top += (target - top) * 0.35;
        if (std::abs(target - top) < 0.002) {
            top = target;
        }
        update();
        if (!leaving && t >= OpenTime && top == target) {
            frames.stop();
        }
    });
    clock.start();
    frames.start();
}

void ChoicePicker::Blur() {
    // The window as it is, softened: drawn small and back again twice, which reads as a blur
    // and costs nothing next to a real one.
    QWidget* window = parentWidget();
    const QPixmap shot = window->grab();
    if (shot.isNull()) {
        return;
    }
    const QSize full = shot.size();
    QImage image = shot.toImage();
    for (const int by : {6, 14}) {
        const QSize small(std::max(1, full.width() / by), std::max(1, full.height() / by));
        image = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .scaled(full, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    behind = QPixmap::fromImage(image);
    behind.setDevicePixelRatio(shot.devicePixelRatio());
}

int ChoicePicker::Visible() const {
    const double ceiling = keep_clear.isEmpty() ? height() * 0.74 : std::max(Head() + RowHeight() + Foot(), keep_clear.top() - 28.0 * Scale);
    const double room = ceiling - Head() - Foot();
    return std::clamp(static_cast<int>(room / RowHeight()), 1, std::max(1, static_cast<int>(choices.size())));
}

QRectF ChoicePicker::Card() const {
    const QFontMetricsF row(colours.row);
    const QFontMetricsF head(colours.title);
    double widest = head.horizontalAdvance(title.toUpper()) + 2 * Side();
    for (const QString& choice : choices) {
        widest = std::max(widest, row.horizontalAdvance(choice) + 2 * Side() + 44 * Scale);
    }
    const double w = std::clamp(widest, 400 * Scale, std::max(400 * Scale, width() * 0.7));
    const double h = Head() + Visible() * RowHeight() + Foot();
    if (!keep_clear.isEmpty()) {
        const double top_y = std::max(16.0 * Scale, (keep_clear.top() - 16.0 * Scale - h) / 2.0);
        return {(width() - w) / 2, top_y, w, h};
    }
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

int ChoicePicker::RowAt(const QPointF& at) const {
    const QRectF card = Card();
    const QRectF list(card.left(), card.top() + Head(), card.width(), Visible() * RowHeight());
    if (!list.contains(at)) {
        return -1;
    }
    const int index = static_cast<int>(std::floor((at.y() - list.top()) / RowHeight() + top));
    return index >= 0 && index < choices.size() ? index : -1;
}

void ChoicePicker::Show(int index) {
    if (choices.isEmpty()) {
        return;
    }
    const int prev = chosen;
    chosen = std::clamp(index, 0, static_cast<int>(choices.size()) - 1);
    if (chosen != prev && focused) {
        focused(chosen);
    }
    const int visible = Visible();
    if (chosen < target) {
        target = chosen;
    } else if (chosen >= target + visible) {
        target = chosen - visible + 1;
    }
    if (!frames.isActive()) {
        frames.start();
    }
    update();
}

void ChoicePicker::Take(int index) {
    if (leaving || index < 0 || index >= choices.size()) {
        return;
    }
    chosen = index;
    now = index; // marked as taken while the card fades
    auto then = std::move(picked);
    picked = nullptr;
    focused = nullptr; // taken, not cancelled
    Leave();
    if (then) {
        then(index);
    }
}

void ChoicePicker::Leave() {
    if (leaving) {
        return;
    }
    leaving = true;
    left_at = clock.elapsed();
    if (focused) {
        auto notify = std::move(focused);
        focused = nullptr;
        notify(-1);
    }
    setAttribute(Qt::WA_TransparentForMouseEvents);
    frames.start();
}

bool ChoicePicker::Key(int key) {
    if (leaving) {
        return true;
    }
    const int page = std::max(1, Visible() - 1);
    switch (key) {
    case Qt::Key_Up:
        Show(chosen - 1);
        return true;
    case Qt::Key_Down:
    case Qt::Key_Tab:
        Show(chosen + 1);
        return true;
    case Qt::Key_PageUp:
    case Qt::Key_Q:
    case Qt::Key_Left:
        Show(chosen - page);
        return true;
    case Qt::Key_PageDown:
    case Qt::Key_E:
    case Qt::Key_Right:
        Show(chosen + page);
        return true;
    case Qt::Key_Home:
        Show(0);
        return true;
    case Qt::Key_End:
        Show(static_cast<int>(choices.size()) - 1);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Take(chosen);
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        Leave();
        return true;
    default:
        return true; // nothing reaches the page while the card is up
    }
}

void ChoicePicker::keyPressEvent(QKeyEvent* event) {
    Key(event->key());
}

void ChoicePicker::resizeEvent(QResizeEvent*) {
    Show(chosen);
}

void ChoicePicker::mouseMoveEvent(QMouseEvent* event) {
    const int at = RowAt(event->position());
    if (at != hover) {
        hover = at;
        if (at >= 0 && at != chosen) {
            chosen = at;
            if (focused) {
                focused(chosen);
            }
        }
        update();
    }
    setCursor(at >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ChoicePicker::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton || !Card().contains(event->position())) {
        Leave(); // a click outside: as it was
        return;
    }
    if (event->button() == Qt::LeftButton) {
        Take(RowAt(event->position()));
    }
}

void ChoicePicker::wheelEvent(QWheelEvent* event) {
    const double rows = -event->angleDelta().y() / 120.0;
    target = std::clamp(target + rows, 0.0, std::max(0.0, static_cast<double>(choices.size() - Visible())));
    target = std::round(target);
    if (!frames.isActive()) {
        frames.start();
    }
}

void ChoicePicker::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const qint64 t = clock.elapsed();
    const double shown = leaving ? 1 - EaseOut(static_cast<double>(t - left_at) / CloseTime) : EaseOut(static_cast<double>(t) / OpenTime);

    // The window behind, blurred and dimmed as the card comes.
    p.setOpacity(shown);
    if (colours.blur && !behind.isNull()) {
        p.drawPixmap(rect(), behind);
    }
    p.fillRect(rect(), QColor(0, 0, 0, colours.blur ? 110 : 145));

    // The card rises a little into place.
    const QRectF card = Card();
    p.translate(0, (1 - shown) * 14 * Scale);
    QPainterPath outline;
    const double round = 12 * Scale;
    outline.addRoundedRect(card, round, round);
    for (int i = 4; i >= 1; --i) {
        QPainterPath shade;
        const double by = i * 4 * Scale;
        shade.addRoundedRect(card.adjusted(-by, -by + 8 * Scale, by, by + 8 * Scale), round + by, round + by);
        p.fillPath(shade, QColor(0, 0, 0, 16));
    }
    p.fillPath(outline, colours.card);
    p.setPen(QPen(WithAlpha(colours.rule, 0.8), 1));
    p.drawPath(outline);

    // The title, and a rule under it.
    p.setFont(colours.title);
    p.setPen(colours.dim);
    p.drawText(QRectF(card.left() + Side(), card.top(), card.width() - 2 * Side(), Head()), Qt::AlignLeft | Qt::AlignVCenter,
               title.toUpper());
    p.fillRect(QRectF(card.left() + Side(), card.top() + Head() - 1, card.width() - 2 * Side(), 1), colours.rule);

    // The rows, clipped to the list.
    const QRectF list(card.left(), card.top() + Head(), card.width(), Visible() * RowHeight());
    p.save();
    p.setClipRect(list);
    p.setFont(colours.row);
    const int first = std::max(0, static_cast<int>(std::floor(top)));
    const int last = std::min(static_cast<int>(choices.size()) - 1, first + Visible() + 1);
    for (int i = first; i <= last; ++i) {
        const QRectF row(list.left() + 8 * Scale, list.top() + (i - top) * RowHeight() + 3 * Scale, list.width() - 16 * Scale, RowHeight() - 6 * Scale);
        if (i == chosen) {
            QPainterPath lit;
            lit.addRoundedRect(row, 7 * Scale, 7 * Scale);
            p.fillPath(lit, WithAlpha(colours.accent, 0.16));
        }
        if (i == now) {
            p.setPen(Qt::NoPen);
            p.setBrush(colours.accent);
            p.drawEllipse(QPointF(row.left() + 18 * Scale, row.center().y()), 4 * Scale, 4 * Scale);
        }
        p.setPen(i == now || i == chosen ? colours.sel : colours.txt);
        p.drawText(row.adjusted(36 * Scale, 0, -14 * Scale, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(colours.row).elidedText(choices[i], Qt::ElideRight, row.width() - 50 * Scale));
    }
    p.restore();

    // A thin bar where the list is longer than the card.
    if (choices.size() > Visible()) {
        const double share = static_cast<double>(Visible()) / choices.size();
        const double h = list.height() * share;
        const double y = list.top() + (list.height() - h) * (top / std::max(1.0, static_cast<double>(choices.size() - Visible())));
        QPainterPath bar;
        bar.addRoundedRect(QRectF(card.right() - 8 * Scale, y + 4 * Scale, 3 * Scale, h - 8 * Scale), 1.5 * Scale, 1.5 * Scale);
        p.fillPath(bar, WithAlpha(colours.dim, 0.6));
    }

    p.setFont(colours.title);
    p.setPen(WithAlpha(colours.dim, 0.8));
    p.drawText(QRectF(card.left() + Side(), card.bottom() - Foot(), card.width() - 2 * Side(), Foot()), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("ENTER SELECT · ESC BACK"));
}

} // namespace Home
