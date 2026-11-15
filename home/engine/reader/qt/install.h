// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

// All that the program's game window (src/main_window.cpp) sees of the reader's windows: the
// program's install.h, where a fork's is what its emulator's own files see. The reader's
// windows are the shared ones (shared/reader/qt); src/reader_host.cpp ties them to the core
// that runs the game.

class QMainWindow;
class QMenu;

#include <QStringList>
class QWidget;

namespace Home {
class CoreSession;
}

namespace ReaderUi {

/// Once, on the main thread: the reader's windows, over `screen` (the widget the core draws
/// the game into) in `window`, and the development channel if READER_DESKTOP_CONTROL names one.
void Install(QMainWindow* window, QWidget* screen);
/// The core whose game starts next (its console is the reader's already: Home::ApplyConsole).
void UseSession(Home::CoreSession* session);
/// Reads from a source of the program's own too (lines another program sends, text copied):
/// ReaderController::Use. After Install.
void UseSource(class ReadingSource* source);
/// Whether the game is what the window shows (not Furoshiki's own page).
void SetScreenShown(bool shown);
/// The program stopped the game for a menu of its own, or let it go on: the reader's pause
/// ends with the program's.
void ProgramPaused(bool paused);

/// The pointer over the game, in the screen widget's own units, before the game hears of it.
/// True when the reader keeps it.
bool PointerPress(float x, float y, bool left_button);
bool PointerMove(float x, float y);
bool PointerRelease(float x, float y);
/// A key pressed with Ctrl held (its letter, small: 'g'). True when it is one of the reader's:
/// G pauses the game to look words up, T pauses the statistics, D makes a card of the
/// sentence, H opens the backlog.
bool Shortcut(int key);
/// What the development channel's "home" does: ends the game, back to Furoshiki's page.
void SetEndGame(void (*end_game)(void* context), void* context);
/// Called on the main thread once the reader has done with a game that stopped (its
/// statistics are on their way to the database: ReaderSession::AfterWrites comes after them).
void SetGameStopped(void (*stopped)(void* context), void* context);
/// The program's own commands in the development channel ("slot save 3", "disc 2"): called
/// on the main thread with a command the reader does not know; true when it answered it.
void SetProgramCommands(std::function<bool(const QString& name, const QStringList& parts, const std::function<void(const QString&)>& reply)> commands);
/// A line said over the game for a moment (a savestate written, a disc changed).
void Notify(const QString& text);
/// Does an action of the settings (reader_backlog, furoshiki_guide...): a button's the
/// controller map gives one.
void RunAction(const QString& name);
/// While set, a controller's back buttons (1 to 5: the Steam Controller's grips and its "..."
/// button) go here first; true: taken. Empty: they do their actions again.
void CaptureBackButtons(std::function<bool(int)> capture);

/// The buttons the reader's stick pointer may have (-1 for none first), and one's name.
[[nodiscard]] QList<int> PointerButtons();
[[nodiscard]] QString PointerButtonName(int button);
/// Gives the pointer that button (-1: none, every button is the game's), a game playing at once.
void SetPointerButton(int button);
/// The game's picture as the core has it now (reader_core.h, READER_CORE_GRAB_PICTURE), no
/// wider than `max_width`, handed to `done` on the main thread; a null picture when there is
/// none. The core copies it on its own thread and goes on: nothing waits for it.
void GrabPicture(int max_width, std::function<void(const class QImage&)> done);
/// Where the game's picture and the console's screens (0 the upper, 1 the lower) are drawn in
/// the screen widget, in its own units; empty where not known or not shown.
[[nodiscard]] class QRectF PictureArea();
[[nodiscard]] class QRectF ScreenArea(int screen);
/// The Reader menu (statistics, dictionaries, Anki, settings, ...).
[[nodiscard]] QMenu* Menu();

} // namespace ReaderUi
