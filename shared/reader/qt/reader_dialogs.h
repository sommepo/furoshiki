// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class QWidget;

namespace ReaderUi {

/// The reader's own windows, each run until it is closed. The game is paused by the caller.

/// The lines read so far, newest first: words in them can be looked up and cards made.
void RunBacklogDialog(QWidget* parent);
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
/// The reader's settings.
void RunSettingsDialog(QWidget* parent);

} // namespace ReaderUi
