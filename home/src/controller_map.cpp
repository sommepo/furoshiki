// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include "controller_map.h"
#include "reader/core/console.h"
#include "reader/desktop/settings.h"

namespace Home::ControllerMap {

namespace {

const QString ButtonAction = QStringLiteral("reader_button_action_");
const QString BackButton = QStringLiteral("reader_back_button_");
const QString PadButton = QStringLiteral("pad_button_");

QList<Item> ItemsOf(const QJsonArray& array) {
    QList<Item> out;
    for (const QJsonValue& value : array) {
        const QJsonObject o = value.toObject();
        Item item;
        const QString kind = o.value(QStringLiteral("k")).toString();
        item.kind = kind == QStringLiteral("body") ? Item::Body : kind == QStringLiteral("part") ? Item::Part
                    : kind == QStringLiteral("key") ? Item::Key : Item::Line;
        item.id = o.value(QStringLiteral("id")).toString();
        item.label = o.value(QStringLiteral("label")).toString();
        if (o.contains(QStringLiteral("d"))) {
            item.path = ParsePath(o.value(QStringLiteral("d")).toString());
        } else {
            const double r = o.value(QStringLiteral("r")).toDouble();
            item.path.addEllipse(QPointF(o.value(QStringLiteral("cx")).toDouble(), o.value(QStringLiteral("cy")).toDouble()), r, r);
        }
        if (o.contains(QStringLiteral("glyph"))) {
            item.glyph = ParsePath(o.value(QStringLiteral("glyph")).toString());
        }
        item.strong = o.value(QStringLiteral("strong")).toBool();
        item.sunk = o.value(QStringLiteral("fill")).toBool();
        item.quiet = o.value(QStringLiteral("quiet")).toBool();
        item.edge = o.value(QStringLiteral("edge")).toBool(true);
        item.label_at = QPointF(o.value(QStringLiteral("lx")).toDouble(), o.value(QStringLiteral("ly")).toDouble());
        item.label_size = o.value(QStringLiteral("ls")).toDouble(8);
        out.append(item);
    }
    return out;
}

/// The key a binding of `action` to the input numbered `number` is kept in.
QString NoneOf(const QString& key) {
    return key.startsWith(ButtonAction) || key.startsWith(BackButton) ? QStringLiteral("none") : QStringLiteral("-1");
}

QString DefaultOf(const QString& key) {
    if (key == ReaderApp::ReaderSettings::KeyCursorButton) {
        return QString::number(Reader::ConsoleTraits::CursorButtonDefault);
    }
    if (key == ReaderApp::ReaderSettings::KeyMenuButton) {
        return QString::number(Reader::ConsoleTraits::MenuButtonDefault);
    }
    if (key == ReaderApp::ReaderSettings::KeyStatsButton) {
        return QStringLiteral("-1");
    }
    if (key.startsWith(BackButton)) {
        const QString action = ReaderApp::ReaderSettings::DefaultBackButtonAction(key.mid(BackButton.size()).toInt());
        return action.isEmpty() ? QStringLiteral("none") : action;
    }
    if (key.startsWith(PadButton)) {
        return key.mid(PadButton.size()); // each console button on its own
    }
    return QStringLiteral("none");
}

} // namespace

QString Pad::LabelOf(const QString& id) const {
    for (const Item& item : draw) {
        if (item.kind == Item::Key && item.id == id) {
            return item.label;
        }
    }
    return {};
}

QPainterPath ParsePath(const QString& text) {
    QPainterPath path;
    static const QRegularExpression token(QStringLiteral("[MLCQZmlcqz]|-?[0-9]*\\.?[0-9]+"));
    QStringList parts;
    for (auto it = token.globalMatch(text); it.hasNext();) {
        parts.append(it.next().captured(0));
    }
    QChar command;
    int i = 0;
    const auto number = [&]() { return i < parts.size() ? parts[i++].toDouble() : 0.0; };
    while (i < parts.size()) {
        if (parts[i].size() == 1 && parts[i][0].isLetter()) {
            command = parts[i][0].toUpper();
            ++i;
        }
        if (command == QLatin1Char('M')) {
            const double x = number(), y = number();
            path.moveTo(x, y);
            command = QLatin1Char('L'); // more pairs after M are lines
        } else if (command == QLatin1Char('L')) {
            const double x = number(), y = number();
            path.lineTo(x, y);
        } else if (command == QLatin1Char('C')) {
            const double a = number(), b = number(), c = number(), d = number(), x = number(), y = number();
            path.cubicTo(a, b, c, d, x, y);
        } else if (command == QLatin1Char('Q')) {
            const double a = number(), b = number(), x = number(), y = number();
            path.quadTo(a, b, x, y);
        } else if (command == QLatin1Char('Z')) {
            path.closeSubpath();
        } else {
            ++i; // something this reader does not draw
        }
    }
    return path;
}

const Data& Load() {
    static const Data data = [] {
        Data out;
        QFile file(QStringLiteral(":/controllers/controllers.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return out;
        }
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        for (const QJsonValue& value : root.value(QStringLiteral("inputs")).toArray()) {
            const QJsonObject o = value.toObject();
            out.inputs.append({o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("number")).toInt(-1),
                               o.value(QStringLiteral("back")).toInt(0), o.value(QStringLiteral("label")).toString()});
        }
        for (const QJsonValue& value : root.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject o = value.toObject();
            out.actions.append({o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("label")).toString(),
                                o.value(QStringLiteral("setting")).toString(), o.value(QStringLiteral("held")).toBool(),
                                o.value(QStringLiteral("desktop")).toBool()});
        }
        const QJsonObject consoles = root.value(QStringLiteral("consoles")).toObject();
        for (auto it = consoles.begin(); it != consoles.end(); ++it) {
            out.pad_of.insert(it.key(), it.value().toString());
        }
        const QJsonObject pads = root.value(QStringLiteral("pads")).toObject();
        for (auto it = pads.begin(); it != pads.end(); ++it) {
            QJsonObject o = it.value().toObject();
            const QString name = o.value(QStringLiteral("name")).toString();
            if (o.contains(QStringLiteral("same_as"))) {
                o = pads.value(o.value(QStringLiteral("same_as")).toString()).toObject();
            }
            Pad pad;
            pad.id = it.key();
            pad.name = name;
            pad.draw = ItemsOf(o.value(QStringLiteral("draw")).toArray());
            out.pads.insert(pad.id, pad);
        }
        return out;
    }();
    return data;
}

