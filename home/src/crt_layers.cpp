// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <QStringList>
#include "crt_layers.h"
#include "reader/core/crt.h"

namespace Home::CrtLayers {

namespace {

bool Known(const QString& key) {
    for (const Reader::Crt::Param& param : Reader::Crt::Params()) {
        if (key == QLatin1String(param.key)) {
            return true;
        }
    }
    return false;
}

void Apply(Values& values, const QString& text) {
    const Values more = Parse(text);
    for (auto it = more.cbegin(); it != more.cend(); ++it) {
        values.insert(it.key(), it.value());
    }
}

QString Number(double value) {
    // As the reader writes them: thousandths, a point whatever the language.
    return QString::number(std::lround(value * 1000.0) / 1000.0, 'f', 3);
}

} // namespace

Values Parse(const QString& text) {
    Values out;
    for (const QString& pair : text.split(u';', Qt::SkipEmptyParts)) {
        const qsizetype at = pair.indexOf(u'=');
        if (at <= 0) {
            continue;
        }
        const QString key = pair.left(at).trimmed();
        bool ok = false;
        const double value = pair.mid(at + 1).trimmed().toDouble(&ok);
        if (ok && Known(key)) {
            out.insert(key, value);
        }
    }
    return out;
}

QString Join(const Values& values) {
    QStringList parts;
    for (const Reader::Crt::Param& param : Reader::Crt::Params()) {
        const QString key = QString::fromLatin1(param.key);
        if (values.contains(key)) {
            parts.append(key + u'=' + Number(values.value(key)));
        }
    }
    return parts.join(u';');
}

Values Initial() {
    Values out;
    for (const Reader::Crt::Param& param : Reader::Crt::Params()) {
        out.insert(QString::fromLatin1(param.key), param.initial);
    }
    return out;
}

Values Resolved(const PlaySettings& play, const QString& base, const QString& console, const QString& game) {
    Values values = Initial();
    Apply(values, base);
    Apply(values, play.Raw(Key));
    if (!console.isEmpty()) {
        Apply(values, play.Raw(Key, console));
        if (!game.isEmpty()) {
            Apply(values, play.Raw(Key, console, game));
        }
    }
    return values;
}

Values Below(const PlaySettings& play, const QString& base, const QString& console, const QString& game) {
    if (!console.isEmpty() && !game.isEmpty()) {
        return Resolved(play, base, console);
    }
    if (!console.isEmpty()) {
        return Resolved(play, base, {});
    }
    Values values = Initial();
    Apply(values, base);
    return values;
}

void Store(PlaySettings& play, const QString& base, const Values& values, const QString& console, const QString& game) {
    const Values below = Below(play, base, console, game);
    Values own;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!below.contains(it.key()) || std::fabs(below.value(it.key()) - it.value()) > 0.0005) {
            own.insert(it.key(), it.value());
        }
    }
    play.Set(Key, Join(own), console, game);
}

Layer Target(const PlaySettings& play, const QString& console, const QString& game) {
    if (!console.isEmpty() && !game.isEmpty() && play.IsSet(Key, console, game)) {
        return {console, game};
    }
    if (!console.isEmpty() && play.IsSet(Key, console)) {
        return {console, {}};
    }
    return {};
}

} // namespace Home::CrtLayers
