// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

class QWidget;

namespace ReaderUi {

/// The reader's own windows, each run until it is closed. The game is paused by the caller.

/// The lines read so far, newest first: words in them can be looked up and cards made.
void RunBacklogDialog(QWidget* parent);
/// The line being read in English with its grammar (reader/desktop/explain.h).
void RunExplainDialog(QWidget* parent);
/// A line's English, where it was explained before; empty otherwise.
QString ExplainedTranslation(const QString& line);
/// What the game's script offers to mark by hand (reader.progress): its routes, say.
void RunProgressDialog(QWidget* parent);
/// Whether the running game's script offers anything to mark.
bool ProgressOffered();
/// Reading statistics: today, the games, the reading days and the sessions.
void RunStatisticsDialog(QWidget* parent);
/// The installed dictionaries: importing, their order, switching off, deleting.
void RunDictionariesDialog(QWidget* parent);
/// Where cards go: the address of AnkiConnect, the deck, the note type and what each field
/// gets.
void RunAnkiSetupDialog(QWidget* parent);
/**
 * A first dictionary without looking for one: Jitendex (Stephen Kraus's, CC BY-SA 4.0), its
 * Yomitan file fetched from its own releases with the system's curl (39 MB) and installed as
 * a file chosen by hand is. False when it was called off or could not be had; `problem` then
 * says why, or stays empty when the user called it off.
 */
bool FetchStarterDictionary(QWidget* parent, QString* problem);
/**
 * Cards set up without a window, where Anki answers at its address and nothing is chosen yet:
 * the deck cards are mined into (one named for mining, else the first that is not Default) and
 * the note type most of its notes are, its fields filled by their names. Says what it chose
 * ("Mining · Lapis"), or nothing when Anki did not answer or no note type could be told.
 */
QString SetUpAnkiByItself();
/// The reader's settings.
void RunSettingsDialog(QWidget* parent);
/// What opens a program's own popup customiser (Furoshiki's), from the Reader settings' popup
/// page; none in an app that has none.
std::function<void()>& PopupCustomiser();
/// Where a line is explained instead of by Claude Code on this computer: a service of the
/// user's (Furoshiki names Furochibi's, where the device knows it); none or empty: here.
std::function<QString()>& ExplainService();
/// An Anki on another computer of the user's (its AnkiConnect address), asked when the one the
/// setup names does not answer; none or empty: only the setup's.
std::function<QString()>& AnkiElsewhere();
/// What shows a program's own settings in place of the reader's settings window (Furoshiki's
/// pages); none in an app that has none.
std::function<void()>& SettingsPages();

} // namespace ReaderUi
