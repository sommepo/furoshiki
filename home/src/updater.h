// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QByteArray>
#include <QObject>
#include <QString>

/**
 * A newer build of the program from the user's own Files page (the home page's Downloads,
 * services.h's "files"): its index names the newest build made of each thing with its SHA-256,
 * and the program is offered when its own file is another. Taking it fetches the file beside
 * the program, checks it against the index, and puts it in the program's place (the running
 * file is renamed aside, which Windows allows of a program that runs), for the next start.
 * The program alone: its cores come as before. The dev line's.
 */
namespace Home::Updater {

/// The index's entry this program is: the Windows program's, or the Linux one's.
[[nodiscard]] QString EntryId();

struct Offer {
    QString url;     ///< Where the build is, whole
    QString sha256;
    QString version; ///< As the index says it, may be empty
    QString changes; ///< The first of what changed, may be empty
    [[nodiscard]] bool IsNone() const {
        return url.isEmpty();
    }
};
/// The entry `id` of the index at `files` (its base address) as an offer; none when the index
/// has no such entry or its build is the one whose SHA-256 is `own`.
[[nodiscard]] Offer Read(const QByteArray& index_json, const QString& files, const QString& id, const QString& own);
/// The SHA-256 of a file, in small hexadecimal; empty when it cannot be read.
[[nodiscard]] QString Sha256(const QString& file);

/// Asks the Files page whether it has another build of this program.
void Check(QObject* context, const QString& files, std::function<void(const Offer&, const QString& error)> done);
/// Takes `offer`: `done` gets nothing when the new program is in place, else what went wrong.
void Take(QObject* context, const Offer& offer, std::function<void(const QString& error)> done);

} // namespace Home::Updater
