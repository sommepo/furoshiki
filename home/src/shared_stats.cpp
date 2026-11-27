// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shared_stats.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QUuid>

namespace Home::SharedStats {

namespace {

const QString KeyFolder = QStringLiteral("stats/shared_folder");
const QString KeyDevice = QStringLiteral("stats/device_id");
const QString Format = QStringLiteral("furoshiki.stats");

QString Platform() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QJsonObject ReadJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool WriteJson(const QString& path, const QJsonObject& object) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace

QString Folder() {
    const QString chosen = QSettings().value(KeyFolder).toString().trimmed();
    return chosen.isEmpty() ? SharedFolder() + QStringLiteral("/stats") : chosen;
}

void SetFolder(const QString& folder) {
    QSettings().setValue(KeyFolder, folder.trimmed());
}

QString DeviceId() {
    QSettings settings;
    QString id = settings.value(KeyDevice).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(KeyDevice, id);
    }
    return id;
}

QJsonObject Record(const Library& library) {
    QJsonObject games;
    QJsonObject systems;
    QJsonArray days;
    for (const GameDay& day : library.game_days) {
        if (day.chars <= 0 && day.active_ms <= 0) {
            continue;
        }
        games.insert(day.game, day.title);
        if (!day.system.isEmpty()) {
            systems.insert(day.game, day.system);
        }
        days.append(QJsonObject{{QStringLiteral("day"), day.day},
                                {QStringLiteral("game"), day.game},
                                {QStringLiteral("chars"), static_cast<double>(day.chars)},
                                {QStringLiteral("active_ms"), static_cast<double>(day.active_ms)}});
    }
    QString name = QHostInfo::localHostName();
    if (name.isEmpty()) {
        name = QSysInfo::machineHostName();
    }
    return QJsonObject{{QStringLiteral("format"), Format},
                       {QStringLiteral("version"), 1},
                       {QStringLiteral("device"), DeviceId()},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("platform"), Platform()},
                       {QStringLiteral("games"), games},
                       {QStringLiteral("systems"), systems},
                       {QStringLiteral("days"), days}};
}

bool WriteOwn(const QString& folder, const QJsonObject& record) {
    if (!QDir().mkpath(folder)) {
        return false;
    }
    const QString path = QDir(folder).filePath(record.value(QStringLiteral("device")).toString() + QStringLiteral(".json"));
    // Written only when the reading changed: a sync tool sends each write to every device.
    QJsonObject was = ReadJson(path);
    was.remove(QStringLiteral("written"));
    if (was == record) {
        return true;
    }
    QJsonObject out = record;
    out.insert(QStringLiteral("written"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    return WriteJson(path, out);
}

int AddOthers(Library& library, const QString& folder) {
    const QString own = DeviceId();
    QMap<QString, Day> days;
    for (const Day& day : library.days) {
        days.insert(day.day, day);
    }
    QHash<QString, QString> system_of;
    for (const Game& game : library.games) {
        system_of.insert(game.id, game.system);
    }
    int added = 0;
    const QDir dir(folder);
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QJsonObject root = ReadJson(info.absoluteFilePath());
        if (root.value(QStringLiteral("format")).toString() != Format || root.value(QStringLiteral("version")).toInt() > 1) {
            continue;
        }
        const QString device = root.value(QStringLiteral("device")).toString();
        if (device.isEmpty() || device == own) {
            continue;
        }
        const QJsonObject games = root.value(QStringLiteral("games")).toObject();
        const QJsonObject systems = root.value(QStringLiteral("systems")).toObject();
        for (const QJsonValue& value : root.value(QStringLiteral("days")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString day = entry.value(QStringLiteral("day")).toString();
            const QString game = entry.value(QStringLiteral("game")).toString();
            const qint64 chars = static_cast<qint64>(entry.value(QStringLiteral("chars")).toDouble());
            const qint64 active = static_cast<qint64>(entry.value(QStringLiteral("active_ms")).toDouble());
            if (day.isEmpty() || (chars <= 0 && active <= 0)) {
                continue;
            }
            Day& total = days[day];
            total.day = day;
            total.chars += chars;
            total.active_ms += active;
            QString system = systems.value(game).toString();
            if (system.isEmpty()) {
                system = system_of.value(game);
            }
            QString title = games.value(game).toString();
            library.game_days.append(GameDay{day, game, title.isEmpty() ? game : title, system, chars, active});
            library.chars += chars;
            library.active_ms += active;
        }
        ++added;
    }
    library.days = days.values();
    return added;
}

Goals ReadGoals(const QString& folder) {
    const QJsonObject root = ReadJson(QDir(folder).filePath(QStringLiteral("goals.json")));
    Goals goals;
    goals.daily = root.value(QStringLiteral("daily")).toInt();
    goals.weekly = root.value(QStringLiteral("weekly")).toInt();
    goals.monthly = root.value(QStringLiteral("monthly")).toInt();
    goals.yearly = root.value(QStringLiteral("yearly")).toInt();
    goals.written = static_cast<qint64>(root.value(QStringLiteral("written")).toDouble());
    return goals;
}

bool WriteGoals(const QString& folder, const Goals& goals) {
    if (!QDir().mkpath(folder)) {
        return false;
    }
    return WriteJson(QDir(folder).filePath(QStringLiteral("goals.json")),
                     QJsonObject{{QStringLiteral("format"), QStringLiteral("furoshiki.goals")},
                                 {QStringLiteral("daily"), goals.daily},
                                 {QStringLiteral("weekly"), goals.weekly},
                                 {QStringLiteral("monthly"), goals.monthly},
                                 {QStringLiteral("yearly"), goals.yearly},
                                 {QStringLiteral("written"), static_cast<double>(goals.written)}});
}

} // namespace Home::SharedStats
