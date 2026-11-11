// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include "look.h"

namespace Home {

QStringList SplitTop(const QString& text, QChar separator) {
    QStringList parts;
    QString part;
    int depth = 0;
    for (const QChar c : text) {
        if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            --depth;
        }
        if (c == separator && depth == 0) {
            if (!part.trimmed().isEmpty() || separator == QLatin1Char(',')) {
                parts.append(part.trimmed());
            }
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.trimmed().isEmpty()) {
        parts.append(part.trimmed());
    }
    return parts;
}

QColor ParseColour(const QString& given, bool* ok) {
    const QString text = given.trimmed().toLower();
    QColor colour;
    if (text == QStringLiteral("transparent")) {
        colour = QColor(0, 0, 0, 0);
    } else if (text.startsWith(QStringLiteral("rgb"))) {
        const int open = text.indexOf(QLatin1Char('(')), close = text.lastIndexOf(QLatin1Char(')'));
        QString inside = text.mid(open + 1, close - open - 1);
        inside.replace(QLatin1Char('/'), QLatin1Char(' '));
        const QStringList parts = inside.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        if (open > 0 && parts.size() >= 3) {
            const auto channel = [](const QString& part) {
                return part.endsWith(QLatin1Char('%')) ? part.chopped(1).toDouble() * 2.55 : part.toDouble();
            };
            const double alpha = parts.size() > 3 ? (parts[3].endsWith(QLatin1Char('%')) ? parts[3].chopped(1).toDouble() / 100
                                                                                           : parts[3].toDouble())
                                                  : 1.0;
            colour = QColor::fromRgbF(std::clamp(channel(parts[0]) / 255, 0.0, 1.0), std::clamp(channel(parts[1]) / 255, 0.0, 1.0),
                                      std::clamp(channel(parts[2]) / 255, 0.0, 1.0), std::clamp(alpha, 0.0, 1.0));
        }
    } else if (text.startsWith(QLatin1Char('#')) && (text.size() == 4 || text.size() == 5)) {
        // #rgb and #rgba: each digit twice.
        QString full = QStringLiteral("#");
        for (int i = 1; i < text.size(); ++i) {
            full += text[i];
            full += text[i];
        }
        colour = QColor::fromString(text.size() == 5 ? QStringLiteral("#") + full.right(2) + full.mid(1, 6) : full);
    } else if (text.startsWith(QLatin1Char('#')) && text.size() == 9) {
        // CSS puts the alpha last, Qt first.
        colour = QColor::fromString(QStringLiteral("#") + text.right(2) + text.mid(1, 6));
    } else {
        colour = QColor::fromString(text);
    }
    if (ok != nullptr) {
        *ok = colour.isValid();
    }
    return colour.isValid() ? colour : QColor(0, 0, 0, 0);
}

double ParseLength(const QString& given, bool* percent) {
    QString text = given.trimmed().toLower();
    if (percent != nullptr) {
        *percent = text.endsWith(QLatin1Char('%'));
    }
    if (text.endsWith(QLatin1Char('%'))) {
        return text.chopped(1).toDouble() / 100;
    }
    for (const char* unit : {"px", "deg"}) {
        if (text.endsWith(QLatin1String(unit))) {
            text.chop(static_cast<int>(std::strlen(unit)));
        }
    }
    return text.toDouble();
}

namespace {

bool IsLength(const QString& text) {
    static const QRegularExpression length(QStringLiteral("^-?(\\d+\\.?\\d*|\\.\\d+)(px|%|deg)?$"));
    return length.match(text.trimmed().toLower()).hasMatch();
}

/// "colour [place [place]]" as one stop or two.
void AddStops(const QString& text, QList<Stop>& stops) {
    const QStringList parts = SplitTop(text, QLatin1Char(' '));
    if (parts.isEmpty()) {
        return;
    }
    // The colour may come first or last.
    int colour_at = IsLength(parts.first()) ? static_cast<int>(parts.size()) - 1 : 0;
    const QColor colour = ParseColour(parts[colour_at]);
    QList<Stop> made;
    for (int i = 0; i < parts.size(); ++i) {
        if (i == colour_at) {
            continue;
        }
        Stop stop{colour};
        stop.place = ParseLength(parts[i], &stop.percent);
        made.append(stop);
    }
    if (made.isEmpty()) {
        made.append(Stop{colour});
    }
    stops.append(made);
}

Layer ParseLayer(const QString& text) {
    Layer layer;
    const int open = text.indexOf(QLatin1Char('('));
    if (open < 0 || text.startsWith(QStringLiteral("rgb"))) {
        layer.kind = Layer::Colour;
        layer.colour = ParseColour(text);
        return layer;
    }
    // The function, what is inside its brackets, and what follows (a position and a size).
    int depth = 0, close = -1;
    for (int i = open; i < text.size(); ++i) {
        depth += text[i] == QLatin1Char('(') ? 1 : text[i] == QLatin1Char(')') ? -1 : 0;
        if (depth == 0) {
            close = i;
            break;
        }
    }
    const QString name = text.left(open).trimmed().toLower();
    const QString inside = text.mid(open + 1, close - open - 1);
    const QString after = close >= 0 ? text.mid(close + 1).trimmed() : QString();
    layer.repeating = name.startsWith(QStringLiteral("repeating-"));
    layer.kind = name.endsWith(QStringLiteral("radial-gradient")) ? Layer::Radial
                 : name.endsWith(QStringLiteral("conic-gradient")) ? Layer::Conic
                                                                     : Layer::Linear;
    if (layer.kind == Layer::Conic) {
        layer.angle = 0; // CSS's conic starts at the top
    }
    QStringList parts = SplitTop(inside);
    if (!parts.isEmpty()) {
        const QString first = parts.first().toLower();
        if (layer.kind == Layer::Linear && first.endsWith(QStringLiteral("deg"))) {
            layer.angle = ParseLength(first);
            parts.removeFirst();
        } else if (layer.kind == Layer::Linear && first.startsWith(QStringLiteral("to "))) {
            const QString to = first.mid(3).trimmed();
            layer.angle = to == QStringLiteral("top") ? 0 : to == QStringLiteral("right") ? 90 : to == QStringLiteral("left") ? 270 : 180;
            parts.removeFirst();
        } else if (layer.kind == Layer::Radial &&
                   (first.startsWith(QStringLiteral("ellipse")) || first.startsWith(QStringLiteral("circle")) ||
                    first.startsWith(QStringLiteral("at ")))) {
            const int at = first.indexOf(QStringLiteral("at "));
            if (at >= 0) {
                const QStringList place = first.mid(at + 3).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                const auto share = [](const QString& word) {
                    return word == QStringLiteral("left") || word == QStringLiteral("top")       ? 0.0
                           : word == QStringLiteral("right") || word == QStringLiteral("bottom") ? 1.0
                           : word == QStringLiteral("center")                                    ? 0.5
                                                                                                 : ParseLength(word);
                };
                layer.at = QPointF(place.size() > 0 ? share(place[0]) : 0.5, place.size() > 1 ? share(place[1]) : 0.5);
            }
            parts.removeFirst();
        } else if (layer.kind == Layer::Conic && first.startsWith(QStringLiteral("from "))) {
            layer.angle = ParseLength(first.mid(5));
            parts.removeFirst();
        }
    }
    for (const QString& part : parts) {
        AddStops(part, layer.stops);
    }
    // "0 0/2px 2px": the tile's size after the slash.
    const int slash = after.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        const QStringList size = after.mid(slash + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!size.isEmpty()) {
            layer.tile_w = ParseLength(size[0]);
            layer.tile_h = size.size() > 1 ? ParseLength(size[1]) : layer.tile_w;
        }
    }
    return layer;
}

} // namespace

