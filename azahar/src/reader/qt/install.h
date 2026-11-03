// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// All that the emulator's main window sees of the desktop reading layer: it is put in once
// (Install), told of games starting and stopping, and asked what the Reader menu is. This file
// and host.cpp are this fork's own; the windows behind them are the reader layer every reader
// fork shares.

#include <functional>
#include <QString>
#include "common/common_types.h"

class GameList;
class GRenderWindow;
class QMainWindow;
class QMenu;
class QWidget;

namespace Core {
class System;
}

namespace ReaderUi {

/// What the main window does for the reader.
struct MainWindowHooks {
    GRenderWindow* render_window{};
    /// Where the game list is, and the list and its placeholder themselves: the 3DS's HOME
    /// menu lies over them.
    QWidget* central{};
    GameList* game_list{};
    QWidget* game_list_placeholder{};
    /// A game is running (not paused).
    std::function<bool()> is_running;
    std::function<void()> pause;
    std::function<void()> resume;
    /// Sets the app's theme anew, after the 3DS's colours were switched on or off.
    std::function<void()> update_theme;
};

/// Adds the reader to the emulator's window: its layer over the game's two screens, its Save
/// sync, the 3DS's HOME menu over the game list, and the development channel when it is asked
/// for. The Reader menu is Menu(), for the main window to put in its menu bar.
void Install(QMainWindow* main_window, Core::System& system, MainWindowHooks hooks);
[[nodiscard]] QMenu* Menu();
/// The reader's layer over the game, while it is up (the main window's pointer is the
/// console's then); nothing otherwise.
[[nodiscard]] QWidget* Overlay();

/// A game was loaded and is about to run.
void OnGameBooted(u64 program_id, const QString& title);
void OnGameStopped();
/// The game runs, or is paused.
void OnEmulationRunning(bool running);
void OnApplicationActive(bool active);
/// Gives the app the 3DS's colours when they are wanted; true when it did. Called by the main
/// window after it has set its own theme.
bool ApplyLook(QMainWindow* main_window);

} // namespace ReaderUi
