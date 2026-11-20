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
#include "controller_screen.h"

namespace Home {

namespace {

constexpr double SheetW = 1280, SheetH = 720;
constexpr double Left = 40, Right = 1240;
constexpr double ListLeft = 690, ListTop = 132, RowH = 29, ListBottom = 626;
constexpr qint64 OpenTime = 180, CloseTime = 120, LearnWait = 6000, GlowTime = 450, WizardWait = 10000;

QColor Faded(QColor colour, double alpha) {
    colour.setAlphaF(std::clamp(alpha, 0.0, 1.0) * colour.alphaF());
    return colour;
}

double Ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return 1 - std::pow(1 - t, 3);
}

} // namespace

ControllerScreen::ControllerScreen(QWidget* over, Look look_, ControllerMap::Store store_, QList<Console> consoles_,
                                   std::function<void()> closed_)
    : QWidget(over), look(std::move(look_)), store(std::move(store_)), consoles(std::move(consoles_)), closed(std::move(closed_)) {
    setObjectName(QStringLiteral("controller-screen"));
    for (const ControllerMap::Action& action : ControllerMap::Load().actions) {
        actions.append(action);
    }
    Build();
    setGeometry(over->rect());
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    frames.setInterval(16);
    connect(&frames, &QTimer::timeout, this, [this] {
        const qint64 now = clock.elapsed();
        if (leaving && now - left_at >= CloseTime) {
            frames.stop();
            hide();
            if (closed) {
                closed();
            }
            deleteLater();
            return;
        }
        if (!wizard_steps.isEmpty() && now - wizard_since >= WizardWait) {
            WizardStep({}); // not pressed in time: passed over
        }
        if (learning >= 0 && now - learn_since >= LearnWait) {
            Stop();
            Say(QStringLiteral("No button pressed, so nothing changed"));
        }
        update();
    });
    clock.start();
    frames.start();
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

double ControllerScreen::Scale() const {
    return std::min(width() / SheetW, height() / SheetH);
}

QPointF ControllerScreen::ToSheet(const QPointF& at) const {
    const double s = Scale();
    return {(at.x() - (width() - SheetW * s) / 2) / s, (at.y() - (height() - SheetH * s) / 2) / s};
}

QString ControllerScreen::ConsoleKey() const {
    return consoles.value(tab).key;
}

const ControllerMap::Action& ControllerScreen::ActionAt(int index) const {
    const int action = lines.value(index).action;
    return actions[std::clamp(action, 0, static_cast<int>(actions.size()) - 1)];
}

int ControllerScreen::Rows() const {
    return static_cast<int>(lines.size());
}

const ControllerMap::Pad& ControllerScreen::ShownPad() const {
    // Every tab draws the controller in the hand, with its own letters by place (the maintainer,
    // 2026-11-20: the bottom button is the same button on every tab): the Steam Controller while
    // the bridge has one, else a common gamepad.
    const ControllerMap::Data& data = ControllerMap::Load();
    if (hand_steam) {
        return ControllerMap::PadOf({});
    }
    const auto generic = data.pads.constFind(QStringLiteral("generic"));
    return generic != data.pads.constEnd() ? *generic : ControllerMap::PadOf({});
}

const ControllerMap::Pad& ControllerScreen::ConsolePad() const {
    // The console's own buttons and their names (Cross, ZL), where the tab is a console's.
    const QString console = ConsoleKey();
    if (!console.isEmpty() && ControllerMap::Load().pad_of.contains(console)) {
        return ControllerMap::PadOf(console);
    }
    return ShownPad();
}

QString ControllerScreen::LabelIn(const ControllerMap::Pad& pad, const QString& id) const {
    // The common gamepad has the letters of the pad in the hand (pad_names.h).
    if (pad.id == QStringLiteral("generic")) {
        if (const QString own = PadNames::Label(hand_names, id); !own.isEmpty()) {
            return own;
        }
    }
    return pad.LabelOf(id);
}

void ControllerScreen::SetHand(const QString& name, const PadNames::Names& names, bool steam) {
    if (name == hand_name && names == hand_names && steam == hand_steam) {
        return;
    }
    const bool redraw_list = steam != hand_steam || !(names == hand_names);
    hand_name = name;
    hand_names = names;
    hand_steam = steam;
    if (redraw_list && wizard_steps.isEmpty()) {
        Build();
    }
    update();
}

QString ControllerScreen::HandName(const QString& input) const {
    if (input.contains(QLatin1Char('+'))) { // two buttons held together
        QStringList names;
        for (const QString& part : input.split(QLatin1Char('+'))) {
            names.append(HandName(part));
        }
        return names.join(QStringLiteral(" + "));
    }
    // What the controller in the hand calls a button, whatever the tab draws: the Steam
    // Controller's own names (its grips) while the bridge has one, else the pad's letters.
    if (hand_steam) {
        return ControllerMap::NameOf(ControllerMap::PadOf({}), input);
    }
    if (const QString own = PadNames::Label(hand_names, input); !own.isEmpty()) {
        return own;
    }
    return ControllerMap::NameOf(ShownPad(), input);
}

QString ControllerScreen::GameName(const QString& id) const {
    static const QHash<QString, QString> words{{QStringLiteral("up"), QStringLiteral("Up")}, {QStringLiteral("down"), QStringLiteral("Down")},
                                               {QStringLiteral("left"), QStringLiteral("Left")}, {QStringLiteral("right"), QStringLiteral("Right")},
                                               {QStringLiteral("l3"), QStringLiteral("Left stick click")},
                                               {QStringLiteral("r3"), QStringLiteral("Right stick click")}};
    QString name = words.value(id);
    if (name.isEmpty()) {
        // A face button by what the console's core is handed for that place (the bottom one
        // is the Switch's and the 3DS's A, by name), not by where the console's own pad has it.
        name = PadNames::ConsoleFaceName(ConsoleKey(), id);
        if (name.isEmpty()) {
            name = LabelIn(ConsolePad(), id);
        }
        if (name.size() > 2) {
            name = name.left(1).toUpper() + name.mid(1).toLower(); // SELECT as Select
        }
    }
    return name;
}

void ControllerScreen::StartWizard() {
    Stop();
    wizard_steps = ControllerMap::GameButtons(ConsolePad());
    wizard_at = 0;
    wizard_got.clear();
    wizard_held.clear();
    wizard_note.clear();
    wizard_since = clock.elapsed();
    notice.clear();
    update();
}

void ControllerScreen::WizardStep(const QString& input) {
    if (wizard_steps.isEmpty()) {
        return;
    }
    const QString id = wizard_steps[wizard_at];
    if (input.isEmpty()) {
        wizard_got.remove(id);
    } else {
        wizard_got.insert(id, input);
        lit_at.insert(input, clock.elapsed());
    }
    wizard_note.clear();
    wizard_since = clock.elapsed();
    if (++wizard_at >= wizard_steps.size()) {
        EndWizard(true);
    }
    update();
}

void ControllerScreen::WizardBack() {
    if (wizard_steps.isEmpty() || wizard_at == 0) {
        return;
    }
    --wizard_at;
    wizard_got.remove(wizard_steps[wizard_at]);
    wizard_note.clear();
    wizard_since = clock.elapsed();
    update();
}

void ControllerScreen::EndWizard(bool keep) {
    if (wizard_steps.isEmpty()) {
        return;
    }
    const int asked = static_cast<int>(wizard_steps.size());
    const int count = static_cast<int>(wizard_got.size());
    wizard_steps.clear();
    if (keep && count > 0) {
        ControllerMap::SetGame(store, wizard_got, ConsoleKey());
        Say(QStringLiteral("%1 of %2 buttons set for %3").arg(count).arg(asked).arg(consoles.value(tab).name.toLower()));
    } else {
        Say(QStringLiteral("Nothing changed"));
    }
    wizard_got.clear();
    Build();
    update();
}

void ControllerScreen::Build() {
    lines.clear();
    const ControllerMap::Pad& pad = ConsolePad();
    lines.append({Line::Head, {}, ConsoleKey().isEmpty() ? QStringLiteral("Game buttons") : QStringLiteral("%1's buttons").arg(consoles.value(tab).name)});
    for (const QString& id : ControllerMap::GameButtons(pad)) {
        lines.append({Line::Game, id, GameName(id)});
    }
    lines.append({Line::Head, {}, QStringLiteral("Shortcuts")});
    for (int i = 0; i < actions.size(); ++i) {
        lines.append({Line::Shortcut, {}, actions[i].label, i});
    }
    row = std::clamp(row, 1, static_cast<int>(lines.size()) - 1);
    if (lines[row].kind == Line::Head) {
        ++row;
    }
    top = std::clamp(top, 0.0, std::max(0.0, static_cast<double>(lines.size()) - (ListBottom - ListTop) / RowH));
}

void ControllerScreen::Say(const QString& text) {
    notice = text;
    notice_at = clock.elapsed();
}

void ControllerScreen::ShowShortcuts() {
    for (int i = 0; i < Rows(); ++i) {
        if (lines[i].kind == Line::Shortcut) {
            row = i; // go down to it, so that its head shows above it
            const int visible = static_cast<int>((ListBottom - ListTop) / RowH);
            top = std::max(0, std::min(i - 1, Rows() - visible));
            update();
            return;
        }
    }
}

void ControllerScreen::ShowRow(int index) {
    const int step = index < row ? -1 : 1;
    index = std::clamp(index, 1, Rows() - 1);
    if (lines[index].kind == Line::Head) {
        index = std::clamp(index + step, 1, Rows() - 1); // a head is passed over
    }
    row = index;
    const int visible = static_cast<int>((ListBottom - ListTop) / RowH);
    if (row - 1 < top) {
        top = row - 1; // its head with it
    } else if (row >= top + visible) {
        top = row - visible + 1;
    }
    update();
}

void ControllerScreen::Learn(int index) {
    ShowRow(index);
    learning = row;
    learn_since = clock.elapsed();
    lit.clear();
    notice.clear();
}

void ControllerScreen::Stop() {
    learning = -1;
}

void ControllerScreen::Close() {
    if (leaving) {
        return;
    }
    leaving = true;
    left_at = clock.elapsed();
    setAttribute(Qt::WA_TransparentForMouseEvents);
    frames.start();
}

void ControllerScreen::Pressed(const QString& input, bool down, bool from_pad) {
    if (leaving) {
        return;
    }
    if (!wizard_steps.isEmpty()) {
        // The wizard has every button: one at a time, each an answer and nothing else.
        if (!down) {
            wizard_held.remove(input);
            return;
        }
        const bool busy = !wizard_held.isEmpty();
        const ControllerMap::Input* pressed = ControllerMap::InputById(input);
        if (pressed != nullptr && pressed->back == 0) {
            wizard_held.insert(input); // a back button says no release
        }
        if (busy) {
            return;
        }
        if (pressed == nullptr || pressed->back > 0) {
            wizard_note = QStringLiteral("Game buttons can't use back buttons");
            wizard_since = clock.elapsed();
            return;
        }
        WizardStep(input);
        return;
    }
    if (learning >= 0) {
        if (!down) {
            return;
        }
        const QString name = HandName(input);
        if (lines[learning].kind == Line::Game) {
            const Line line = lines[learning];
            Stop();
            if (!ControllerMap::AssignGame(store, line.id, input, ConsoleKey())) {
                Say(QStringLiteral("Game buttons can't use back buttons"));
                return;
            }
            Say(QStringLiteral("%1 on %2").arg(line.label, name));
            lit_at.insert(input, clock.elapsed());
            return;
        }
        const ControllerMap::Action& action = ActionAt(learning);
        const QString before = ControllerMap::ActionOn(store, input, ConsoleKey());
        Stop();
        if (!ControllerMap::Assign(store, action, input, ConsoleKey())) {
            Say(QStringLiteral("%1 can't use a back button").arg(action.label));
            return;
        }
        QString was;
        for (const ControllerMap::Action& other : actions) {
            if (other.id == before && before != action.id) {
                was = other.label;
            }
        }
        Say(was.isEmpty() ? QStringLiteral("%1 on %2").arg(action.label, name)
                          : QStringLiteral("%1 set to %2 (was %3)").arg(action.label, name, was));
        lit_at.insert(input, clock.elapsed());
        return;
    }
    if (down) {
        lit.insert(input);
        lit_at.insert(input, clock.elapsed());
    } else {
        lit.remove(input);
    }
    update();
    if (!from_pad || !down) {
        return;
    }
    // The controller works the screen: the d-pad along the rows and the tabs, A learns, B goes
    // back, X takes a button off, Y is all consoles' again, the shoulders change console.
    static const QHash<QString, int> keys{
        {QStringLiteral("up"), Qt::Key_Up},       {QStringLiteral("down"), Qt::Key_Down},
        {QStringLiteral("left"), Qt::Key_Left},   {QStringLiteral("right"), Qt::Key_Right},
        {QStringLiteral("south"), Qt::Key_Return}, {QStringLiteral("east"), Qt::Key_Escape},
        {QStringLiteral("west"), Qt::Key_Backspace}, {QStringLiteral("north"), Qt::Key_Y},
        {QStringLiteral("l1"), Qt::Key_Q},        {QStringLiteral("r1"), Qt::Key_E},
        {QStringLiteral("start"), Qt::Key_W}};
    if (const int key = keys.value(input, 0); key != 0) {
        Key(key);
    }
}

void ControllerScreen::Key(int key) {
    if (leaving) {
        return;
    }
    if (!wizard_steps.isEmpty()) {
        // The keyboard's own: Escape ends it as it was, Space passes one over, Backspace goes back one.
        if (key == Qt::Key_Escape) {
            EndWizard(false);
        } else if (key == Qt::Key_Space) {
            WizardStep({});
        } else if (key == Qt::Key_Backspace) {
            WizardBack();
        }
        return;
    }
    if (learning >= 0) {
        if (key == Qt::Key_Escape) {
            Stop();
            Say(QStringLiteral("Nothing changed"));
        }
        return;
    }
    switch (key) {
    case Qt::Key_Up:
        ShowRow(row - 1);
        break;
    case Qt::Key_Down:
        ShowRow(row + 1);
        break;
    case Qt::Key_Left:
    case Qt::Key_Q:
    case Qt::Key_PageUp:
        tab = (tab + static_cast<int>(consoles.size()) - 1) % static_cast<int>(consoles.size());
        notice.clear();
        Build();
        break;
    case Qt::Key_Right:
    case Qt::Key_E:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
        tab = (tab + 1) % static_cast<int>(consoles.size());
        notice.clear();
        Build();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Learn(row);
        break;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        if (lines[row].kind == Line::Game) {
            ControllerMap::ClearGame(store, lines[row].id, ConsoleKey());
            Say(QStringLiteral("%1 reset").arg(lines[row].label));
            break;
        }
        ControllerMap::Clear(store, ActionAt(row), ConsoleKey());
        if (store.save) {
            store.save();
        }
        Say(QStringLiteral("%1 has no button now").arg(ActionAt(row).label));
        break;
    case Qt::Key_W:
        StartWizard();
        break;
    case Qt::Key_Y:
        if (!ConsoleKey().isEmpty()) {
            ControllerMap::Reset(store, ConsoleKey());
            Build();
            Say(QStringLiteral("%1 uses the buttons of all consoles again").arg(consoles.value(tab).name));
        }
        break;
    case Qt::Key_Escape:
    case Qt::Key_F2:
        Close();
        break;
    default:
        break;
    }
    update();
}

void ControllerScreen::keyPressEvent(QKeyEvent* event) {
    Key(event->key());
}

void ControllerScreen::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = ToSheet(event->position());
    int now = -1;
    for (const Hit& hit : hits) {
        if (hit.area.contains(at) && (hit.kind == 1 || hit.kind == 2)) {
            now = hit.index;
        }
    }
    if (now != hover) {
        hover = now;
        update();
    }
    bool clickable = false;
    for (const Hit& hit : hits) {
        clickable = clickable || hit.area.contains(at);
    }
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ControllerScreen::mousePressEvent(QMouseEvent* event) {
    const QPointF at = ToSheet(event->position());
    if (!wizard_steps.isEmpty()) {
        for (const Hit& hit : hits) {
            if (hit.area.contains(at) && hit.kind == 6) {
                Key(hit.index == 0 ? Qt::Key_Backspace : hit.index == 1 ? Qt::Key_Space : Qt::Key_Escape);
            }
        }
        update();
        return;
    }
    if (learning >= 0) {
        Stop();
        Say(QStringLiteral("Nothing changed"));
        update();
        return;
    }
    // The hits are kept in drawing order: a row's Learn is after its row.
    const Hit* found = nullptr;
    for (const Hit& hit : hits) {
        if (hit.area.contains(at)) {
            found = &hit;
        }
    }
    if (found == nullptr) {
        return;
    }
    if (found->kind == 0) {
        tab = found->index;
        notice.clear();
        Build();
    } else if (found->kind == 1) {
        if (event->button() == Qt::RightButton) {
            ShowRow(found->index);
            Key(Qt::Key_Backspace);
        } else if (row == found->index) {
            Learn(found->index);
        } else {
            ShowRow(found->index);
        }
    } else if (found->kind == 2) {
        Learn(found->index);
    } else if (found->kind == 3) {
        Key(Qt::Key_Y);
    } else if (found->kind == 4) {
        Close();
    } else if (found->kind == 5) {
        StartWizard();
    }
    update();
}

void ControllerScreen::wheelEvent(QWheelEvent* event) {
    const int visible = static_cast<int>((ListBottom - ListTop) / RowH);
    top = std::clamp(top - event->angleDelta().y() / 120.0, 0.0, std::max(0.0, static_cast<double>(Rows() - visible)));
    top = std::round(top);
    update();
}

void ControllerScreen::PaintPad(QPainter& p, const QRectF& area) {
    const ControllerMap::Pad& pad = ShownPad();
    const double k = std::min(area.width() / 400.0, area.height() / 260.0);
    const QPointF origin(area.center().x() - 200 * k, area.center().y() - 130 * k);
    const qint64 now = clock.elapsed();
    QSet<QString> chosen;
    // The drawing is the controller in the hand: a console's button is lit where it is on that
    // controller (its source), a shortcut on the buttons it is on.
    QSet<QString> given;
    if (!wizard_steps.isEmpty()) {
        for (auto it = wizard_got.cbegin(); it != wizard_got.cend(); ++it) {
            given.insert(it.value());
        }
    } else if (lines.value(row).kind == Line::Game) {
        chosen.insert(ControllerMap::GameSource(store, lines[row].id, ConsoleKey()));
    } else {
        for (const ControllerMap::Binding& binding : ControllerMap::BindingsOf(store, ActionAt(row), ConsoleKey())) {
            chosen.insert(binding.input);
        }
    }
    const auto glow = [&](const QString& drawn) {
        if (lit.contains(drawn)) {
            return 1.0;
        }
        const qint64 at = lit_at.value(drawn, -GlowTime * 2);
        return std::clamp(1.0 - static_cast<double>(now - at) / GlowTime, 0.0, 1.0);
    };
    p.save();
    p.translate(origin);
    p.scale(k, k);
    const auto pen = [&](const QColor& colour, double width) {
        QPen made(colour, width / k * std::max(1.0, k / 1.5));
        made.setJoinStyle(Qt::RoundJoin);
        made.setCapStyle(Qt::RoundCap);
        return made;
    };
    for (const ControllerMap::Item& item : pad.draw) {
        switch (item.kind) {
        case ControllerMap::Item::Body:
        case ControllerMap::Item::Part:
            p.setBrush(look.raised);
            p.setPen(pen(look.ink, item.kind == ControllerMap::Item::Body ? 1.5 : 1.2));
            p.drawPath(item.path);
            break;
        case ControllerMap::Item::Line:
            p.setBrush(item.sunk ? QBrush(look.ground) : QBrush(Qt::NoBrush));
            p.setPen(pen(Faded(look.ink, item.strong ? 0.9 : 0.38), item.strong ? 1.15 : 1.0));
            p.drawPath(item.path);
            break;
        case ControllerMap::Item::Key: {
            const bool on = chosen.contains(item.id);
            const double lit_now = on ? 0 : glow(item.id);
            // The button the wizard asks for breathes; those it has are outlined in the accent.
            const double breath = on && !wizard_steps.isEmpty() ? 0.55 + 0.45 * std::cos(now / 160.0) : 1.0;
            const QColor fill = on ? Faded(look.accent, breath) : lit_now > 0 ? Faded(look.ink, 0.2 + 0.6 * lit_now) : look.ground;
            if (item.edge) {
                p.setBrush(fill);
                p.setPen(pen(on || given.contains(item.id) ? look.accent : look.ink, 1.15));
            } else {
                p.setBrush(on || lit_now > 0 ? QBrush(fill) : QBrush(Qt::NoBrush));
                p.setPen(Qt::NoPen);
            }
            p.drawPath(item.path);
            const QColor mark = on || lit_now > 0.5 ? look.ground : look.ink;
            if (!item.glyph.isEmpty()) {
                p.setBrush(Qt::NoBrush);
                p.setPen(pen(mark, 1.1));
                p.drawPath(item.glyph);
            } else if (const QString label = LabelIn(pad, item.id); !item.quiet && !label.isEmpty()) {
                QFont face = look.font(2, 10, 0.02);
                face.setPixelSize(100); // set by the painter's scale below: whole pixels would jump
                p.save();
                p.translate(item.label_at);
                p.scale(item.label_size / 100.0, item.label_size / 100.0);
                p.setFont(face);
                p.setPen(mark);
                p.drawText(QRectF(-400, -100, 800, 200), Qt::AlignCenter, label);
                p.restore();
            }
            break;
        }
        }
    }
    p.restore();

    // Under the drawing, the controller in the hand by its name; with a Steam Controller on the
    // bridge, its back too: its grips and its "..." button, under any console's pad.
    QList<const ControllerMap::Input*> back;
    if (hand_steam) {
        for (const ControllerMap::Input& input : ControllerMap::Load().inputs) {
            if (input.back > 0) {
                back.append(&input);
            }
        }
    }
    const double chip_w = 86, gap = 10;
    double x = area.center().x() - (back.size() * chip_w + (back.size() - 1) * gap) / 2;
    const double y = area.bottom() + 34;
    p.setFont(look.font(2, 10, 0.12));
    p.setPen(look.muted);
    const QString hand = hand_name.isEmpty() ? QStringLiteral("No controller found") : hand_name;
    p.drawText(QRectF(area.left(), area.bottom() + 6, area.width(), 20), Qt::AlignHCenter | Qt::AlignVCenter,
               (hand_steam ? hand + QStringLiteral(" · back buttons") : hand).toUpper());
    for (const ControllerMap::Input* input : back) {
        const QRectF chip(x, y, chip_w, 26);
        const bool on = chosen.contains(input->id);
        const double lit_now = glow(input->id);
        p.setBrush(on ? look.accent : lit_now > 0 ? Faded(look.ink, 0.15 + 0.45 * lit_now) : look.ground);
        p.setPen(QPen(on ? look.accent : Faded(look.ink, 0.6), 1));
        p.drawRoundedRect(chip, 6, 6);
        p.setPen(on ? look.ground : look.ink);
        p.drawText(chip, Qt::AlignCenter, input->label.toUpper());
        x += chip_w + gap;
    }
}

void ControllerScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const qint64 now = clock.elapsed();
    const double shown = leaving ? 1 - Ease(static_cast<double>(now - left_at) / CloseTime) : Ease(static_cast<double>(now) / OpenTime);
    p.setOpacity(shown);
    p.fillRect(rect(), look.ground);
    const double s = Scale();
    p.translate((width() - SheetW * s) / 2, (height() - SheetH * s) / 2 + (1 - shown) * 10 * s);
    p.scale(s, s);
    hits.clear();
    const QString console = ConsoleKey();

    // The head, as the pages have it.
    const QFont small = look.font(2, 11, 0.14);
    p.setFont(small);
    p.setPen(look.muted);
    const QString back = QStringLiteral("‹  SETTINGS · ");
    p.drawText(QPointF(Left, 49), back);
    p.setPen(look.ink);
    p.drawText(QPointF(Left + QFontMetricsF(small).horizontalAdvance(back), 49), QStringLiteral("CONTROLLER"));
    hits.append({QRectF(Left, 30, 220, 26), 4, 0});
    p.fillRect(QRectF(Left, 64, Right - Left, 1), look.rule);
    const bool asking = !wizard_steps.isEmpty();
    if (!asking) {
        // The wizard's way in: every button of the tab's console asked for in turn.
        const QString setup = QStringLiteral("START MAP ALL BUTTONS");
        const double w = QFontMetricsF(small).horizontalAdvance(setup) + 24;
        const QRectF button(Right - w, 32, w, 24);
        p.setPen(QPen(look.ink, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(button, 4, 4);
        p.drawText(button, Qt::AlignCenter, setup);
        hits.append({button, 5, 0});
    }

    // The consoles.
    const QFont tab_face = look.font(2, 12, 0.14);
    p.setFont(tab_face);
    double x = Left;
    for (int i = 0; i < consoles.size(); ++i) {
        const QString name = consoles[i].name.toUpper();
        const double w = QFontMetricsF(tab_face).horizontalAdvance(name);
        const bool on = i == tab;
        p.setPen(on ? look.ink : asking ? Faded(look.muted, 0.35) : look.muted);
        p.drawText(QPointF(x, 102), name);
        if (on) {
            p.fillRect(QRectF(x, 110, w, 2), look.ink);
        }
        if (!asking) {
            hits.append({QRectF(x - 6, 84, w + 12, 32), 0, i});
        }
        x += w + 30;
    }
    if (!console.isEmpty() && !asking) {
        const bool own = ControllerMap::HasOwn(store, console);
        const QString same = own ? hand_names.north.toUpper() + QStringLiteral(" RESET TO ALL CONSOLES") : QStringLiteral("ALL CONSOLES");
        const double w = QFontMetricsF(tab_face).horizontalAdvance(same);
        p.setPen(own ? look.ink : look.muted);
        p.drawText(QPointF(Right - w, 102), same);
        if (own) {
            hits.append({QRectF(Right - w - 6, 84, w + 12, 32), 3, 0});
        }
    }

    // The console's controller.
    PaintPad(p, QRectF(Left, 140, 600, 380));

    // The actions.
    const QFont number_face = look.font(2, 10, 0.1);
    const QFont label_face = look.font(1, 15, 0);
    const QFont chip_face = look.font(2, 11, 0.06);
    const ControllerMap::Pad& pad = ShownPad();
    if (asking) {
        // One button asked for at a time, with the time left to press it.
        const QString id = wizard_steps[wizard_at];
        p.setFont(number_face);
        p.setPen(look.muted);
        p.drawText(QPointF(ListLeft, 190), QStringLiteral("%1 / %2 · %3").arg(wizard_at + 1).arg(wizard_steps.size()).arg(consoles.value(tab).name.toUpper()));
        p.setFont(look.font(1, 20, 0));
        p.drawText(QPointF(ListLeft, 250), QStringLiteral("Press a button for"));
        p.setFont(look.font(0, 58, 0.01));
        p.setPen(look.ink);
        p.drawText(QRectF(ListLeft, 268, Right - ListLeft, 90), Qt::AlignLeft | Qt::AlignVCenter, GameName(id));
        p.setFont(look.font(1, 14, 0));
        p.setPen(look.accent);
        p.drawText(QPointF(ListLeft, 392), wizard_note);
        const double left_share = std::clamp(1.0 - static_cast<double>(now - wizard_since) / WizardWait, 0.0, 1.0);
        p.fillRect(QRectF(ListLeft, 414, Right - ListLeft, 2), look.rule);
        p.fillRect(QRectF(ListLeft, 414, (Right - ListLeft) * left_share, 2), look.ink);
        const char* const keys[] = {"BACKSPACE BACK ONE", "SPACE SKIP", "ESC CANCEL"};
        double kx = ListLeft;
        p.setFont(number_face);
        for (int i = 0; i < 3; ++i) {
            const QString text = QLatin1String(keys[i]);
            const double w = QFontMetricsF(number_face).horizontalAdvance(text) + 28;
            const QRectF chip(kx, 440, w, 30);
            const bool usable = i != 0 || wizard_at > 0;
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Faded(look.ink, usable ? 0.45 : 0.15), 1));
            p.drawRoundedRect(chip, 4, 4);
            p.setPen(usable ? look.ink : Faded(look.ink, 0.3));
            p.drawText(chip, Qt::AlignCenter, text);
            if (usable) {
                hits.append({chip, 6, i});
            }
            kx += w + 10;
        }
    } else {
    p.save();
    p.setClipRect(QRectF(ListLeft - 10, ListTop - 4, Right - ListLeft + 20, ListBottom - ListTop + 6));
    for (int i = 0; i < Rows(); ++i) {
        const double y = ListTop + (i - top) * RowH;
        if (y + RowH < ListTop || y > ListBottom) {
            continue;
        }
        const Line& one = lines[i];
        if (one.kind == Line::Head) {
            p.setFont(number_face);
            p.setPen(look.muted);
            p.drawText(QRectF(ListLeft, y + 4, 400, RowH - 4), Qt::AlignBottom | Qt::AlignLeft, one.label.toUpper());
            p.fillRect(QRectF(ListLeft, y + RowH - 1, Right - ListLeft, 1), look.rule);
            continue;
        }
        const bool on = i == row;
        const bool over = i == hover;
        const QRectF line(ListLeft, y, Right - ListLeft, RowH);
        if (on) {
            p.fillRect(QRectF(ListLeft, y + 1, 2, RowH - 2), look.ink);
        }
        p.fillRect(QRectF(ListLeft, y + RowH - 1, Right - ListLeft, 1), Faded(look.rule, 0.7));
        p.setFont(label_face);
        p.setPen(on || over ? look.ink : Faded(look.ink, 0.66));
        p.drawText(QRectF(ListLeft + 16, y, 364, RowH), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(label_face).elidedText(one.label, Qt::ElideRight, 364));
        // Learn, on the right; the buttons it is on before it.
        const QRectF learn(Right - 74, y + 5, 74, RowH - 10);
        p.setFont(number_face);
        if (learning == i) {
            const double pulse = 0.55 + 0.45 * std::sin(now / 160.0);
            p.setPen(Faded(look.accent, pulse));
            p.drawText(QRectF(ListLeft + 380, y, Right - ListLeft - 380, RowH), Qt::AlignVCenter | Qt::AlignRight,
                       QStringLiteral("PRESS A BUTTON · %1").arg(std::max<qint64>(1, (LearnWait - (now - learn_since) + 999) / 1000)));
            hits.append({line, 1, i});
            continue;
        }
        p.setBrush(on ? look.ink : Qt::NoBrush);
        p.setPen(QPen(on ? look.ink : Faded(look.ink, over ? 0.7 : 0.35), 1));
        p.drawRoundedRect(learn, 4, 4);
        p.setPen(on ? look.ground : (over ? look.ink : look.muted));
        p.drawText(learn, Qt::AlignCenter, QStringLiteral("LEARN"));
        p.setBrush(Qt::NoBrush);
        double chip_right = learn.left() - 12;
        QList<ControllerMap::Binding> bindings;
        if (one.kind == Line::Game) {
            // The controller's button it is on: marked where that is not its own.
            bool inherited = false, changed = false;
            const QString from = ControllerMap::GameSource(store, one.id, console, &inherited, &changed);
            if (from != QStringLiteral("none")) {
                bindings.append({from, !changed || inherited});
            }
        } else {
            bindings = ControllerMap::BindingsOf(store, actions[one.action], console);
        }
        const bool own_default = one.kind == Line::Game && !bindings.isEmpty() && bindings.first().input == one.id;
        p.setFont(chip_face);
        if (bindings.isEmpty()) {
            p.setPen(look.muted);
            p.drawText(QRectF(chip_right - 60, y, 60, RowH), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("—"));
        }
        for (auto it = bindings.crbegin(); it != bindings.crend(); ++it) {
            const QString name = HandName(it->input).toUpper();
            const double w = std::max(30.0, QFontMetricsF(chip_face).horizontalAdvance(name) + 18);
            const QRectF chip(chip_right - w, y + 5, w, RowH - 10);
            p.setPen(QPen(it->inherited ? Faded(look.ink, 0.3) : look.accent, 1));
            p.setBrush(it->inherited ? Qt::NoBrush : QBrush(Faded(look.accent, 0.16)));
            p.drawRoundedRect(chip, 4, 4);
            p.setPen(it->inherited ? look.muted : look.ink);
            p.drawText(chip, Qt::AlignCenter, name);
            chip_right = chip.left() - 8;
        }
        if (!bindings.isEmpty() && bindings.first().inherited && !console.isEmpty() && !own_default) {
            p.setFont(number_face);
            p.setPen(look.muted);
            p.drawText(QRectF(chip_right - 140, y, 140, RowH), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("ALL CONSOLES"));
        }
        hits.append({line, 1, i});
        hits.append({learn, 2, i});
    }
    p.restore();

    }

    // The foot: what was done, and the keys.
    p.fillRect(QRectF(Left, 646, Right - Left, 1), look.rule);
    p.setFont(number_face);
    if (!notice.isEmpty() && now - notice_at < 5000) {
        p.setPen(look.ink);
        p.drawText(QRectF(Left, 656, 640, 28), Qt::AlignVCenter | Qt::AlignLeft, notice);
    }
    p.setPen(look.muted);
    const QString keys = asking ? QStringLiteral("WAIT TO SKIP A BUTTON")
                         : learning >= 0 ? QStringLiteral("PRESS ANY BUTTON · ESC OR CLICK CANCEL")
                         : QStringLiteral("%1 CHANGE · %2 CLEAR · %3%4 %5 CONSOLE · %6 BACK")
                               .arg(hand_names.south, hand_names.west,
                                    console.isEmpty() ? QString() : hand_names.north + QStringLiteral(" RESET · "), hand_names.l1,
                                    hand_names.r1, hand_names.east)
                               .toUpper();
    p.drawText(QRectF(Left + 640, 656, Right - Left - 640, 28), Qt::AlignVCenter | Qt::AlignRight, keys);
}

} // namespace Home
