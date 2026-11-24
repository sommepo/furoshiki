// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "conversion_box.h"

#include <algorithm>
#include <cmath>
#include <QEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QTextOption>

namespace Home {

namespace {

constexpr double BoxW = 620;
constexpr double BoxH = 322;

/// Text in a rectangle of the sheet, wrapped.
void Say(QPainter& p, const QFont& font, const QRectF& area, const QString& text, const QColor& colour) {
    p.setFont(font);
    p.setPen(colour);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    p.drawText(area, text, option);
}

/// A line of text, elided to `room`.
void Line(QPainter& p, const QFont& font, double x, double y, double room, const QString& text, const QColor& colour) {
    p.setFont(font);
    p.setPen(colour);
    p.drawText(QPointF(x, y), QFontMetricsF(font).elidedText(text, Qt::ElideRight, room));
}

} // namespace

DictionaryConversionBox::DictionaryConversionBox(QWidget* parent, DictionaryShelf* shelf_) : QWidget(parent), shelf(shelf_) {
    hide();
    parent->installEventFilter(this); // its size: the box covers the whole window
    ticks.setInterval(250);
    connect(&ticks, &QTimer::timeout, this, [this] {
        const bool running = shelf->Converting();
        now = shelf->Conversion();
        if (!running && !now.finished) {
            // Nothing being moved (never was, or a start with nothing left): it waits no more.
            ticks.stop();
            return;
        }
        if (!running && now.finished && !finished) {
            // Over: the result is said, put away or not, until OK.
            finished = true;
            shelf->Refresh();
            put_away = false;
            if (mode != Mode::Removed && mode != Mode::Confirm) {
                mode = Mode::Finished;
                chosen = 1;
            }
        }
        if (!put_away && !isVisible() && (!may_show || may_show())) {
            Place();
            show();
            raise();
            if (shown_changed) {
                shown_changed(true);
            }
        }
        if (finished && (isVisible() || mode == Mode::Removed)) {
            ticks.stop();
        }
        update();
    });
}

void DictionaryConversionBox::SetStyle(const DictionaryPanel::Style& style_) {
    style = style_;
    Place();
    update();
}

void DictionaryConversionBox::Watch() {
    if (finished && !put_away && !isVisible() && (!may_show || may_show())) {
        Place();
        show();
        raise();
        if (shown_changed) {
            shown_changed(true);
        }
        update();
        return;
    }
    if (!finished && !ticks.isActive() && shelf->Converting()) {
        ticks.start();
    }
}

void DictionaryConversionBox::Reopen() {
    if (!shelf->Converting() && !finished) {
        return;
    }
    put_away = false;
    Place();
    show();
    raise();
    if (shown_changed) {
        shown_changed(true);
    }
    if (!ticks.isActive() && !finished) {
        ticks.start();
    }
}

void DictionaryConversionBox::Close() {
    hide();
    if (finished) {
        // OK: the result is put away for good.
        if (shown_changed) {
            shown_changed(false);
        }
        deleteLater();
        return;
    }
    put_away = true; // Hide: the strip at the foot goes on saying it, and brings this back
    if (shown_changed) {
        shown_changed(false);
    }
}

QString DictionaryConversionBox::Label(int which) const {
    switch (mode) {
    case Mode::Running:
        return which == 0 ? QStringLiteral("REMOVE & RE-IMPORT…") : QStringLiteral("HIDE");
    case Mode::Confirm:
        return which == 0 ? QStringLiteral("CANCEL") : QStringLiteral("REMOVE");
    case Mode::Removed:
        return which == 0 ? (import_ask ? QStringLiteral("IMPORT…") : QString()) : QStringLiteral("OK");
    case Mode::Finished:
        return which == 0 ? QString() : QStringLiteral("OK");
    }
    return {};
}

void DictionaryConversionBox::Press(int which) {
    if (Label(which).isEmpty()) {
        return;
    }
    switch (mode) {
    case Mode::Running:
        if (which == 0) {
            mode = Mode::Confirm; // asked first: it deletes what the user has
            chosen = 0;
            update();
        } else {
            Close();
        }
        return;
    case Mode::Confirm:
        if (which == 0) {
            if (finished) {
                mode = Mode::Finished;
                chosen = 1;
            } else {
                mode = Mode::Running;
                chosen = 1;
            }
            update();
            return;
        }
        removed = shelf->AbandonConversion();
        now = shelf->Conversion();
        finished = true;
        ticks.stop();
        if (removed == 0 && now.finished && !shelf->Converting()) {
            // Conversion already completed right before Remove was clicked.
            mode = Mode::Finished;
            chosen = 1;
            update();
            return;
        }
        mode = Mode::Removed;
        chosen = import_ask ? 0 : 1;
        update();
        return;
    case Mode::Removed:
    case Mode::Finished:
        Close();
        if (which == 0 && import_ask) {
            import_ask();
        }
        return;
    }
}

bool DictionaryConversionBox::Key(int key) {
    if (!isVisible()) {
        return false;
    }
    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
        // Between the two buttons, where there are two.
        if (!Label(key == Qt::Key_Left ? 0 : 1).isEmpty()) {
            chosen = key == Qt::Key_Left ? 0 : 1;
            update();
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Press(chosen);
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        // Back: hide while it runs, the question not asked, the result put away.
        Press(mode == Mode::Confirm ? 0 : 1);
        return true;
    default:
        return true; // the page under it waits while the box is up
    }
}

bool DictionaryConversionBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        Place();
    }
    return QWidget::eventFilter(watched, event);
}

