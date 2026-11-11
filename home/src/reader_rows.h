// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace ReaderApp {
class ReaderSettings;
}

/**
 * The reader's own settings as rows of Furoshiki's settings pages (home_window.cpp's
 * SettingRows name them and say which page each stands on): one list of what each is kept as,
 * what it may be and how it is said, so that the pages are the one place they are set. A row
 * is a switch, a number or a choice; what it is now is asked of the settings' own accessors,
 * so a default is written down once, there.
 */
namespace ReaderRows {

/// What the row named `name` is set to, as the pages say it; empty when it is none of these.
[[nodiscard]] QString Value(const ReaderApp::ReaderSettings& settings, const QString& name);
/// Goes `step` on (a switch flips, a choice takes its next, a number its step); false when the
/// row is none of these. The caller tells the reader its settings changed.
bool Step(ReaderApp::ReaderSettings& settings, const QString& name, int step);
/// Whether the row was set by the user (it then carries a mark), and the row put back to what
/// it is by itself; false when the row is none of these.
[[nodiscard]] bool IsSet(const ReaderApp::ReaderSettings& settings, const QString& name);
bool Reset(ReaderApp::ReaderSettings& settings, const QString& name);
/// Whether the row is a number (the arrows change it, Enter does not).
[[nodiscard]] bool IsNumber(const QString& name);
/// Every row's name, for the tests.
[[nodiscard]] QStringList Names();

} // namespace ReaderRows