Fill ParseFill(const QString& text) {
    Fill fill;
    for (const QString& part : SplitTop(text)) {
        if (!part.isEmpty()) {
            fill.layers.append(ParseLayer(part));
        }
    }
    return fill;
}

QList<Shadow> ParseShadows(const QString& text) {
    QList<Shadow> out;
    if (text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("none")) {
        return out;
    }
    for (const QString& part : SplitTop(text)) {
        Shadow shadow;
        QList<double> lengths;
        for (const QString& word : SplitTop(part, QLatin1Char(' '))) {
            if (word == QStringLiteral("inset")) {
                shadow.inset = true;
            } else if (IsLength(word)) {
                lengths.append(ParseLength(word));
            } else {
                shadow.colour = ParseColour(word);
            }
        }
        shadow.dx = lengths.value(0);
        shadow.dy = lengths.value(1);
        shadow.blur = lengths.value(2);
        shadow.spread = lengths.value(3);
        out.append(shadow);
    }
    return out;
}

Theme::Theme(QString key_, QJsonObject values_) : key(std::move(key_)), values(std::move(values_)) {}

QString Theme::Name() const {
    return values.value(QStringLiteral("name")).toString(key);
}

int Theme::Order() const {
    return values.value(QStringLiteral("order")).toInt(100);
}

