// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include "popup_controls.h"

namespace Home::PopupControls {

namespace {

const QString Begin = QStringLiteral("/* furoshiki-controls ");
const QString End = QStringLiteral("/* furoshiki-controls end */");

/// A colour as CSS: #rrggbb, or rgba() when it is see-through.
QString Css(const QString& value) {
    const QColor colour = QColor::fromString(value);
    if (colour.alpha() == 255) {
        return colour.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(colour.red())
        .arg(colour.green())
        .arg(colour.blue())
        .arg(QString::number(colour.alphaF(), 'g', 3));
}

QString FontStack(const QString& key) {
    if (key == QStringLiteral("mincho")) {
        return QStringLiteral("\"Noto Serif JP\", \"Yu Mincho\", \"Hiragino Mincho ProN\", serif");
    }
    if (key == QStringLiteral("rounded")) {
        return QStringLiteral("\"M PLUS Rounded 1c\", \"Hiragino Maru Gothic ProN\", \"Noto Sans JP\", sans-serif");
    }
    return QStringLiteral("\"Noto Sans JP\", \"Noto Sans CJK JP\", \"Yu Gothic UI\", \"Meiryo\", sans-serif");
}

/// Where the block is in `css`: its start and its length, or -1.
std::pair<qsizetype, qsizetype> Find(const QString& css) {
    const qsizetype start = css.indexOf(Begin);
    if (start < 0) {
        return {-1, 0};
    }
    const qsizetype end = css.indexOf(End, start);
    if (end < 0) {
        return {-1, 0};
    }
    return {start, end + End.size() - start};
}

} // namespace

const QList<Control>& Controls() {
    static const QList<Control> controls{
        {QStringLiteral("font"), QStringLiteral("Font"), QStringLiteral("Text"), Control::Font},
        {QStringLiteral("headword"), QStringLiteral("Word"), QStringLiteral("Text"), Control::Size, 10, 64},
        {QStringLiteral("gloss"), QStringLiteral("Meanings"), QStringLiteral("Text"), Control::Size, 8, 40},
        {QStringLiteral("small"), QStringLiteral("Reading and notes"), QStringLiteral("Text"), Control::Size, 6, 32},
        {QStringLiteral("tag"), QStringLiteral("Tags"), QStringLiteral("Text"), Control::Size, 6, 32},
        {QStringLiteral("frequency"), QStringLiteral("Frequency"), QStringLiteral("Text"), Control::Size, 6, 32},
        {QStringLiteral("background"), QStringLiteral("Background"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("text"), QStringLiteral("Text"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("dim"), QStringLiteral("Quieter text"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("border_colour"), QStringLiteral("Border"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("tag_background"), QStringLiteral("Tag background"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("tag_text"), QStringLiteral("Tag text"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("unknown"), QStringLiteral("Not-in-Anki mark"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("button"), QStringLiteral("Buttons"), QStringLiteral("Colours"), Control::Colour},
        {QStringLiteral("border"), QStringLiteral("Border width"), QStringLiteral("Box"), Control::Size, 0, 12},
        {QStringLiteral("corners"), QStringLiteral("Corners"), QStringLiteral("Box"), Control::Size, 0, 32},
        {QStringLiteral("padding"), QStringLiteral("Inner space"), QStringLiteral("Box"), Control::Size, 0, 40},
        {QStringLiteral("spacing"), QStringLiteral("Space between words"), QStringLiteral("Box"), Control::Size, 0, 32},
    };
    return controls;
}

const QList<QPair<QString, QString>>& Fonts() {
    static const QList<QPair<QString, QString>> fonts{{QStringLiteral("gothic"), QStringLiteral("Gothic")},
                                                      {QStringLiteral("mincho"), QStringLiteral("Mincho (serif)")},
                                                      {QStringLiteral("rounded"), QStringLiteral("Rounded")}};
    return fonts;
}

QString Block(const Values& values) {
    if (values.isEmpty()) {
        return {};
    }
    QJsonObject kept;
    for (auto it = values.begin(); it != values.end(); ++it) {
        kept.insert(it.key(), it.value());
    }
    const auto has = [&](const char* key) { return values.contains(QString::fromLatin1(key)); };
    const auto value = [&](const char* key) { return values.value(QString::fromLatin1(key)); };
    const auto px = [&](const char* key) {
        return QStringLiteral("calc(%1px * var(--scale)) !important").arg(value(key));
    };
    QStringList out;
    out << Begin + QString::fromUtf8(QJsonDocument(kept).toJson(QJsonDocument::Compact)) + QStringLiteral(" */");
    out << QStringLiteral("/* Made by the controls above the editor: change them there. */");
    QStringList root;
    if (has("font")) {
        root << QStringLiteral("--font: %1 !important;").arg(FontStack(value("font")));
    }
    if (has("text")) {
        root << QStringLiteral("--text: %1 !important;").arg(Css(value("text")));
    }
    if (has("dim")) {
        root << QStringLiteral("--dim: %1 !important;").arg(Css(value("dim")));
    }
    if (!root.isEmpty()) {
        out << QStringLiteral(":root { %1 }").arg(root.join(QLatin1Char(' ')));
    }
    QStringList popup;
    if (has("font")) {
        popup << QStringLiteral("font-family: var(--font) !important;");
    }
    if (has("background")) {
        popup << QStringLiteral("background: %1 !important;").arg(Css(value("background")));
    }
    if (has("text")) {
        popup << QStringLiteral("color: %1 !important;").arg(Css(value("text")));
    }
    if (has("border_colour")) {
        popup << QStringLiteral("border-color: %1 !important;").arg(Css(value("border_colour")));
    }
    if (has("border")) {
        popup << QStringLiteral("border-width: %1px !important;").arg(value("border"));
        if (value("border") != QStringLiteral("0")) {
            popup << QStringLiteral("border-style: solid;");
        }
    }
    if (has("corners")) {
        popup << QStringLiteral("border-radius: %1px !important;").arg(value("corners"));
    }
    if (has("padding")) {
        popup << QStringLiteral("padding: %1;").arg(px("padding"));
    }
    if (!popup.isEmpty()) {
        out << QStringLiteral(".popup { %1 }").arg(popup.join(QLatin1Char(' ')));
    }
    if (has("border_colour")) {
        out << QStringLiteral(".entry + .entry { border-top-color: %1 !important; }").arg(Css(value("border_colour")));
    }
    if (has("headword")) {
        out << QStringLiteral(".headword { font-size: %1; }").arg(px("headword"));
    }
    if (has("gloss")) {
        out << QStringLiteral(".glosses, .forms .form { font-size: %1; }").arg(px("gloss"));
    }
    if (has("small")) {
        out << QStringLiteral(".reading, .lookups, .reasons, .pitch, .number, .notes, .dictionary-name { font-size: %1; }")
                   .arg(px("small"));
    }
    if (has("dim")) {
        out << QStringLiteral(".reading, .lookups, .number, .notes, .dictionary-name, .headword rt { color: %1 !important; }")
                   .arg(Css(value("dim")));
    }
    QStringList tag;
    if (has("tag")) {
        tag << QStringLiteral("font-size: %1;").arg(px("tag"));
    }
    if (has("tag_background")) {
        tag << QStringLiteral("background: %1 !important;").arg(Css(value("tag_background")));
    }
    if (has("tag_text")) {
        tag << QStringLiteral("color: %1 !important;").arg(Css(value("tag_text")));
    }
    if (!tag.isEmpty()) {
        out << QStringLiteral(".tag { %1 }").arg(tag.join(QLatin1Char(' ')));
    }
    if (has("frequency")) {
        out << QStringLiteral(".tag.frequency, .tag.frequency .tag-value { font-size: %1; }").arg(px("frequency"));
    }
    if (has("unknown")) {
        out << QStringLiteral(".entry.unknown .headword { background: color-mix(in srgb, %1 35%, transparent) !important; "
                              "box-shadow: none !important; }")
                   .arg(Css(value("unknown")));
    }
    if (has("button")) {
        out << QStringLiteral(".card, .card.book { background: %1 !important; }").arg(Css(value("button")));
    }
    if (has("spacing")) {
        out << QStringLiteral(".entry { padding-top: %1px !important; padding-bottom: %1px !important; }")
                   .arg(value("spacing"));
    }
    out << End;
    return out.join(QLatin1Char('\n'));
}

Values Read(const QString& css) {
    const auto [start, length] = Find(css);
    if (start < 0) {
        return {};
    }
    const qsizetype from = start + Begin.size();
    const qsizetype to = css.indexOf(QStringLiteral(" */"), from);
    if (to < 0) {
        return {};
    }
    const QJsonObject kept = QJsonDocument::fromJson(css.mid(from, to - from).toUtf8()).object();
    Values values;
    for (auto it = kept.begin(); it != kept.end(); ++it) {
        if (!it.value().toString().isEmpty()) {
            values.insert(it.key(), it.value().toString());
        }
    }
    return values;
}

QString With(const QString& css, const Values& values) {
    const QString block = Block(values);
    const auto [start, length] = Find(css);
    if (start >= 0) {
        QString out = css;
        out.replace(start, length, block);
        if (block.isEmpty()) {
            // Taken out: the end of the file as it was before the block came.
            while (out.endsWith(QLatin1Char('\n'))) {
                out.chop(1);
            }
            if (!out.isEmpty()) {
                out += QLatin1Char('\n');
            }
        }
        return out;
    }
    if (block.isEmpty()) {
        return css;
    }
    QString out = css;
    while (out.endsWith(QLatin1Char('\n'))) {
        out.chop(1);
    }
    return out.isEmpty() ? block + QLatin1Char('\n') : out + QStringLiteral("\n\n") + block + QLatin1Char('\n');
}

} // namespace Home::PopupControls
