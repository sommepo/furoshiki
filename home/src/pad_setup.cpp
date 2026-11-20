// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include "pad_setup.h"

namespace Home {

namespace {

constexpr double SheetW = 1280, SheetH = 720;
constexpr double Left = 160, Right = 1120;
constexpr qint64 OpenTime = 180, StepTime = 160;

QColor Faded(QColor colour, double alpha) {
    colour.setAlphaF(std::clamp(alpha, 0.0, 1.0) * colour.alphaF());
    return colour;
}

const char* const StepNames[] = {"Letters", "Check", "Shortcuts"};

/// The three layouts as rows, in the order shown.
struct LayoutRow {
    int family;
    const char* said;
};
constexpr LayoutRow LayoutRows[] = {
    {PadNames::Xbox, "A at the bottom: Xbox letters"},
    {PadNames::Nintendo, "A on the right: Nintendo letters"},
    {PadNames::PlayStation, "My controller has × ○ △ □ instead"},
};

} // namespace

PadSetup::PadSetup(QWidget* over, Look look_, QString pad_name_, PadNames::Names names_, int reported_, Shortcuts shortcuts_,
                   std::function<void(int)> chosen_, std::function<void(bool)> finished_)
    : QWidget(over), look(std::move(look_)), pad_name(std::move(pad_name_)), names(std::move(names_)), reported(reported_),
      chosen(std::move(chosen_)), finished(std::move(finished_)), shortcuts(std::move(shortcuts_)) {
    setObjectName(QStringLiteral("pad-setup"));
    answer = reported;
    playstation_row = reported == PadNames::PlayStation;
    setGeometry(over->rect());
    over->installEventFilter(this);
    setFocusPolicy(Qt::StrongFocus);
    frames.setInterval(16);
    connect(&frames, &QTimer::timeout, this, [this] {
        const qint64 now = clock.elapsed();
        if (now > OpenTime && now - step_at > StepTime) {
            frames.stop();
        }
        update();
    });
    clock.start();
    frames.start();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

double PadSetup::Scale() const {
    return std::min(width() / SheetW, height() / SheetH);
}

QPointF PadSetup::ToSheet(const QPointF& at) const {
    const double s = Scale();
    return {(at.x() - (width() - SheetW * s) / 2) / s, (at.y() - (height() - SheetH * s) / 2) / s};
}

bool PadSetup::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

PadNames::Names PadSetup::Shown() const {
    return PadNames::WithLayout(names, answer);
}

void PadSetup::Next() {
    if (done) {
        return;
    }
    if (step == 0) {
        step = 1;
    } else if (step == 1) {
        if (chosen) {
            chosen(answer); // kept once it is checked
        }
        step = 2;
        choice = 0;
    } else {
        Finish(false); // the shortcuts are kept as each is learnt
        return;
    }
    note.clear();
    step_at = clock.elapsed();
    frames.start();
    update();
}

void PadSetup::Back() {
    if (done || step == 0) {
        return;
    }
    step -= 1;
    note.clear();
    step_at = clock.elapsed();
    frames.start();
    update();
}

void PadSetup::Finish(bool set_up) {
    if (done) {
        return;
    }
    done = true;
    hide();
    if (finished) {
        finished(set_up);
    }
    deleteLater();
}

void PadSetup::Learnt(const QString& input) {
    const QString action = shortcuts.actions.value(choice);
    const QString name = shortcuts.name ? shortcuts.name(input, Shown()) : input;
    // A button reading or the popup already has: said once, taken if pressed again.
    const QString uses = shortcuts.conflict ? shortcuts.conflict(input) : QString();
    if (!uses.isEmpty() && pending != input) {
        pending = input;
        note = QStringLiteral("%1 %2. Press it again to use it anyway, or press another.").arg(name, uses);
        if (!shortcuts.free.isEmpty()) {
            note += QStringLiteral(" Free: ") + shortcuts.free + QLatin1Char('.');
        }
        return;
    }
    learning = false;
    pending.clear();
    if (shortcuts.assign && shortcuts.assign(action, input)) {
        note = QStringLiteral("%1: %2").arg(shortcuts.label ? shortcuts.label(action) : action, name);
    } else {
        note = QStringLiteral("Couldn't put it on %1. Try another button.").arg(name);
    }
}

void PadSetup::Released(const QString& input) {
    if (done || !learning) {
        return;
    }
    learn_down.remove(input);
    // One button, or two held together: taken when all are let go.
    if (learn_down.isEmpty() && !learn_got.isEmpty()) {
        const QString got = learn_got.size() >= 2 ? learn_got[0] + QLatin1Char('+') + learn_got[1] : learn_got[0];
        learn_got.clear();
        Learnt(got);
        update();
    }
}

void PadSetup::Pressed(const QString& input) {
    if (done) {
        return;
    }
    if (learning) {
        // The back button (by place: the right one) stops learning; any other is learnt.
        if (input == QStringLiteral("east") && learn_got.isEmpty()) {
            learning = false;
            pending.clear();
            note = QStringLiteral("Nothing changed.");
            update();
            return;
        }
        learn_down.insert(input);
        if (!learn_got.contains(input)) {
            learn_got.append(input);
        }
        update();
        return;
    }
    if (step == 2) {
        const int rows = static_cast<int>(shortcuts.actions.size()) + 1; // and Done
        if (input == QStringLiteral("up") || input == QStringLiteral("down")) {
            choice = (choice + (input == QStringLiteral("down") ? 1 : rows - 1)) % rows;
            note.clear();
            pending.clear();
        } else if (input == QStringLiteral("south")) {
            if (choice >= rows - 1) {
                Finish(false);
                return;
            }
            learning = true; // the confirm button's own release is not learnt (nothing held yet)
            learn_down.clear();
            learn_got.clear();
            note.clear();
        } else if (input == QStringLiteral("east") || input == QStringLiteral("left")) {
            Back();
            return;
        } else if (input == QStringLiteral("right")) {
            Finish(false);
            return;
        }
        update();
        return;
    }
    // The d-pad works every step: right goes on, left goes back.
    if (input == QStringLiteral("right")) {
        Next();
        return;
    }
    if (input == QStringLiteral("left")) {
        Back();
        return;
    }
    const bool face = input == QStringLiteral("south") || input == QStringLiteral("east") || input == QStringLiteral("west") ||
                      input == QStringLiteral("north");
    if (step == 0) {
        if (input == QStringLiteral("up") || input == QStringLiteral("down")) {
            // Up and down go through the three rows in turn (the maintainer, 2026-11-20: down
            // went from the first straight to the third); right then goes on with the one marked.
            static const int rows[] = {PadNames::Xbox, PadNames::Nintendo, PadNames::PlayStation};
            int at = answer == PadNames::Nintendo ? 1 : answer == PadNames::PlayStation ? 2 : 0;
            at = std::clamp(at + (input == QStringLiteral("down") ? 1 : -1), 0, 2);
            answer = rows[at];
            playstation_row = answer == PadNames::PlayStation;
            pressed = true;
            note.clear();
        } else if (face && playstation_row) {
            answer = PadNames::PlayStation; // the row chosen: any face button says yes
            pressed = true;
        } else if (face) {
            const int family = PadNames::LayoutFromPress(input);
            if (family >= 0) {
                answer = family;
                pressed = true;
                note.clear();
            } else {
                note = QStringLiteral("That one isn't A. A is the bottom or the right button.");
            }
        }
    } else if (step == 1) {
        // The letters are known: the bottom button confirms, the right one goes back.
        if (input == QStringLiteral("south")) {
            Next();
            return;
        }
        if (input == QStringLiteral("east")) {
            Back();
            return;
        }
    }
    update();
}

void PadSetup::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        if (learning) {
            learning = false;
            note = QStringLiteral("Nothing changed.");
            update();
            return;
        }
        Finish(false);
        return;
    case Qt::Key_Up:
        Pressed(QStringLiteral("up"));
        return;
    case Qt::Key_Down:
        Pressed(QStringLiteral("down"));
        return;
    case Qt::Key_Left:
    case Qt::Key_Backspace:
        Back();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (step == 2) {
            Pressed(QStringLiteral("south")); // a row learnt from the controller, or Done
            return;
        }
        Next();
        return;
    case Qt::Key_Right:
        Next();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void PadSetup::mousePressEvent(QMouseEvent* event) {
    const QPointF at = ToSheet(event->position());
    for (const auto& [area, what] : std::as_const(hits)) {
        if (!area.contains(at)) {
            continue;
        }
        if (what == 10) {
            Next();
        } else if (what == 11) {
            Back();
        } else if (what >= 20 && !learning) {
            choice = what - 20;
            Pressed(QStringLiteral("south"));
        } else if (what >= 0 && what < 3) {
            answer = LayoutRows[what].family;
            playstation_row = answer == PadNames::PlayStation;
            pressed = true;
            update();
        }
        return;
    }
}

void PadSetup::PaintSteps(QPainter& p) {
    // The three steps along a row, the current one marked; those done in the ink, those to come muted.
    const QFont face = look.font(2, 12, 0.14);
    p.setFont(face);
    double x = Left;
    for (int i = 0; i < 3; ++i) {
        const QString text = QStringLiteral("%1  %2").arg(i + 1).arg(QString::fromLatin1(StepNames[i]).toUpper());
        const double w = QFontMetricsF(face).horizontalAdvance(text) + 36;
        const QRectF box(x, 140, w, 34);
        if (i == step) {
            p.setPen(Qt::NoPen);
            p.setBrush(look.accent);
            p.drawRoundedRect(box, 6, 6);
            p.setPen(look.ground);
        } else {
            p.setPen(QPen(Faded(look.ink, i < step ? 0.7 : 0.3), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box, 6, 6);
            p.setPen(i < step ? look.ink : look.muted);
        }
        p.drawText(box, Qt::AlignCenter, text);
        x += w;
        if (i < 2) {
            p.setPen(look.muted);
            p.drawText(QRectF(x, 140, 40, 34), Qt::AlignCenter, QStringLiteral("→"));
            x += 40;
        }
    }
}

void PadSetup::PaintFoot(QPainter& p, const QList<QPair<QString, QString>>& keys) {
    // The keys at the foot: each its button in a box, then what it does.
    p.fillRect(QRectF(Left, 640, Right - Left, 1), look.rule);
    const QFont cap = look.font(2, 12, 0.08);
    const QFont word = look.font(2, 12, 0.14);
    double x = Left;
    for (const auto& [button, does] : keys) {
        p.setFont(cap);
        const double w = QFontMetricsF(cap).horizontalAdvance(button) + 18;
        const QRectF box(x, 656, w, 26);
        p.setPen(QPen(look.ink, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 4, 4);
        p.drawText(box, Qt::AlignCenter, button);
        p.setFont(word);
        p.setPen(look.muted);
        const double ww = QFontMetricsF(word).horizontalAdvance(does.toUpper());
        p.drawText(QRectF(box.right() + 10, 656, ww + 4, 26), Qt::AlignVCenter | Qt::AlignLeft, does.toUpper());
        x = box.right() + 10 + ww + 30;
    }
}

void PadSetup::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const double shown = std::clamp(static_cast<double>(clock.elapsed()) / OpenTime, 0.0, 1.0);
    p.setOpacity(shown);
    p.fillRect(rect(), look.ground);
    const double s = Scale();
    p.translate((width() - SheetW * s) / 2, (height() - SheetH * s) / 2 + (1 - shown) * 10 * s);
    p.scale(s, s);
    hits.clear();

    const QFont small = look.font(2, 11, 0.14);
    p.setFont(small);
    p.setPen(look.muted);
    p.drawText(QPointF(Left, 100), (QStringLiteral("New controller · ") + (pad_name.isEmpty() ? QStringLiteral("Controller") : pad_name)).toUpper());
    p.fillRect(QRectF(Left, 114, Right - Left, 1), look.rule);
    PaintSteps(p);

    const PadNames::Names letters = Shown();
    const QString confirm = PadNames::ConfirmName(letters);
    const QString back = PadNames::BackName(letters);
    const double slide = std::clamp(1.0 - static_cast<double>(clock.elapsed() - step_at) / StepTime, 0.0, 1.0);
    p.translate(slide * 24, 0);
    p.setOpacity(shown * (1.0 - slide * 0.8));

    if (step == 0) {
        p.setPen(look.ink);
        p.setFont(look.font(0, 30, 0.01));
        p.drawText(QRectF(Left, 200, Right - Left, 50), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Press the button marked A"));
        p.setFont(look.font(1, 18, 0));
        p.setPen(look.muted);
        p.drawText(QRectF(Left, 252, Right - Left, 30), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Any face button answers. Press another to change the answer."));
        const QFont row_face = look.font(1, 18, 0);
        double y = 304;
        for (int i = 0; i < static_cast<int>(std::size(LayoutRows)); ++i) {
            const QRectF area(Left, y, Right - Left, 46);
            const bool on = LayoutRows[i].family == answer;
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(Faded(look.ink, 0.08));
                p.drawRoundedRect(area, 6, 6);
                p.fillRect(QRectF(Left, y + 8, 3, 30), look.accent);
            }
            p.setFont(row_face);
            p.setPen(on ? look.ink : look.muted);
            p.drawText(area.adjusted(22, 0, -16, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(LayoutRows[i].said));
            p.setFont(look.font(2, 10, 0.12));
            p.setPen(look.muted);
            if (LayoutRows[i].family == PadNames::PlayStation) {
                p.drawText(area.adjusted(0, 0, -16, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("↓ CHOOSE, THEN ANY FACE BUTTON"));
            } else if (LayoutRows[i].family == reported && !pressed) {
                p.drawText(area.adjusted(0, 0, -16, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("WHAT THE CONTROLLER REPORTS"));
            }
            hits.append({area, i});
            y += 52;
        }
        p.setFont(look.font(1, 20, 0));
        p.setPen(note.isEmpty() ? look.ink : look.accent);
        p.drawText(QRectF(Left, 480, Right - Left, 34), Qt::AlignLeft | Qt::AlignVCenter,
                   note.isEmpty() ? (pressed ? PadNames::LayoutAnswer(answer) : QStringLiteral("Not pressed yet: ") + PadNames::LayoutAnswer(answer))
                                  : note);
        p.setOpacity(shown);
        p.resetTransform();
        p.translate((width() - SheetW * s) / 2, (height() - SheetH * s) / 2);
        p.scale(s, s);
        PaintFoot(p, {{QStringLiteral("A"), QStringLiteral("Answer")},
                      {QStringLiteral("↑ ↓"), QStringLiteral("× ○ △ □ row")},
                      {QStringLiteral("→"), QStringLiteral("Next")},
                      {QStringLiteral("Esc"), QStringLiteral("Close")}});
        return;
    }

    if (step == 1) {
        p.setPen(look.ink);
        p.setFont(look.font(0, 30, 0.01));
        p.drawText(QRectF(Left, 200, Right - Left, 50), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Is this right?"));
        // The four face buttons in their places, with this controller's letters.
        const QPointF centre(Left + 110, 400);
        const double off = 58, r = 26;
        const struct {
            QPointF at;
            QString letter;
        } faces[] = {
            {centre + QPointF(0, -off), letters.north},
            {centre + QPointF(off, 0), letters.east},
            {centre + QPointF(0, off), letters.south},
            {centre + QPointF(-off, 0), letters.west},
        };
        p.setFont(look.font(1, 22, 0));
        for (const auto& one : faces) {
            p.setPen(QPen(look.ink, 1.4));
            p.setBrush(look.raised);
            p.drawEllipse(one.at, r, r);
            p.drawText(QRectF(one.at.x() - r, one.at.y() - r, 2 * r, 2 * r), Qt::AlignCenter, one.letter);
        }
        // Beside them, what each is: a column a button (bottom, right, left, top), a row a console.
        const double table_left = Left + 250;
        const double col_w = 115, name_w = Right - table_left - 4 * col_w;
        const QString places[] = {QStringLiteral("Bottom"), QStringLiteral("Right"), QStringLiteral("Left"), QStringLiteral("Top")};
        const QString heads[] = {letters.south, letters.east, letters.west, letters.north};
        p.setFont(look.font(2, 11, 0.12));
        for (int c = 0; c < 4; ++c) {
            p.setPen(look.muted);
            p.drawText(QRectF(table_left + name_w + c * col_w, 268, col_w, 22), Qt::AlignLeft | Qt::AlignVCenter,
                       (places[c] + QStringLiteral(" ") + heads[c]).toUpper());
        }
        p.fillRect(QRectF(table_left, 294, Right - table_left, 1), look.rule);
        double y = 300;
        const QFont cell = look.font(1, 17, 0);
        for (const PadNames::ConsoleFaces& row : PadNames::FacesOnConsoles()) {
            p.setFont(cell);
            p.setPen(look.muted);
            p.drawText(QRectF(table_left, y, name_w, 40), Qt::AlignLeft | Qt::AlignVCenter, row.console);
            p.setPen(look.ink);
            const QString cells[] = {row.south, row.east, row.west, row.north};
            for (int c = 0; c < 4; ++c) {
                p.drawText(QRectF(table_left + name_w + c * col_w, y, col_w, 40), Qt::AlignLeft | Qt::AlignVCenter, cells[c]);
            }
            y += 42;
        }
        p.setOpacity(shown);
        p.resetTransform();
        p.translate((width() - SheetW * s) / 2, (height() - SheetH * s) / 2);
        p.scale(s, s);
        PaintFoot(p, {{QStringLiteral("→ or ") + confirm, QStringLiteral("Yes")},
                      {QStringLiteral("← or ") + back, QStringLiteral("Change letters")},
                      {QStringLiteral("Esc"), QStringLiteral("Close")}});
        hits.append({QRectF(Left, 650, 200, 40), 10});
        hits.append({QRectF(Left + 220, 650, 300, 40), 11});
        return;
    }

    // Shortcuts: the reading shortcuts, one row each with its button, learnt here.
    p.setPen(look.ink);
    p.setFont(look.font(0, 30, 0.01));
    p.drawText(QRectF(Left, 196, Right - Left, 44), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Reading shortcuts"));
    p.setFont(look.font(1, 16, 0));
    p.setPen(look.muted);
    p.drawText(QRectF(Left, 238, Right - Left, 26), Qt::AlignLeft | Qt::AlignVCenter,
               learning ? QStringLiteral("Press the button for %1, or two held together. %2 cancels.")
                              .arg(shortcuts.label ? shortcuts.label(shortcuts.actions.value(choice)) : QString(), back)
                        : QStringLiteral("Choose a row and press %1 to set its button. Free buttons: %2.").arg(confirm, shortcuts.free));
    const double row_h = 36;
    double y = 276;
    const int rows = static_cast<int>(shortcuts.actions.size()) + 1;
    for (int i = 0; i < rows; ++i) {
        const bool done_row = i == rows - 1;
        const QRectF area(Left, y, Right - Left, row_h - 4);
        const bool on = i == choice;
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(learning ? QBrush(Faded(look.accent, 0.25)) : QBrush(Faded(look.ink, 0.08)));
            p.drawRoundedRect(area, 6, 6);
            p.fillRect(QRectF(Left, y + 6, 3, row_h - 16), look.accent);
        }
        p.setFont(look.font(1, 18, 0));
        p.setPen(on ? look.ink : Faded(look.ink, 0.75));
        const QString action = shortcuts.actions.value(i);
        p.drawText(area.adjusted(22, 0, -16, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   done_row ? QStringLiteral("Done") : (shortcuts.label ? shortcuts.label(action) : action));
        if (!done_row) {
            const QString now = on && learning ? (learn_got.isEmpty() ? QStringLiteral("PRESS A BUTTON")
                                                                      : (shortcuts.name ? shortcuts.name(learn_got.join(QLatin1Char('+')), letters) : learn_got.join(QLatin1Char('+'))))
                                               : (shortcuts.current ? shortcuts.current(action, letters) : QStringLiteral("None"));
            p.setFont(look.font(2, 12, 0.08));
            p.setPen(now == QStringLiteral("None") ? look.muted : look.ink);
            p.drawText(area.adjusted(0, 0, -16, 0), Qt::AlignRight | Qt::AlignVCenter, now.toUpper());
        }
        hits.append({area, 20 + i});
        y += row_h;
    }
    if (!note.isEmpty()) {
        p.setFont(look.font(1, 16, 0));
        p.setPen(look.accent);
        p.drawText(QRectF(Left, y + 4, Right - Left, 50), Qt::AlignLeft | Qt::TextWordWrap, note);
    }
    p.setOpacity(shown);
    p.resetTransform();
    p.translate((width() - SheetW * s) / 2, (height() - SheetH * s) / 2);
    p.scale(s, s);
    if (learning) {
        PaintFoot(p, {{QStringLiteral("Any button"), QStringLiteral("Set")}, {back, QStringLiteral("Cancel")}});
    } else {
        PaintFoot(p, {{QStringLiteral("↑ ↓"), QStringLiteral("Choose")},
                      {confirm, QStringLiteral("Set button")},
                      {QStringLiteral("→"), QStringLiteral("Done")},
                      {QStringLiteral("← or ") + back, QStringLiteral("Back")},
                      {QStringLiteral("Esc"), QStringLiteral("Close")}});
    }
}

} // namespace Home
