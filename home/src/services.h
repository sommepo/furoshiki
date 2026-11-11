// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * The user's own services, as a paired save sync service names them (docs/pairing.md, "The
 * other services"): the study tracker, Furochibi's service, the playthrough guides, the home
 * page's Files and an Anki on another computer. Their addresses are settings of the device
 * (QSettings, "services/<name>"), taken from a pairing link or asked of the save sync service a
 * device is paired with already, so that one link sets a device up for all of them. The dev
 * line's: a published build has no save sync to be paired with.
 *
 * What is asked of them goes through the system's curl, as the hooks' files are fetched: an
 * https address needs nothing of Qt's that a Windows package may lack.
 */
namespace Home::Services {

/// A service's address without a "/" at its end, or empty when it is not known.
[[nodiscard]] QString Of(const QString& name);
/// Every service known, by name.
[[nodiscard]] QMap<QString, QString> All();
/// Keeps the addresses given (a name of PairingServices each); one left out stays as it is.
void Keep(const QMap<QString, QString>& services);

/// What a request came to: the answer's bytes, or why there is none.
struct Answer {
    QByteArray body;
    QString error;
    [[nodiscard]] bool Ok() const {
        return error.isEmpty();
    }
};
/**
 * Asks `address` (GET, or POST with `body` as JSON) and hands `done` the answer on the
 * caller's thread, while `context` lives. `bearer` goes as Authorization where it is not
 * empty. `seconds` is how long it may take.
 */
void Ask(QObject* context, const QString& address, const QByteArray& body, const QString& bearer, int seconds,
         std::function<void(const Answer&)> done);

/// Asks the paired save sync service for `path` ("/v1/stats"), each of its addresses in turn
/// until one answers.
void AskSync(QObject* context, const QStringList& sync_addresses, const QString& code, const QString& path,
             std::function<void(const Answer&)> done);

/// Where and when a game was read last on another device.
struct Elsewhere {
    QString device;
    QString day; ///< "2026-11-09"
};
/**
 * From the service's GET /v1/stats (every device's statistics record): for each game's ID the
 * device that read it on the latest day, this install (`own`) left out. A record without a
 * device's name is called by its app.
 */
[[nodiscard]] QHash<QString, Elsewhere> ReadElsewhere(const QByteArray& stats_json, const QString& own);

/// The services a save sync service's GET /v1/pair names; empty when it names none.
[[nodiscard]] QMap<QString, QString> FromPairAnswer(const QByteArray& json);
/**
 * Asks the save sync service the device is paired with (its addresses in the order tried, its
 * code) for the other services, keeps them, and says how many it has now (-1: none answered).
 */
void AskPaired(QObject* context, const QStringList& sync_addresses, const QString& code, std::function<void(int)> done);

} // namespace Home::Services