bool Theme::Has(const QString& name) const {
    return values.contains(name) && !values.value(name).isNull();
}

namespace {
QString Resolve(const QJsonObject& values, QString text, int depth) {
    static const QRegularExpression var(QStringLiteral("var\\(--([A-Za-z0-9_-]+)\\)"));
    if (depth > 8) {
        return text;
    }
    for (;;) {
        const QRegularExpressionMatch match = var.match(text);
        if (!match.hasMatch()) {
            return text;
        }
        const QJsonValue value = values.value(match.captured(1));
        const QString replaced = value.isDouble() ? QString::number(value.toDouble()) : Resolve(values, value.toString(), depth + 1);
        text.replace(match.capturedStart(), match.capturedLength(), replaced);
    }
}
} // namespace

QString Theme::Text(const QString& name) const {
    const QJsonValue value = values.value(name);
    return value.isDouble() ? QString::number(value.toDouble()) : Resolve(values, value.toString(), 0);
}

QColor Theme::Colour(const QString& name, const QColor& otherwise) const {
    if (!Has(name)) {
        return otherwise;
    }
    bool ok = false;
    const QColor colour = ParseColour(Text(name), &ok);
    return ok ? colour : otherwise;
}

Fill Theme::FillOf(const QString& name) const {
    return Has(name) ? ParseFill(Text(name)) : Fill{};
}

QList<Shadow> Theme::Shadows(const QString& name) const {
    return Has(name) ? ParseShadows(Text(name)) : QList<Shadow>{};
}

double Theme::Number(const QString& name, double otherwise) const {
    const QJsonValue value = values.value(name);
    return value.isDouble() ? value.toDouble() : value.isString() ? ParseLength(Text(name)) : otherwise;
}

QJsonObject Theme::Object(const QString& name) const {
    return values.value(name).toObject();
}

double Theme::Number(const QString& object, const QString& name, double otherwise) const {
    const QJsonValue value = Object(object).value(name);
    return value.isDouble() ? value.toDouble() : value.isString() ? ParseLength(value.toString()) : otherwise;
}

QString Theme::Text(const QString& object, const QString& name) const {
    const QJsonValue value = Object(object).value(name);
    return value.isDouble() ? QString::number(value.toDouble()) : Resolve(values, value.toString(), 0);
}

TypeSpec Theme::Type(const QString& role) const {
    const QJsonObject type = Object(QStringLiteral("type")).value(role).toObject();
    TypeSpec spec;
    spec.font = type.value(QStringLiteral("font")).toString();
    spec.size = type.value(QStringLiteral("size")).toDouble(16);
    spec.spacing = type.value(QStringLiteral("spacing")).toDouble(0);
    spec.line = type.value(QStringLiteral("line")).toDouble(0);
    spec.bold = type.value(QStringLiteral("bold")).toBool(false);
    return spec;
}

FuzzLevel Theme::Fuzz(const QString& level) const {
    const QJsonObject fuzz = Object(QStringLiteral("fuzz")).value(level).toObject();
    FuzzLevel out;
    out.shift = fuzz.value(QStringLiteral("shift")).toDouble(0);
    out.blur = fuzz.value(QStringLiteral("blur")).toDouble(0);
    out.contrast = fuzz.value(QStringLiteral("contrast")).toDouble(1);
    return out;
}