const Pad& PadOf(const QString& console) {
    const Data& data = Load();
    const QString id = data.pad_of.value(console.isEmpty() ? QStringLiteral("all") : console, data.pad_of.value(QStringLiteral("all")));
    static const Pad none;
    const auto found = data.pads.constFind(id);
    if (found != data.pads.constEnd()) {
        return *found;
    }
    const auto steam = data.pads.constFind(data.pad_of.value(QStringLiteral("all")));
    return steam != data.pads.constEnd() ? *steam : none;
}

const Input* InputById(const QString& id) {
    for (const Input& input : Load().inputs) {
        if (input.id == id) {
            return &input;
        }
    }
    return nullptr;
}

const Input* InputByNumber(int number) {
    for (const Input& input : Load().inputs) {
        if (input.number >= 0 && input.number == number) {
            return &input;
        }
    }
    return nullptr;
}

QString NameOf(const Pad& pad, const QString& id) {
    const QString own = pad.LabelOf(id);
    if (!own.isEmpty()) {
        return own;
    }
    if (const Input* input = InputById(id); input != nullptr && !input->label.isEmpty()) {
        return input->label;
    }
    static const QHash<QString, QString> arrows{{QStringLiteral("up"), QStringLiteral("↑")}, {QStringLiteral("down"), QStringLiteral("↓")},
                                                {QStringLiteral("left"), QStringLiteral("←")}, {QStringLiteral("right"), QStringLiteral("→")}};
    return arrows.value(id, id.toUpper());
}

QStringList Keys() {
    QStringList keys{ReaderApp::ReaderSettings::KeyCursorButton, ReaderApp::ReaderSettings::KeyMenuButton,
                     ReaderApp::ReaderSettings::KeyStatsButton};
    for (const Input& input : Load().inputs) {
        keys.append(input.back > 0 ? BackButton + QString::number(input.back) : ButtonAction + QString::number(input.number));
    }
    for (const Input& input : Load().inputs) {
        if (input.number >= 0) {
            keys.append(PadButton + input.id);
        }
    }
    return keys;
}

QStringList GameButtons(const Pad& pad) {
    QStringList out;
    for (const Item& item : pad.draw) {
        if (item.kind == Item::Key && !out.contains(item.id)) {
            out.append(item.id);
        }
    }
    // As a hand goes over a pad: the directions, the four, the shoulders, the sticks, the two in the middle.
    static const QStringList order{QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"), QStringLiteral("right"),
                                   QStringLiteral("south"), QStringLiteral("east"), QStringLiteral("west"), QStringLiteral("north"),
                                   QStringLiteral("l1"), QStringLiteral("r1"), QStringLiteral("l2"), QStringLiteral("r2"),
                                   QStringLiteral("l3"), QStringLiteral("r3"), QStringLiteral("select"), QStringLiteral("start")};
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return order.indexOf(a) < order.indexOf(b); });
    return out;
}

QString GameSource(const Store& store, const QString& button, const QString& console, bool* inherited, bool* changed) {
    const QString source = Resolved(store, PadButton + button, console, inherited);
    if (changed != nullptr) {
        *changed = source != button;
    }
    return source;
}

