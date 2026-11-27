// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Reading statistics shared between devices through a folder that the user keeps in sync
// themself (Syncthing, a network share, a cloud folder). Each device writes one file of its own,
// <folder>/<device id>.json, and reads every other device's file to add its reading to the
// Statistics page. As no two devices ever write the same file, a sync tool never has two
// versions of one file to choose between. The daily, weekly, monthly and yearly goals are one
// more file, goals.json, the newest written wins. The Android app writes and reads the same
// files (android/app/.../SharedStats.kt).
//
//   {"format": "furoshiki.stats", "version": 1, "device": "<id>", "name": "Z13",
//    "platform": "windows", "written": <ms>, "games": {"<id>": "<title>"},
//    "systems": {"<id>": "<console>"},
//    "days": [{"day": "2026-11-27", "game": "<id>", "chars": 123, "active_ms": 456}]}

#include <QJsonObject>
#include <QString>
#include "library.h"

namespace Home::SharedStats {

/// The folder chosen (Settings › Sync & About › Statistics folder), else <shared folder>/stats.
[[nodiscard]] QString Folder();
void SetFolder(const QString& folder);

/// This device's id in the folder: made once, kept in the program's settings.
[[nodiscard]] QString DeviceId();

/// This device's own reading, as a file: the library's days before any other device's are added.
[[nodiscard]] QJsonObject Record(const Library& library);
/// Writes the record to <folder>/<device id>.json when it differs from what is there.
/// False when the folder cannot be written.
bool WriteOwn(const QString& folder, const QJsonObject& record);

/// Adds every other device's file in the folder to the library's days, game days and totals.
/// Returns how many devices were added.
int AddOthers(Library& library, const QString& folder);

/// Goals: what goals.json holds and when it was written (0 for none).
struct Goals {
    int daily{}, weekly{}, monthly{}, yearly{};
    qint64 written{};
};
[[nodiscard]] Goals ReadGoals(const QString& folder);
bool WriteGoals(const QString& folder, const Goals& goals);

} // namespace Home::SharedStats