void DictionaryConversionBox::Place() {
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
    }
}

QRectF DictionaryConversionBox::Panel() const {
    const double s = std::max(0.01, style.scale);
    const double w = BoxW * s;
    const double h = BoxH * s;
    return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
}

QRectF DictionaryConversionBox::SheetButton(int which) {
    return which == 0 ? QRectF(28, BoxH - 52, 204, 32) : QRectF(BoxW - 128, BoxH - 52, 100, 32);
}

QRectF DictionaryConversionBox::Button(int which) const {
    const QRectF panel = Panel();
    const double s = std::max(0.01, style.scale);
    const QRectF sheet = SheetButton(which);
    return QRectF(panel.left() + sheet.left() * s, panel.top() + sheet.top() * s, sheet.width() * s, sheet.height() * s);
}

void DictionaryConversionBox::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int which : {0, 1}) {
            if (Button(which).contains(event->position())) {
                chosen = which;
                Press(which);
                break;
            }
        }
    }
    event->accept(); // nothing under the box takes a click while it is up
}

void DictionaryConversionBox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    // The program dimmed under it: this is to be read first.
    p.fillRect(rect(), QColor(0, 0, 0, 120));
    const QRectF panel = Panel();
    p.fillRect(panel, style.raised.alpha() > 0 ? style.raised : style.ground);
    p.setPen(style.rule);
    p.drawRect(panel.adjusted(0.5, 0.5, -0.5, -0.5));
    p.fillRect(QRectF(panel.left(), panel.top(), panel.width(), 2), style.accent);

    p.save();
    p.translate(panel.topLeft());
    const double s = std::max(0.01, style.scale);
    p.scale(s, s);
    const double x = 28;
    const double room = BoxW - 2 * x;
    const int count = std::max(now.total, 0);

    if (mode == Mode::Confirm) {
        Line(p, style.head, x, 36, room, QStringLiteral("REMOVE AND RE-IMPORT DICTIONARIES?"), style.accent);
        Say(p, style.row, QRectF(x, 50, room, 150),
            QStringLiteral("This removes your %1 %2 from Furoshiki and deletes the old file. You can re-import "
                           "them from their .zip files by choosing their folder. Only do this if you still have "
                           "those files.")
                .arg(count)
                .arg(count == 1 ? QStringLiteral("dictionary") : QStringLiteral("dictionaries")),
            style.ink);
    } else if (mode == Mode::Removed) {
        Line(p, style.head, x, 36, room, QStringLiteral("DICTIONARIES REMOVED"), style.muted);
        Say(p, style.row, QRectF(x, 50, room, 150),
            (removed == 1 ? QStringLiteral("1 dictionary was removed and the old file has been deleted. Choose Import… "
                                           "below to re-import now, or do it later in Settings › Dictionaries.")
                          : QStringLiteral("%1 dictionaries were removed and the old file has been deleted. Choose "
                                           "Import… below to re-import now, or do it later in Settings › Dictionaries.")
                                .arg(removed)),
            style.ink);
    } else if (!finished) {
        Line(p, style.head, x, 36, room, QStringLiteral("UPDATING DICTIONARIES"), style.muted);
        Say(p, style.row, QRectF(x, 50, room, 96),
            QStringLiteral("Furoshiki now uses a faster dictionary format. Your %1 %2 being converted once. Lookups "
                           "will keep using the old file until this finishes, so you can keep reading. If you close "
                           "Furoshiki, conversion will resume next time you open it.")
                .arg(count)
                .arg(count == 1 ? QStringLiteral("dictionary is") : QStringLiteral("dictionaries are")),
            style.ink);
        // Dictionary n of N, its name, and the bar for the whole move.
        const int at = std::min(now.done + 1, std::max(count, 1));
        Line(p, style.mono_small, x, 176, room, QStringLiteral("DICTIONARY %1 OF %2").arg(at).arg(count), style.muted);
        Line(p, style.row, x, 200, room, now.title, style.ink);
        double share = 0;
        if (count > 0) {
            const double part = now.steps > 0 ? std::clamp(static_cast<double>(now.step) / now.steps, 0.0, 1.0) : 0.0;
            share = std::clamp((now.done + part) / count, 0.0, 1.0);
        }
        const QRectF track(x, 216, room, 6);
        p.fillRect(track, style.rule);
        p.fillRect(QRectF(track.left(), track.top(), track.width() * share, track.height()), style.accent);
        Line(p, style.mono_small, x, 242, room,
             QStringLiteral("%1%").arg(static_cast<int>(std::floor(share * 100))) +
                 (now.resumed > 0 ? QStringLiteral(" · %1 ALREADY CONVERTED").arg(now.resumed) : QString()),
             style.muted);
    } else if (now.failed.isEmpty()) {
        Line(p, style.head, x, 36, room, QStringLiteral("DICTIONARIES UPDATED"), style.muted);
        const QString took =
            now.seconds < 1.0 ? QStringLiteral("under a second") : QStringLiteral("%1 s").arg(now.seconds, 0, 'f', 0);
        Say(p, style.row, QRectF(x, 50, room, 120),
            (count == 1 ? QStringLiteral("Your dictionary was updated to the new format in %1. The old file has been "
                                         "deleted. Lookups and imports will use the new format from now on.")
                              .arg(took)
                        : QStringLiteral("All %1 dictionaries were updated to the new format in %2. The old file has "
                                         "been deleted. Lookups and imports will use the new format from now on.")
                              .arg(count)
                              .arg(took)),
            style.ink);
    } else {
        Line(p, style.head, x, 36, room, QStringLiteral("COULDN'T UPDATE SOME DICTIONARIES"), style.accent);
        Say(p, style.row, QRectF(x, 50, room, 96),
            QStringLiteral("Your dictionaries will keep working from the old file, and Furoshiki will try again next "
                           "time you open it. If this keeps happening, delete these in Settings › Dictionaries and "
                           "re-import them:"),
            style.ink);
        Say(p, style.row, QRectF(x, 150, room, 100), now.failed.join(QStringLiteral(" · ")), style.accent);
    }
    // The buttons (Button() is their place in the window): the one the keys are on filled.
    for (int which : {0, 1}) {
        const QString said = Label(which);
        if (said.isEmpty()) {
            continue;
        }
        const QRectF button = SheetButton(which);
        if (which == chosen) {
            p.fillRect(button, style.ink);
        }
        p.setPen(style.ink);
        p.setBrush(Qt::NoBrush);
        p.drawRect(button);
        p.setFont(style.mono);
        p.setPen(which == chosen ? (style.raised.alpha() > 0 ? style.raised : style.ground) : style.ink);
        p.drawText(button, Qt::AlignCenter, said);
    }
    p.restore();
}

} // namespace Home