QStringList Theme::Fonts() const {
    QStringList out;
    for (const QString& role : TypeRoles()) {
        const QString font = Type(role).font;
        if (!font.isEmpty() && !out.contains(font)) {
            out.append(font);
        }
    }
    return out;
}

QStringList TypeRoles() {
    return {QStringLiteral("head"),   QStringLiteral("clock"), QStringLiteral("file"),  QStringLiteral("title"),
            QStringLiteral("sub"),    QStringLiteral("stats"), QStringLiteral("number"), QStringLiteral("row"),
            QStringLiteral("empty"),  QStringLiteral("when"),  QStringLiteral("tab"),   QStringLiteral("lr")};
}

QStringList RequiredValues() {
    return {QStringLiteral("name"),          QStringLiteral("page"),      QStringLiteral("bg"),
            QStringLiteral("panel-bg"),      QStringLiteral("panel-shadow"), QStringLiteral("frame"),
            QStringLiteral("frame-dk"),      QStringLiteral("card-frame"), QStringLiteral("txt"),
            QStringLiteral("txt-sh"),        QStringLiteral("sel-txt"),   QStringLiteral("sel-sh"),
            QStringLiteral("dim"),           QStringLiteral("dim-sh"),
            QStringLiteral("accent"),        QStringLiteral("accent-glow"), QStringLiteral("accent-groove"),
            QStringLiteral("rail-hi"),       QStringLiteral("rail-lo"),   QStringLiteral("divot"),
            QStringLiteral("divot-sh"),      QStringLiteral("line"),      QStringLiteral("tab-bg"),
            QStringLiteral("tab-txt"),       QStringLiteral("tab-sh"),    QStringLiteral("tab-shadow"),
            QStringLiteral("tab-on"),        QStringLiteral("thumb-frame"), QStringLiteral("thumb-shadow"),
            QStringLiteral("orb-glow"),      QStringLiteral("clock"),     QStringLiteral("layout"),
            QStringLiteral("file-text"),     QStringLiteral("type"),      QStringLiteral("fuzz")};
}

QStringList Theme::Missing() const {
    QStringList missing;
    for (const QString& name : RequiredValues()) {
        if (!Has(name)) {
            missing.append(name);
        }
    }
    for (const QString& role : TypeRoles()) {
        if (Type(role).font.isEmpty()) {
            missing.append(QStringLiteral("type.") + role);
        }
    }
    for (const QString& level : {QStringLiteral("light"), QStringLiteral("heavy")}) {
        if (!Object(QStringLiteral("fuzz")).contains(level)) {
            missing.append(QStringLiteral("fuzz.") + level);
        }
    }
    // Colours that do not read as colours, backgrounds without a layer, var() left unresolved.
    for (const char* name : {"page", "panel-bg", "frame", "frame-dk", "card-frame", "txt", "sel-txt", "dim", "accent",
                             "rail-hi", "rail-lo", "divot", "line", "tab-txt", "tab-on", "thumb-frame"}) {
        bool ok = false;
        if (Has(QLatin1String(name))) {
            (void)ParseColour(Text(QLatin1String(name)), &ok);
            if (!ok) {
                missing.append(QStringLiteral("a colour: ") + QLatin1String(name));
            }
        }
    }
    for (const QString& name : values.keys()) {
        if (values.value(name).isString() && Text(name).contains(QStringLiteral("var("))) {
            missing.append(QStringLiteral("a value for var() in ") + name);
        }
    }
    return missing;
}

QList<Theme> LoadThemes(const QString& folder) {
    QList<Theme> themes;
    const QDir dir(folder);
    for (const QString& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            themes.append(Theme(name.chopped(5), document.object()));
        }
    }
    std::stable_sort(themes.begin(), themes.end(), [](const Theme& a, const Theme& b) { return a.Order() < b.Order(); });
    return themes;
}

QSize FirstWindowSize(QSize room) {
    QSize size(1920, 1080);
    if (size.width() > room.width() || size.height() > room.height()) {
        size.scale(room, Qt::KeepAspectRatio);
    }
    return size;
}

} // namespace Home
