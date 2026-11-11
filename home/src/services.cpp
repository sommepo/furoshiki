// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#ifndef READER_NO_SAVE_SYNC
#include "pairing.h"
#else
// A published build has no pairing (and so no service is ever named to it): the names alone.
namespace Home {
inline QStringList PairingServices() {
    return {QStringLiteral("tracker"), QStringLiteral("furochibi"), QStringLiteral("guides"), QStringLiteral("files"), QStringLiteral("anki")};
}
} // namespace Home
#endif
#include "services.h"

namespace Home::Services {

namespace {

QString Tidy(QString address) {
    address = address.trimmed();
    while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
    }
    return address;
}

} // namespace

QString Of(const QString& name) {
    return Tidy(QSettings().value(QStringLiteral("services/") + name).toString());
}

QMap<QString, QString> All() {
    QMap<QString, QString> out;
    for (const QString& name : PairingServices()) {
        const QString address = Of(name);
        if (!address.isEmpty()) {
            out.insert(name, address);
        }
    }
    return out;
}

void Keep(const QMap<QString, QString>& services) {
    QSettings settings;
    for (const QString& name : PairingServices()) {
        const QString address = Tidy(services.value(name));
        if (!address.isEmpty()) {
            settings.setValue(QStringLiteral("services/") + name, address);
        }
    }
}

void Ask(QObject* context, const QString& address, const QByteArray& body, const QString& bearer, int seconds,
         std::function<void(const Answer&)> done) {
    auto* curl = new QProcess(context);
    QStringList arguments{QStringLiteral("--silent"), QStringLiteral("--show-error"), QStringLiteral("--fail"),
                          QStringLiteral("--location"), QStringLiteral("--max-time"), QString::number(seconds)};
    if (!bearer.isEmpty()) {
        arguments << QStringLiteral("-H") << QStringLiteral("Authorization: Bearer ") + bearer;
    }
    if (!body.isNull()) {
        // The body by standard input: a line of Japanese is not for a command line on Windows.
        arguments << QStringLiteral("-H") << QStringLiteral("Content-Type: application/json") << QStringLiteral("--data-binary")
                  << QStringLiteral("@-");
    }
    arguments << address;
    const auto finish = [curl, done = std::move(done)](bool started) {
        Answer answer;
        if (!started) {
            answer.error = QStringLiteral("curl could not be started");
        } else if (curl->exitStatus() != QProcess::NormalExit || curl->exitCode() != 0) {
            const QString said = QString::fromUtf8(curl->readAllStandardError()).trimmed();
            answer.error = said.isEmpty() ? QStringLiteral("no answer") : said;
        } else {
            answer.body = curl->readAllStandardOutput();
        }
        curl->deleteLater();
        done(answer);
    };
    QObject::connect(curl, &QProcess::finished, context, [finish](int, QProcess::ExitStatus) { finish(true); });
    QObject::connect(curl, &QProcess::errorOccurred, context, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(false);
        }
    });
    curl->start(QStringLiteral("curl"), arguments);
    if (!body.isNull()) {
        curl->write(body);
    }
    curl->closeWriteChannel();
}

QMap<QString, QString> FromPairAnswer(const QByteArray& json) {
    QMap<QString, QString> out;
    const QJsonObject services = QJsonDocument::fromJson(json).object().value(QStringLiteral("services")).toObject();
    for (const QString& name : PairingServices()) {
        const QString address = Tidy(services.value(name).toString());
        if (!address.isEmpty()) {
            out.insert(name, address);
        }
    }
    return out;
}

void AskSync(QObject* context, const QStringList& sync_addresses, const QString& code, const QString& path,
             std::function<void(const Answer&)> done) {
    if (sync_addresses.isEmpty() || code.isEmpty()) {
        Answer none;
        none.error = QStringLiteral("not paired");
        done(none);
        return;
    }
    auto next = std::make_shared<std::function<void(int)>>();
    *next = [context, sync_addresses, code, path, done, next](int at) {
        QString address = sync_addresses[at].trimmed();
        if (!address.contains(QStringLiteral("://"))) {
            address = QStringLiteral("http://") + address;
        }
        Ask(context, Tidy(address) + path, QByteArray(), code, 6, [sync_addresses, done, next, at](const Answer& answer) {
            if (!answer.Ok() && at + 1 < sync_addresses.size()) {
                (*next)(at + 1);
                return;
            }
            done(answer);
        });
    };
    (*next)(0);
}

QHash<QString, Elsewhere> ReadElsewhere(const QByteArray& stats_json, const QString& own) {
    QHash<QString, Elsewhere> out;
    for (const QJsonValue& value : QJsonDocument::fromJson(stats_json).object().value(QStringLiteral("devices")).toArray()) {
        const QJsonObject record = value.toObject();
        if (record.value(QStringLiteral("id")).toString().compare(own, Qt::CaseInsensitive) == 0) {
            continue;
        }
        QString device = record.value(QStringLiteral("device")).toString().trimmed();
        if (device.isEmpty()) {
            device = record.value(QStringLiteral("app")).toString();
        }
        for (const QJsonValue& each : record.value(QStringLiteral("days")).toArray()) {
            const QJsonObject day = each.toObject();
            const QString game = day.value(QStringLiteral("game")).toString();
            const QString when = day.value(QStringLiteral("day")).toString();
            if (game.isEmpty() || when.isEmpty() || day.value(QStringLiteral("chars")).toDouble() <= 0) {
                continue;
            }
            if (!out.contains(game) || when > out[game].day) {
                out[game] = Elsewhere{device, when};
            }
        }
    }
    return out;
}

void AskPaired(QObject* context, const QStringList& sync_addresses, const QString& code, std::function<void(int)> done) {
    AskSync(context, sync_addresses, code, QStringLiteral("/v1/pair"), [done](const Answer& answer) {
        if (!answer.Ok()) {
            done(-1);
            return;
        }
        Keep(FromPairAnswer(answer.body));
        done(static_cast<int>(All().size()));
    });
}

} // namespace Home::Services
