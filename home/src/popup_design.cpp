// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "popup_design.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <optional>

namespace Home::PopupDesign {

namespace {

const QString Kind = QStringLiteral("reader-popup-design");

Property Size(const char* key, const char* label, double low, double high, double start, double step = 1, const char* unit = "px") {
    Property p;
    p.key = QString::fromLatin1(key);
    p.label = QString::fromLatin1(label);
    p.kind = Property::Size;
    p.low = low;
    p.high = high;
    p.start = start;
    p.step = step;
    p.unit = QString::fromLatin1(unit);
    return p;
}

Property Number(const char* key, const char* label, double low, double high, double start, double step, const char* unit) {
    Property p = Size(key, label, low, high, start, step, unit);
    p.kind = Property::Number;
    return p;
}

Property Colour(const char* key, const char* label) {
    Property p;
    p.key = QString::fromLatin1(key);
    p.label = QString::fromLatin1(label);
    p.kind = Property::Colour;
    return p;
}

Property Toggle(const char* key, const char* label) {
    Property p;
    p.key = QString::fromLatin1(key);
    p.label = QString::fromLatin1(label);
    p.kind = Property::Toggle;
    return p;
}

Property Choice(const char* key, const char* label, QList<QPair<QString, QString>> choices) {
    Property p;
    p.key = QString::fromLatin1(key);
    p.label = QString::fromLatin1(label);
    p.kind = Property::Choice;
    p.choices = std::move(choices);
    return p;
}

QList<QPair<QString, QString>> Fonts() {
    return {{QStringLiteral("sans"), QStringLiteral("Gothic")},
            {QStringLiteral("serif"), QStringLiteral("Mincho")},
            {QStringLiteral("rounded"), QStringLiteral("Rounded")},
            {QStringLiteral("mono"), QStringLiteral("Monospaced")}};
}

QString FontStack(const QString& font) {
    if (font == QStringLiteral("serif")) {
        return QStringLiteral("\"Noto Serif JP\", \"Yu Mincho\", \"Hiragino Mincho ProN\", serif");
    }
    if (font == QStringLiteral("rounded")) {
        return QStringLiteral("\"M PLUS Rounded 1c\", \"Kosugi Maru\", \"Yu Gothic UI\", sans-serif");
    }
    if (font == QStringLiteral("mono")) {
        return QStringLiteral("\"BIZ UDGothic\", \"MS Gothic\", ui-monospace, monospace");
    }
    return QStringLiteral("\"Noto Sans JP\", \"Yu Gothic UI\", \"Hiragino Sans\", \"Meiryo\", sans-serif");
}

QString Scaled(double px) {
    return QStringLiteral("calc(%1px * var(--scale))").arg(px);
}

/// A colour as CSS takes it, alpha too; empty for one that is not a colour.
QString ColourOf(const QJsonValue& value) {
    const QColor colour = QColor::fromString(value.toString());
    if (!colour.isValid()) {
        return {};
    }
    return colour.alpha() == 255 ? colour.name() : QStringLiteral("rgba(%1, %2, %3, %4)").arg(colour.red()).arg(colour.green()).arg(colour.blue()).arg(colour.alphaF(), 0, 'f', 3);
}

/// The rules of one value: selector { declarations }, each declaration !important, as the user's
/// stylesheet comes after the look and must win over its own rules.
QString Rules(const QString& key, const QJsonValue& value) {
    const double n = value.toDouble();
    const bool on = value.toBool();
    const QString c = ColourOf(value);
    const auto rule = [](const QString& selector, const QString& declarations) {
        QString out;
        for (const QString& one : declarations.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            out += one.trimmed() + QStringLiteral(" !important; ");
        }
        return selector + QStringLiteral(" { ") + out + QStringLiteral("}\n");
    };
    const auto shown = [&](const QString& selector, const QString& display = QStringLiteral("")) {
        return rule(selector, on ? (display.isEmpty() ? QStringLiteral("visibility: visible") : QStringLiteral("display: ") + display) : QStringLiteral("display: none"));
    };
    // The whole popup.
    if (key == QStringLiteral("popup.ground") && !c.isEmpty()) return rule(QStringLiteral(".popup"), QStringLiteral("--ground: ") + c);
    if (key == QStringLiteral("popup.text") && !c.isEmpty()) return rule(QStringLiteral(".popup"), QStringLiteral("--text: %1; color: %1").arg(c));
    if (key == QStringLiteral("popup.dim") && !c.isEmpty()) return rule(QStringLiteral(".popup"), QStringLiteral("--dim: ") + c);
    if (key == QStringLiteral("popup.accent") && !c.isEmpty()) return rule(QStringLiteral(".popup"), QStringLiteral("--accent: ") + c);
    if (key == QStringLiteral("popup.edge") && !c.isEmpty()) return rule(QStringLiteral(".popup"), QStringLiteral("--edge: %1; border-color: %1").arg(c));
    if (key == QStringLiteral("popup.edge_width")) return rule(QStringLiteral(".popup"), QStringLiteral("border-width: %1px; border-style: solid").arg(n));
    if (key == QStringLiteral("popup.top") && !c.isEmpty()) return rule(QStringLiteral(".popup"), QStringLiteral("border-top: 3px solid ") + c);
    if (key == QStringLiteral("popup.radius")) return rule(QStringLiteral(".popup"), QStringLiteral("--radius: %1px; border-radius: %1px").arg(n));
    if (key == QStringLiteral("popup.pad_x")) return rule(QStringLiteral(".popup"), QStringLiteral("padding-left: %1; padding-right: %1").arg(Scaled(n)));
    if (key == QStringLiteral("popup.pad_y")) return rule(QStringLiteral(".popup"), QStringLiteral("padding-top: %1; padding-bottom: %1").arg(Scaled(n)));
    if (key == QStringLiteral("popup.font")) return rule(QStringLiteral(".popup"), QStringLiteral("--font: %1; font-family: %1").arg(FontStack(value.toString())));
    if (key == QStringLiteral("popup.size")) return rule(QStringLiteral(".popup"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("popup.line")) return rule(QStringLiteral(".popup"), QStringLiteral("line-height: %1").arg(n / 100.0));
    if (key == QStringLiteral("popup.shadow")) {
        return rule(QStringLiteral(".popup"), n <= 0 ? QStringLiteral("box-shadow: none") : QStringLiteral("box-shadow: 0 %1px %2px rgba(0, 0, 0, 0.45)").arg(n / 3).arg(n));
    }
    // An entry and the rule between entries.
    if (key == QStringLiteral("entries.gap")) {
        return rule(QStringLiteral(".entry"), QStringLiteral("padding-bottom: %1").arg(Scaled(n))) +
               rule(QStringLiteral(".entry + .entry"), QStringLiteral("padding-top: %1").arg(Scaled(n)));
    }
    if (key == QStringLiteral("entries.rule")) return rule(QStringLiteral(".entry + .entry"), on ? QStringLiteral("border-top-style: solid; border-top-width: 1px") : QStringLiteral("border-top: none"));
    if (key == QStringLiteral("entries.rule_colour") && !c.isEmpty()) return rule(QStringLiteral(".entry + .entry"), QStringLiteral("border-top-color: ") + c);
    // The headline.
    if (key == QStringLiteral("headword.size")) return rule(QStringLiteral(".headword"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("headword.weight")) return rule(QStringLiteral(".headword"), QStringLiteral("font-weight: %1").arg(static_cast<int>(n)));
    if (key == QStringLiteral("headword.colour") && !c.isEmpty()) return rule(QStringLiteral(".headword"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("headword.font")) return rule(QStringLiteral(".headword"), QStringLiteral("font-family: ") + FontStack(value.toString()));
    if (key == QStringLiteral("headword.furigana")) {
        return rule(QStringLiteral(".headword rt"), on ? QStringLiteral("display: ruby-text") : QStringLiteral("display: none"));
    }
    if (key == QStringLiteral("headword.furigana_size")) return rule(QStringLiteral(".headword rt"), QStringLiteral("font-size: %1em").arg(n / 100.0));
    if (key == QStringLiteral("headword.unknown")) {
        const QString word = QStringLiteral(".popup:not(.no-mark) .entry.unknown .headword");
        const QString mode = value.toString();
        if (mode == QStringLiteral("colour")) return rule(word, QStringLiteral("background: none; box-shadow: none; padding: 0; color: var(--accent)"));
        if (mode == QStringLiteral("underline")) return rule(word, QStringLiteral("background: none; padding: 0; box-shadow: inset 0 -3px 0 var(--accent)"));
        if (mode == QStringLiteral("tint")) return rule(word, QStringLiteral("background: color-mix(in srgb, var(--accent) 22%, transparent); box-shadow: none; padding: 0 3px; border-radius: 4px"));
        if (mode == QStringLiteral("none")) return rule(word, QStringLiteral("background: none; box-shadow: none; padding: 0"));
        return {};
    }
    if (key == QStringLiteral("reading.show")) return shown(QStringLiteral(".head > .reading"), QStringLiteral("inline"));
    if (key == QStringLiteral("reading.size")) return rule(QStringLiteral(".head > .reading"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("reading.colour") && !c.isEmpty()) return rule(QStringLiteral(".head > .reading"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("pitch.show")) return shown(QStringLiteral(".head > .pitch, .head > .reasons"), QStringLiteral("inline"));
    if (key == QStringLiteral("pitch.size")) return rule(QStringLiteral(".head > .pitch, .head > .reasons"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("pitch.colour") && !c.isEmpty()) return rule(QStringLiteral(".head > .pitch, .head > .reasons"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("frequencies.show")) return shown(QStringLiteral(".tags.frequencies"), QStringLiteral("inline-flex"));
    if (key == QStringLiteral("frequencies.size")) return rule(QStringLiteral(".tag.frequency"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("frequencies.colour") && !c.isEmpty()) return rule(QStringLiteral(".tag.frequency .tag-value"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("frequencies.names")) return rule(QStringLiteral(".tag.frequency .tag-name"), on ? QStringLiteral("display: inline") : QStringLiteral("display: none"));
    // Tags: a part of speech, 名前, forms', frequencies' boxes.
    if (key == QStringLiteral("tags.size")) return rule(QStringLiteral(".tag"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("tags.colour") && !c.isEmpty()) return rule(QStringLiteral(".tag"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("tags.ground") && !c.isEmpty()) return rule(QStringLiteral(".tag"), QStringLiteral("background: ") + c);
    if (key == QStringLiteral("tags.edge") && !c.isEmpty()) return rule(QStringLiteral(".tag"), QStringLiteral("border: 1px solid ") + c);
    if (key == QStringLiteral("tags.radius")) return rule(QStringLiteral(".tag"), QStringLiteral("border-radius: %1px").arg(n));
    if (key == QStringLiteral("tags.pad")) return rule(QStringLiteral(".tag"), QStringLiteral("padding: 0 %1").arg(Scaled(n)));
    if (key == QStringLiteral("tags.glow")) {
        return rule(QStringLiteral(".tag"), n <= 0 ? QStringLiteral("box-shadow: none") : QStringLiteral("box-shadow: 0 0 %1px color-mix(in srgb, var(--accent) 70%, transparent)").arg(n));
    }
    if (key == QStringLiteral("tags.capitals")) {
        return rule(QStringLiteral(".tag"), on ? QStringLiteral("text-transform: uppercase; letter-spacing: 0.1em") : QStringLiteral("text-transform: none; letter-spacing: normal"));
    }
    if (key == QStringLiteral("lookups.show")) return shown(QStringLiteral(".entry > .lookups"), QStringLiteral("block"));
    if (key == QStringLiteral("lookups.size")) return rule(QStringLiteral(".entry > .lookups"), QStringLiteral("font-size: ") + Scaled(n));
    // Senses.
    if (key == QStringLiteral("dictionary.show")) return shown(QStringLiteral(".dictionary-name"), QStringLiteral("block"));
    if (key == QStringLiteral("dictionary.size")) return rule(QStringLiteral(".dictionary-name"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("dictionary.colour") && !c.isEmpty()) return rule(QStringLiteral(".dictionary-name"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("dictionary.rule")) {
        return rule(QStringLiteral(".dictionary-name"), on ? QStringLiteral("border-bottom: 1px solid var(--edge); padding-bottom: 2px") : QStringLiteral("border-bottom: none"));
    }
    if (key == QStringLiteral("glosses.size")) return rule(QStringLiteral(".glosses, .forms .form"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("glosses.colour") && !c.isEmpty()) return rule(QStringLiteral(".glosses"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("glosses.font")) return rule(QStringLiteral(".glosses, .forms .form"), QStringLiteral("font-family: ") + FontStack(value.toString()));
    if (key == QStringLiteral("glosses.spacing")) return rule(QStringLiteral(".sense"), QStringLiteral("margin: %1 0").arg(Scaled(n)));
    if (key == QStringLiteral("glosses.separator")) {
        const QString mode = value.toString();
        if (mode == QStringLiteral("lines")) {
            return rule(QStringLiteral(".glosses .gloss"), QStringLiteral("display: block")) + rule(QStringLiteral(".glosses .gloss + .gloss::before"), QStringLiteral("content: none"));
        }
        const QHash<QString, QString> marks{{QStringLiteral("bar"), QStringLiteral("\" | \"")}, {QStringLiteral("dot"), QStringLiteral("\" · \"")},
                                            {QStringLiteral("slash"), QStringLiteral("\" / \"")}, {QStringLiteral("semicolon"), QStringLiteral("\"; \"")},
                                            {QStringLiteral("comma"), QStringLiteral("\", \"")}};
        return marks.contains(mode) ? rule(QStringLiteral(".glosses .gloss + .gloss::before"), QStringLiteral("content: ") + marks.value(mode)) : QString();
    }
    if (key == QStringLiteral("numbers.show")) return shown(QStringLiteral(".sense > .number"), QStringLiteral("inline"));
    if (key == QStringLiteral("numbers.colour") && !c.isEmpty()) return rule(QStringLiteral(".sense > .number"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("grammar.show")) return shown(QStringLiteral(".sense .tag[data-category=\"part-of-speech\"]"), QStringLiteral("inline"));
    if (key == QStringLiteral("forms.show")) return shown(QStringLiteral(".entry > .forms"), QStringLiteral("block"));
    // A name's card.
    if (key == QStringLiteral("card.portrait")) {
        return rule(QStringLiteral(".name-card .portrait, .popup[data-portrait=\"large\"] .name-card .portrait"), QStringLiteral("width: ") + Scaled(n));
    }
    if (key == QStringLiteral("card.name_size")) return rule(QStringLiteral(".who-name"), QStringLiteral("font-size: ") + Scaled(n));
    if (key == QStringLiteral("card.about")) return shown(QStringLiteral(".name-card .about"), QStringLiteral("block"));
    if (key == QStringLiteral("card.also")) return shown(QStringLiteral(".name-card .also"), QStringLiteral("flex"));
    if (key == QStringLiteral("card.romaji")) return shown(QStringLiteral(".name-card .romaji"), QStringLiteral("inline"));
    // The card buttons.
    if (key == QStringLiteral("buttons.size")) return rule(QStringLiteral(".card"), QStringLiteral("width: %1; height: %1").arg(Scaled(n)));
    if (key == QStringLiteral("buttons.colour") && !c.isEmpty()) return rule(QStringLiteral(".card, .card.book"), QStringLiteral("color: ") + c);
    if (key == QStringLiteral("buttons.ground") && !c.isEmpty()) return rule(QStringLiteral(".card"), QStringLiteral("background: ") + c);
    if (key == QStringLiteral("buttons.edge") && !c.isEmpty()) return rule(QStringLiteral(".card"), QStringLiteral("border: 1px solid ") + c);
    if (key == QStringLiteral("buttons.radius")) return rule(QStringLiteral(".card"), QStringLiteral("border-radius: %1px").arg(n));
    if (key == QStringLiteral("buttons.side")) {
        const QString side = value.toString();
        if (side == QStringLiteral("left") || side == QStringLiteral("right")) {
            return rule(QStringLiteral(".card-buttons"), QStringLiteral("justify-content: ") + (side == QStringLiteral("left") ? QStringLiteral("flex-start") : QStringLiteral("flex-end")));
        }
    }
    return {};
}

QStringList Strings(const QJsonValue& value) {
    QStringList out;
    for (const QJsonValue& one : value.toArray()) {
        out.append(one.toString());
    }
    return out;
}

} // namespace

const QList<Element>& Elements() {
    static const QList<Element> all = [] {
        const QList<QPair<QString, QString>> fonts = Fonts();
        QList<Element> list;
        list.append({QStringLiteral("popup"), QStringLiteral("The popup"), QStringLiteral(".popup"), QStringLiteral("popup.size"),
                     {Colour("popup.ground", "Ground"), Colour("popup.text", "Text"), Colour("popup.dim", "Quieter text"),
                      Colour("popup.accent", "Accent"), Colour("popup.edge", "Edge"), Size("popup.edge_width", "Edge width", 0, 6, 1),
                      Colour("popup.top", "Rule along the top"), Size("popup.radius", "Corners", 0, 28, 6),
                      Size("popup.pad_x", "Room at the sides", 0, 40, 10), Size("popup.pad_y", "Room above and below", 0, 40, 8),
                      Choice("popup.font", "Type", fonts), Size("popup.size", "Text size", 9, 22, 13, 0.5),
                      Number("popup.line", "Line height", 100, 220, 140, 5, "%"), Size("popup.shadow", "Shadow", 0, 40, 0)}});
        list.append({QStringLiteral("headword"), QStringLiteral("Headword"), QStringLiteral(".headword"), QStringLiteral("headword.size"),
                     {Size("headword.size", "Size", 12, 56, 22), Number("headword.weight", "Weight", 300, 900, 600, 100, ""),
                      Colour("headword.colour", "Colour"), Choice("headword.font", "Type", fonts), Toggle("headword.furigana", "Furigana over it"),
                      Number("headword.furigana_size", "Furigana size", 30, 80, 50, 2, "%"),
                      Choice("headword.unknown", "A word not in Anki", {{QStringLiteral("tint"), QStringLiteral("Tinted")},
                                                                       {QStringLiteral("underline"), QStringLiteral("Underlined")},
                                                                       {QStringLiteral("colour"), QStringLiteral("In the accent")},
                                                                       {QStringLiteral("none"), QStringLiteral("Not marked")}})}});
        list.append({QStringLiteral("reading"), QStringLiteral("Reading"), QStringLiteral(".head > .reading"), QStringLiteral("reading.size"),
                     {Toggle("reading.show", "Shown beside it"), Size("reading.size", "Size", 8, 24, 11, 0.5), Colour("reading.colour", "Colour")}});
        list.append({QStringLiteral("pitch"), QStringLiteral("Pitch and inflection"), QStringLiteral(".pitch, .reasons"), QStringLiteral("pitch.size"),
                     {Toggle("pitch.show", "Shown"), Size("pitch.size", "Size", 8, 20, 11, 0.5), Colour("pitch.colour", "Colour")}});
        list.append({QStringLiteral("frequencies"), QStringLiteral("Frequencies"), QStringLiteral(".tags.frequencies, .tag.frequency"), QStringLiteral("frequencies.size"),
                     {Toggle("frequencies.show", "Shown"), Toggle("frequencies.names", "Their lists' names"), Size("frequencies.size", "Size", 8, 20, 11, 0.5),
                      Colour("frequencies.colour", "Figures' colour")}});
        list.append({QStringLiteral("tags"), QStringLiteral("Tags"), QStringLiteral(".tag"), QStringLiteral("tags.size"),
                     {Size("tags.size", "Size", 8, 20, 11, 0.5), Colour("tags.colour", "Text"), Colour("tags.ground", "Ground"), Colour("tags.edge", "Edge"),
                      Size("tags.radius", "Corners", 0, 12, 3), Size("tags.pad", "Room inside", 0, 12, 4), Size("tags.glow", "Glow", 0, 16, 0),
                      Toggle("tags.capitals", "Small capitals")}});
        list.append({QStringLiteral("lookups"), QStringLiteral("Times looked up"), QStringLiteral(".lookups"), QStringLiteral("lookups.size"),
                     {Toggle("lookups.show", "Shown"), Size("lookups.size", "Size", 8, 18, 11, 0.5)}});
        list.append({QStringLiteral("dictionary"), QStringLiteral("Dictionary names"), QStringLiteral(".dictionary-name"), QStringLiteral("dictionary.size"),
                     {Toggle("dictionary.show", "Shown"), Size("dictionary.size", "Size", 8, 18, 11, 0.5), Colour("dictionary.colour", "Colour"),
                      Toggle("dictionary.rule", "A rule under each")}});
        list.append({QStringLiteral("glosses"), QStringLiteral("Meanings"), QStringLiteral(".glosses, .gloss, .sense, .senses"), QStringLiteral("glosses.size"),
                     {Size("glosses.size", "Size", 9, 26, 13, 0.5), Colour("glosses.colour", "Colour"), Choice("glosses.font", "Type", fonts),
                      Size("glosses.spacing", "Room between senses", 0, 12, 1),
                      Choice("glosses.separator", "Between meanings", {{QStringLiteral("bar"), QStringLiteral("|")}, {QStringLiteral("dot"), QStringLiteral("·")},
                                                                       {QStringLiteral("slash"), QStringLiteral("/")}, {QStringLiteral("semicolon"), QStringLiteral(";")},
                                                                       {QStringLiteral("comma"), QStringLiteral(",")}, {QStringLiteral("lines"), QStringLiteral("A line each")}}),
                      Toggle("numbers.show", "Numbers"), Colour("numbers.colour", "Numbers' colour"), Toggle("grammar.show", "Grammar tags")}});
        list.append({QStringLiteral("forms"), QStringLiteral("Other forms"), QStringLiteral(".forms"), QString(), {Toggle("forms.show", "Shown")}});
        list.append({QStringLiteral("card"), QStringLiteral("A name's card"), QStringLiteral(".name-card"), QStringLiteral("card.portrait"),
                     {Size("card.portrait", "Picture", 30, 160, 54), Size("card.name_size", "Name size", 12, 36, 18), Toggle("card.romaji", "Romaji"),
                      Toggle("card.also", "Other names"), Toggle("card.about", "About them")}});
        list.append({QStringLiteral("buttons"), QStringLiteral("Card buttons"), QStringLiteral(".card-buttons, .card"), QStringLiteral("buttons.size"),
                     {Size("buttons.size", "Size", 16, 44, 24), Colour("buttons.colour", "Sign"), Colour("buttons.ground", "Ground"), Colour("buttons.edge", "Edge"),
                      Size("buttons.radius", "Corners", 0, 22, 4),
                      Choice("buttons.side", "Side", {{QStringLiteral("left"), QStringLiteral("Left")}, {QStringLiteral("right"), QStringLiteral("Right")}})}});
        list.append({QStringLiteral("entries"), QStringLiteral("Between words"), QStringLiteral(".entry"), QString(),
                     {Toggle("entries.rule", "A rule"), Colour("entries.rule_colour", "Its colour"), Size("entries.gap", "Room", 0, 30, 6)}});
        return list;
    }();
    return all;
}

const Property* PropertyOf(const QString& key) {
    for (const Element& element : Elements()) {
        for (const Property& property : element.properties) {
            if (property.key == key) {
                return &property;
            }
        }
    }
    return nullptr;
}

const QList<QPair<QString, QString>>& Blocks() {
    static const QList<QPair<QString, QString>> blocks{
        {QStringLiteral("head"), QStringLiteral("Headline")},   {QStringLiteral("lookups"), QStringLiteral("Times looked up")},
        {QStringLiteral("card"), QStringLiteral("Name's card")}, {QStringLiteral("senses"), QStringLiteral("Meanings")},
        {QStringLiteral("forms"), QStringLiteral("Other forms")}, {QStringLiteral("buttons"), QStringLiteral("Card buttons")}};
    return blocks;
}

const QList<QPair<QString, QString>>& HeadParts() {
    static const QList<QPair<QString, QString>> parts{
        {QStringLiteral("headword"), QStringLiteral("Headword")}, {QStringLiteral("reading"), QStringLiteral("Reading")},
        {QStringLiteral("name"), QStringLiteral("名前")},          {QStringLiteral("reasons"), QStringLiteral("Inflection")},
        {QStringLiteral("pitch"), QStringLiteral("Pitch")},        {QStringLiteral("frequencies"), QStringLiteral("Frequencies")}};
    return parts;
}

namespace {
QString BlockSelector(const QString& id) {
    static const QHash<QString, QString> of{{QStringLiteral("head"), QStringLiteral(".entry > .head")},
                                            {QStringLiteral("lookups"), QStringLiteral(".entry > .lookups")},
                                            {QStringLiteral("card"), QStringLiteral(".entry > .name-card")},
                                            {QStringLiteral("senses"), QStringLiteral(".entry > .dictionary-name, .entry > .senses, .entry > .more")},
                                            {QStringLiteral("forms"), QStringLiteral(".entry > .forms")},
                                            {QStringLiteral("buttons"), QStringLiteral(".entry > .card-buttons")}};
    return of.value(id);
}

QString PartSelector(const QString& id) {
    static const QHash<QString, QString> of{{QStringLiteral("headword"), QStringLiteral(".head > .headword")},
                                            {QStringLiteral("reading"), QStringLiteral(".head > .reading")},
                                            {QStringLiteral("name"), QStringLiteral(".head > .tag.name")},
                                            {QStringLiteral("reasons"), QStringLiteral(".head > .reasons")},
                                            {QStringLiteral("pitch"), QStringLiteral(".head > .pitch")},
                                            {QStringLiteral("frequencies"), QStringLiteral(".head > .tags.frequencies")}};
    return of.value(id);
}
} // namespace

QString Css(const Design& design) {
    QString out;
    if (design.Empty()) {
        return out;
    }
    out += QStringLiteral("/* Furoshiki's popup design \"%1\" over %2: made by its customiser, not by hand. */\n").arg(design.name, design.base);
    for (const Element& element : Elements()) {
        for (const Property& property : element.properties) {
            const QJsonValue value = design.values.value(property.key);
            if (!value.isUndefined() && !value.isNull()) {
                out += Rules(property.key, Clean(property, value));
            }
        }
    }
    // The order of an entry's blocks: an entry is a column, each block its place in it.
    if (!design.entry_order.isEmpty()) {
        out += QStringLiteral(".popup .entry { display: flex !important; flex-direction: column !important; }\n");
        int place = 1;
        for (const QString& id : design.entry_order) {
            if (const QString selector = BlockSelector(id); !selector.isEmpty()) {
                out += QStringLiteral("%1 { order: %2 !important; }\n").arg(selector).arg(place++);
            }
        }
    }
    if (!design.head_order.isEmpty()) {
        int place = 1;
        for (const QString& id : design.head_order) {
            if (const QString selector = PartSelector(id); !selector.isEmpty()) {
                out += QStringLiteral("%1 { order: %2 !important; }\n").arg(selector).arg(place++);
            }
        }
    }
    return out;
}

QJsonValue Clean(const Property& property, const QJsonValue& value) {
    switch (property.kind) {
    case Property::Size:
    case Property::Number: {
        if (!value.isDouble() && !value.isString()) {
            return QJsonValue();
        }
        bool ok = true;
        const double n = value.isDouble() ? value.toDouble() : value.toString().toDouble(&ok);
        if (!ok) {
            return QJsonValue();
        }
        const double step = property.step > 0 ? property.step : 1;
        return std::clamp(std::round(n / step) * step, property.low, property.high);
    }
    case Property::Colour:
        return QColor::fromString(value.toString()).isValid() ? QJsonValue(QColor::fromString(value.toString()).name(QColor::HexArgb)) : QJsonValue();
    case Property::Toggle:
        return value.isBool() ? value : QJsonValue(value.toString() == QStringLiteral("1") || value.toString() == QStringLiteral("true"));
    case Property::Choice:
        for (const auto& choice : property.choices) {
            if (choice.first == value.toString()) {
                return value;
            }
        }
        return QJsonValue();
    }
    return QJsonValue();
}

Design FromJson(const QJsonObject& json) {
    Design design;
    design.name = json.value(QStringLiteral("name")).toString();
    design.base = json.value(QStringLiteral("base")).toString();
    const QJsonObject values = json.value(QStringLiteral("values")).toObject();
    for (auto at = values.begin(); at != values.end(); ++at) {
        if (const Property* property = PropertyOf(at.key())) {
            if (const QJsonValue clean = Clean(*property, at.value()); !clean.isNull() && !clean.isUndefined()) {
                design.values.insert(at.key(), clean);
            }
        }
    }
    const QJsonObject order = json.value(QStringLiteral("order")).toObject();
    design.entry_order = Strings(order.value(QStringLiteral("entry")));
    design.head_order = Strings(order.value(QStringLiteral("head")));
    return design;
}

QJsonObject ToJson(const Design& design) {
    QJsonObject json{{QStringLiteral("format"), 1}, {QStringLiteral("kind"), Kind}, {QStringLiteral("name"), design.name},
                     {QStringLiteral("base"), design.base}, {QStringLiteral("values"), design.values}};
    QJsonObject order;
    if (!design.entry_order.isEmpty()) {
        order.insert(QStringLiteral("entry"), QJsonArray::fromStringList(design.entry_order));
    }
    if (!design.head_order.isEmpty()) {
        order.insert(QStringLiteral("head"), QJsonArray::fromStringList(design.head_order));
    }
    if (!order.isEmpty()) {
        json.insert(QStringLiteral("order"), order);
    }
    return json;
}

QList<Design> InFolder(const QString& folder, const QString& base) {
    QList<Design> out;
    for (const QFileInfo& file : QDir(folder).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile in(file.filePath());
        if (!in.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject json = QJsonDocument::fromJson(in.readAll()).object();
        if (json.value(QStringLiteral("kind")).toString() != Kind) {
            continue;
        }
        Design design = FromJson(json);
        design.name = file.completeBaseName();
        if (base.isEmpty() || design.base == base) {
            out.append(design);
        }
    }
    return out;
}

std::optional<Design> Load(const QString& folder, const QString& name) {
    if (name.isEmpty()) {
        return std::nullopt;
    }
    for (const Design& design : InFolder(folder)) {
        if (design.name == name) {
            return design;
        }
    }
    return std::nullopt;
}

bool Save(const QString& folder, const Design& design) {
    static const QRegularExpression plain(QStringLiteral("^[\\w\\-]+$"));
    if (!plain.match(design.name).hasMatch()) {
        return false;
    }
    QDir().mkpath(folder);
    QSaveFile out(folder + QLatin1Char('/') + design.name + QStringLiteral(".json"));
    return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(ToJson(design)).toJson()) >= 0 && out.commit();
}

bool Remove(const QString& folder, const QString& name) {
    return !name.isEmpty() && QFile::remove(folder + QLatin1Char('/') + name + QStringLiteral(".json"));
}

QString FreeName(const QString& folder, const QString& base) {
    const QString first = base + QStringLiteral("-mine");
    QString name = first;
    for (int n = 2; QFileInfo::exists(folder + QLatin1Char('/') + name + QStringLiteral(".json")); ++n) {
        name = first + QLatin1Char('-') + QString::number(n);
    }
    return name;
}

QString Shown(const QString& name, const QString& base) {
    // "yomitan-dark-mine" over yomitan-dark: "Yomitan dark · mine".
    const auto words = [](QString text) {
        text.replace(QLatin1Char('-'), QLatin1Char(' '));
        if (!text.isEmpty()) {
            text[0] = text[0].toUpper();
        }
        return text;
    };
    if (!base.isEmpty() && name.startsWith(base + QLatin1Char('-'))) {
        return words(base) + QStringLiteral(" · ") + QString(name.mid(base.size() + 1)).replace(QLatin1Char('-'), QLatin1Char(' '));
    }
    return words(name);
}

QJsonObject PageConfig() {
    QJsonArray pick, sizes;
    // The most particular first: a tag in the headline is the frequencies' or 名前's, not "tags".
    QList<Element> order = Elements();
    std::stable_sort(order.begin(), order.end(), [](const Element& a, const Element& b) {
        const auto rank = [](const QString& id) { return id == QStringLiteral("popup") ? 3 : id == QStringLiteral("entries") ? 2 : id == QStringLiteral("tags") ? 1 : 0; };
        return rank(a.id) < rank(b.id);
    });
    for (const Element& element : order) {
        pick.append(QJsonArray{element.id, element.pick});
        if (!element.size.isEmpty()) {
            const Property* property = PropertyOf(element.size);
            sizes.append(QJsonObject{{QStringLiteral("element"), element.id}, {QStringLiteral("key"), element.size},
                                     {QStringLiteral("width"), element.id == QStringLiteral("card") || element.id == QStringLiteral("buttons")},
                                     {QStringLiteral("low"), property->low}, {QStringLiteral("high"), property->high}});
        }
    }
    QJsonArray blocks, parts;
    for (const auto& [id, label] : Blocks()) {
        blocks.append(QJsonArray{id, BlockSelector(id)});
    }
    for (const auto& [id, label] : HeadParts()) {
        parts.append(QJsonArray{id, PartSelector(id)});
    }
    return {{QStringLiteral("pick"), pick}, {QStringLiteral("sizes"), sizes}, {QStringLiteral("blocks"), blocks}, {QStringLiteral("parts"), parts}};
}

} // namespace Home::PopupDesign
