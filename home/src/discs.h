// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/**
 * A game on several discs is one title (the user, 2026-11-13: "make multi disc games show as one
 * thing"): its files are named alike but for "(Disc N)" (Redump's naming), in one folder. The
 * lists keep its first disc; the game is given the others as it starts (the menu's "Change disc"),
 * and starts on the disc it was left on. The same rule as Furoshiki for Android's (Discs.kt).
 */
namespace Home::Discs {

/// The disc's number in a file's name, or 0 for a game on one disc.
[[nodiscard]] int Number(const QString& name);
/// The path with the disc taken out of its name: alike for every disc of a game.
[[nodiscard]] QString GroupKey(const QString& path);
/// A title without its "(Disc N)".
[[nodiscard]] QString WithoutDisc(const QString& title);
/// Every disc of the game `path` is one of, in their order, `path` among them; only `path` for
/// a game on one disc. Discs are files of the same kind beside it.
[[nodiscard]] QStringList Of(const QString& path);

} // namespace Home::Discs
