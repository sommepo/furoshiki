// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include "services.h"
#include "updater.h"

namespace Home::Updater {

QString EntryId() {
#ifdef _WIN32
    return QStringLiteral("furoshiki-windows-exe");
#else
    return QStringLiteral("furoshiki-linux-program");
#endif
}

Offer Read(const QByteArray& index_json, const QString& files, const QString& id, const QString& own) {
    for (const QJsonValue& value : QJsonDocument::fromJson(index_json).object().value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("id")).toString() != id) {
            continue;
        }
        const QString sha = item.value(QStringLiteral("sha256")).toString().toLower();
        const QString url = item.value(QStringLiteral("url")).toString();
        if (sha.size() != 64 || url.isEmpty() || sha == own.toLower()) {
            return {};
        }
        Offer offer;
        offer.url = url.startsWith(QLatin1Char('/')) ? files + url : url;
        offer.sha256 = sha;
        offer.version = item.value(QStringLiteral("version")).toString();
        offer.changes = item.value(QStringLiteral("changes")).toArray().first().toString();
        return offer;
    }
    return {};
}

QString Sha256(const QString& file) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&in) ? QString::fromLatin1(hash.result().toHex()) : QString();
}

void Check(QObject* context, const QString& files, std::function<void(const Offer&, const QString&)> done) {
    if (files.isEmpty()) {
        done({}, QStringLiteral("no Files page is known"));
        return;
    }
    Services::Ask(context, files + QStringLiteral("/downloads/data/index.json"), QByteArray(), QString(), 15,
                  [files, done](const Services::Answer& answer) {
                      if (!answer.Ok()) {
                          done({}, answer.error);
                          return;
                      }
                      done(Read(answer.body, files, EntryId(), Sha256(QCoreApplication::applicationFilePath())), {});
                  });
}

void Take(QObject* context, const Offer& offer, std::function<void(const QString&)> done) {
    const QString program = QCoreApplication::applicationFilePath();
    const QString fetched = program + QStringLiteral(".new");
    QFile::remove(fetched);
    auto* curl = new QProcess(context);
    QObject::connect(curl, &QProcess::errorOccurred, context, [curl, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            curl->deleteLater();
            done(QStringLiteral("curl could not be started"));
        }
    });
    QObject::connect(curl, &QProcess::finished, context, [curl, offer, program, fetched, done](int code, QProcess::ExitStatus status) {
        const QString said = QString::fromUtf8(curl->readAllStandardError()).trimmed();
        curl->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            QFile::remove(fetched);
            done(said.isEmpty() ? QStringLiteral("the build could not be fetched") : said);
            return;
        }
        // Only the very file the index names goes in the program's place.
        if (Sha256(fetched) != offer.sha256) {
            QFile::remove(fetched);
            done(QStringLiteral("what was fetched is not the build the Files page names"));
            return;
        }
        const QString aside = program + QStringLiteral(".old");
        QFile::remove(aside);
        if (!QFile::rename(program, aside)) {
            QFile::remove(fetched);
            done(QStringLiteral("the program's file could not be moved aside"));
            return;
        }
        if (!QFile::rename(fetched, program)) {
            QFile::rename(aside, program); // as it was
            done(QStringLiteral("the new program could not be put in place"));
            return;
        }
        QFile::setPermissions(program, QFile::permissions(aside));
        done({});
    });
    curl->start(QStringLiteral("curl"), {QStringLiteral("--silent"), QStringLiteral("--show-error"), QStringLiteral("--fail"),
                                         QStringLiteral("--location"), QStringLiteral("--max-time"), QStringLiteral("600"),
                                         QStringLiteral("-o"), fetched, offer.url});
}

} // namespace Home::Updater
