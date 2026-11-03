// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include "play_settings.h"

namespace Home {

namespace {

const QString All = QStringLiteral("all");
const QString Consoles = QStringLiteral("consoles");
const QString Games = QStringLiteral("games");

QString NumberText(double value) {
    // Whole numbers as such ("3", not "3.0"); others as short as they go.
    if (std::fabs(value - std::round(value)) < 1e-9) {
        return QString::number(static_cast<qint64>(std::llround(value)));
    }
    return QString::number(value, 'g', 6);
}

} // namespace

bool PlaySetting::Common() const {
    // A console's own key begins with its console's key and a dot; the common ones are the
    // interface's (picture.*, sound.*, emulation.*).
    // The program's own popup.* (popup_style.h) are common too.
    return key.startsWith(QStringLiteral("picture.")) || key.startsWith(QStringLiteral("sound.")) ||
           key.startsWith(QStringLiteral("emulation.")) || key.startsWith(QStringLiteral("popup."));
}

bool PlaySetting::Hidden() const {
    return key == QStringLiteral("picture.crt") || key == QStringLiteral("emulation.speed") || kind == Text ||
           kind == Folder;
}

QString PlaySetting::Moved(const QString& value, int steps) const {
    switch (kind) {
    case Switch:
        return (steps % 2 != 0) == (value == QStringLiteral("1")) ? QStringLiteral("0") : QStringLiteral("1");
    case Choice: {
        if (choices.isEmpty()) {
            return value;
        }
        const int at = std::max(0, static_cast<int>(choices.indexOf(value)));
        const int n = static_cast<int>(choices.size());
        return choices[((at + steps) % n + n) % n];
    }
    case Number: {
        const double step = this->step > 0 ? this->step : 1;
        bool ok = false;
        double now = value.toDouble(&ok);
        if (!ok) {
            now = default_value.toDouble();
        }
        const double next = std::clamp(std::round((now + steps * step) / step) * step, low, high);
        return NumberText(next);
    }
    default:
        return value;
    }
}

QString PlaySetting::Shown(const QString& value) const {
    switch (kind) {
    case Switch:
        return value == QStringLiteral("1") ? QStringLiteral("ON") : QStringLiteral("OFF");
    case Number:
        if (key == QStringLiteral("picture.resolution")) {
            return value + QStringLiteral("×");
        }
        if (key == QStringLiteral("sound.volume")) {
            return value + QStringLiteral("%");
        }
        return value;
    default:
        return value.toUpper();
    }
}

QString CommonLabel(const QString& key) {
    static const QList<QPair<QString, QString>> labels{
        {QStringLiteral("picture.resolution"), QStringLiteral("Internal resolution")},
        {QStringLiteral("picture.renderer"), QStringLiteral("Renderer")},
        {QStringLiteral("picture.aspect"), QStringLiteral("Shape of the picture")},
        {QStringLiteral("picture.vsync"), QStringLiteral("Vertical sync")},
        {QStringLiteral("picture.frame_limit"), QStringLiteral("At the console's own speed")},
        {QStringLiteral("sound.volume"), QStringLiteral("Volume")},
    };
    for (const auto& [name, label] : labels) {
        if (name == key) {
            return label;
        }
    }
    return key;
}

PlaySettings::PlaySettings(QString file_) : file(std::move(file_)) {
    QFile in(file);
    if (in.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(in.readAll()).object();
    }
}

QJsonObject PlaySettings::LayerOf(const QString& console, const QString& game) const {
    if (console.isEmpty()) {
        return root.value(All).toObject();
    }
    if (game.isEmpty()) {
        return root.value(Consoles).toObject().value(console).toObject();
    }
    return root.value(Games).toObject().value(console).toObject().value(game).toObject();
}

PlaySettings::Resolved PlaySettings::Value(const PlaySetting& setting, const QString& console,
                                           const QString& game) const {
    if (!game.isEmpty()) {
        const QJsonValue value = LayerOf(console, game).value(setting.key);
        if (value.isString()) {
            return {value.toString(), Layer::Game};
        }
    }
    if (!console.isEmpty()) {
        const QJsonValue value = LayerOf(console, {}).value(setting.key);
        if (value.isString()) {
            return {value.toString(), Layer::Console};
        }
    }
    if (setting.Common()) {
        const QJsonValue value = LayerOf({}, {}).value(setting.key);
        if (value.isString()) {
            return {value.toString(), Layer::All};
        }
    }
    return {setting.default_value, Layer::Default};
}

bool PlaySettings::IsSet(const QString& key, const QString& console, const QString& game) const {
    return LayerOf(console, game).value(key).isString();
}

void PlaySettings::Set(const QString& key, const QString& value, const QString& console, const QString& game) {
    QJsonObject layer = LayerOf(console, game);
    if (value.isEmpty()) {
        layer.remove(key);
    } else {
        layer.insert(key, value);
    }
    if (console.isEmpty()) {
        root.insert(All, layer);
    } else if (game.isEmpty()) {
        QJsonObject consoles = root.value(Consoles).toObject();
        consoles.insert(console, layer);
        root.insert(Consoles, consoles);
    } else {
        QJsonObject games = root.value(Games).toObject();
        QJsonObject of_console = games.value(console).toObject();
        of_console.insert(game, layer);
        games.insert(console, of_console);
        root.insert(Games, games);
    }
    root.insert(QStringLiteral("format"), 1);
}

QList<QPair<QString, QString>> PlaySettings::ValuesFor(const PlayConsole& console, const QString& game) const {
    QList<QPair<QString, QString>> out;
    for (const PlaySetting& setting : console.settings) {
        // The reader's own (the CRT picture) are handed over by the reader itself.
        // The popup's (popup_style.h) are the program's: no core is handed them.
        if (setting.key == QStringLiteral("picture.crt") || setting.key == QStringLiteral("emulation.speed") ||
            setting.key.startsWith(QStringLiteral("popup."))) {
            continue;
        }
        const Resolved resolved = Value(setting, console.key, game);
        if (!resolved.value.isEmpty()) {
            out.append({setting.key, resolved.value});
        }
    }
    return out;
}

QStringList PlaySettings::Folders(const QString& console) const {
    QStringList out;
    for (const QJsonValue& value : root.value(QStringLiteral("folders")).toObject().value(console).toArray()) {
        if (!value.toString().isEmpty()) {
            out.append(value.toString());
        }
    }
    return out;
}

void PlaySettings::SetFolders(const QString& console, const QStringList& folders) {
    QJsonObject all = root.value(QStringLiteral("folders")).toObject();
    if (folders.isEmpty()) {
        all.remove(console);
    } else {
        all.insert(console, QJsonArray::fromStringList(folders));
    }
    root.insert(QStringLiteral("folders"), all);
}

bool PlaySettings::HasGame(const QString& console, const QString& game) const {
    return !game.isEmpty() && !LayerOf(console, game).isEmpty();
}

bool PlaySettings::OldAppsRead() const {
    return root.value(QStringLiteral("old_apps_read")).toBool();
}

void PlaySettings::SetOldAppsRead() {
    root.insert(QStringLiteral("old_apps_read"), true);
    root.insert(QStringLiteral("format"), 1);
}

bool PlaySettings::Save() const {
    if (file.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return out.commit();
}

} // namespace Home
