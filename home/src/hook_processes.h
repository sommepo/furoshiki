// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The programs running on this computer that a hook could be started against (docs/hooks.md):
// on Windows each program with a window, by its window's title; on Linux this user's processes
// (no window titles are asked for there). READER_HOME_FAKE_PROGRAMS stands in for them in tests
// and pictures: "PID|name|window title" separated by ";", a PID of "?" meaning the real process
// of that name, left out while none runs.

#include <optional>
#include <QList>
#include <QString>

namespace Home::Hooks {

struct Program {
    qint64 pid{};
    QString name;   ///< Its program file's name: "game.exe"
    QString path;
    QString window; ///< Its window's title, where it has one
    bool is32{};    ///< A 32-bit program on a 64-bit system: the engine must be of its size
};

[[nodiscard]] QList<Program> Programs();
/// The running program of that file name (as a title remembers it), the newest if several.
[[nodiscard]] std::optional<Program> ProgramNamed(const QString& name);
/// Whether the process is still there.
[[nodiscard]] bool Runs(qint64 pid);
/// The program a window of this computer is of (Windows::Entry's handle, else its title as a
/// program's window has it), from `programs`; empty when none is.
[[nodiscard]] Program ProgramOfWindow(const QList<Program>& programs, quintptr handle, const QString& title);

} // namespace Home::Hooks