bool AssignGame(const Store& store, const QString& button, const QString& input, const QString& console) {
    const Input* physical = InputById(input);
    if (physical == nullptr || physical->number < 0) {
        return false;
    }
    const QString before = GameSource(store, button, console);
    // The console's button that was on that input takes the one this leaves.
    for (const Input& other : Load().inputs) {
        if (other.number >= 0 && other.id != button && GameSource(store, other.id, console) == input) {
            store.set(PadButton + other.id, before, console);
        }
    }
    store.set(PadButton + button, input, console);
    if (store.save) {
        store.save();
    }
    return true;
}

void SetGame(const Store& store, const QHash<QString, QString>& given, const QString& console) {
    QHash<QString, QString> before;
    for (const Input& input : Load().inputs) {
        if (input.number >= 0) {
            before.insert(input.id, GameSource(store, input.id, console));
        }
    }
    const QList<QString> taken = given.values();
    for (auto it = given.cbegin(); it != given.cend(); ++it) {
        store.set(PadButton + it.key(), it.value(), console);
    }
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!given.contains(it.key()) && taken.contains(it.value())) {
            store.set(PadButton + it.key(), QStringLiteral("none"), console);
        }
    }
    if (store.save) {
        store.save();
    }
}

void ClearGame(const Store& store, const QString& button, const QString& console) {
    // Its own again; in a console's layer, said outright where all consoles have it elsewhere.
    const QString all = console.isEmpty() ? QString() : Resolved(store, PadButton + button, {});
    store.set(PadButton + button, console.isEmpty() || all == button ? QString() : button, console);
    if (store.save) {
        store.save();
    }
}

QString Resolved(const Store& store, const QString& key, const QString& console, bool* inherited) {
    if (inherited != nullptr) {
        *inherited = false;
    }
    if (!console.isEmpty()) {
        const QString own = store.get(key, console);
        if (!own.isEmpty()) {
            return own;
        }
        if (inherited != nullptr) {
            *inherited = true;
        }
    }
    const QString all = store.get(key, {});
    return all.isEmpty() ? DefaultOf(key) : all;
}

QList<Binding> BindingsOf(const Store& store, const Action& action, const QString& console) {
    QList<Binding> out;
    bool inherited = false;
    if (!action.setting.isEmpty()) {
        const int number = Resolved(store, action.setting, console, &inherited).toInt();
        if (const Input* input = InputByNumber(number)) {
            out.append({input->id, inherited});
        }
    }
    for (const Input& input : Load().inputs) {
        const QString key = input.back > 0 ? BackButton + QString::number(input.back) : ButtonAction + QString::number(input.number);
        if (Resolved(store, key, console, &inherited) == action.id) {
            out.append({input.id, inherited});
        }
    }
    return out;
}

QString ActionOn(const Store& store, const QString& id, const QString& console) {
    for (const Action& action : Load().actions) {
        for (const Binding& binding : BindingsOf(store, action, console)) {
            if (binding.input == id) {
                return action.id;
            }
        }
    }
    return {};
}

void Clear(const Store& store, const Action& action, const QString& console) {
    if (!action.setting.isEmpty() && Resolved(store, action.setting, console).toInt() >= 0) {
        store.set(action.setting, QStringLiteral("-1"), console);
    }
    for (const Input& input : Load().inputs) {
        const QString key = input.back > 0 ? BackButton + QString::number(input.back) : ButtonAction + QString::number(input.number);
        if (Resolved(store, key, console) == action.id) {
            store.set(key, QStringLiteral("none"), console);
        }
    }
}

bool Assign(const Store& store, const Action& action, const QString& id, const QString& console) {
    const Input* input = InputById(id);
    if (input == nullptr || (action.held && input->back > 0)) {
        return false;
    }
    // The action leaves where it was, and what was on the input leaves it.
    Clear(store, action, console);
    if (input->number >= 0) {
        for (const Action& other : Load().actions) {
            if (!other.setting.isEmpty() && Resolved(store, other.setting, console).toInt() == input->number) {
                store.set(other.setting, QStringLiteral("-1"), console);
            }
        }
    }
    const QString key = input->back > 0 ? BackButton + QString::number(input->back) : ButtonAction + QString::number(input->number);
    if (!action.setting.isEmpty() && input->number >= 0) {
        store.set(key, QStringLiteral("none"), console);
        store.set(action.setting, QString::number(input->number), console);
    } else {
        store.set(key, action.id, console);
    }
    if (store.save) {
        store.save();
    }
    return true;
}

void Reset(const Store& store, const QString& console) {
    if (console.isEmpty()) {
        return;
    }
    for (const QString& key : Keys()) {
        store.set(key, {}, console);
    }
    if (store.save) {
        store.save();
    }
}

bool HasOwn(const Store& store, const QString& console) {
    if (console.isEmpty()) {
        return false;
    }
    for (const QString& key : Keys()) {
        if (!store.get(key, console).isEmpty()) {
            return true;
        }
    }
    return false;
}

} // namespace Home::ControllerMap
