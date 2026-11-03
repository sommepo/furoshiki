// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// All that the emulator's Qt frontend sees of the reader.

#include <functional>
#include <QList>
#include <QPointF>
#include <QString>

class QMainWindow;
class QWidget;

namespace ReaderUi {

/// A game of the emulator's list, as the PS2-style menu shows it.
struct HomeGame {
    QString path;
    QString name;
    QString serial;
    /// The file of its cover picture, or empty.
    QString cover;
};

/// What the main window does for the menu and the reader.
struct MainWindowHooks {
    /// The widget the game list is in, and the one the game's picture is shown in while a
    /// game runs in the main window.
    QWidget* game_list{};
    QWidget* picture_area{};
    std::function<QList<HomeGame>()> games;
    std::function<void(const QString& path)> start_game;
    std::function<void()> add_game_folder;
    /// The emulator's own windows, by name: "settings", "controllers".
    std::function<void(const QString& what)> open;
    /// Sets the app's theme anew, after the console's colours were switched on or off.
    std::function<void()> update_theme;
    std::function<void(bool paused)> set_paused;
};

/// Adds the reader to the emulator's window: its menu, its layer over the game's picture and
/// the PS2-style menu over the game list.
void Install(QMainWindow* main_window, MainWindowHooks hooks);
/// The game list was read anew.
void GamesChanged();
/// The game list's reading going on now is cut short (true: GameListWidget::cancelRefresh), or a
/// new one begins (false): Reader Home is told the games only from a reading that was not cut.
void GameListCut(bool cut);
/// Gives the app the console's colours when they are wanted; true when it did. Called by
/// the main window after it has set its own theme.
bool ApplyLook(QMainWindow* main_window);

// The pointer (the mouse, or a finger) on the surface the game is drawn in, before the game
// gets it. True when the reader keeps the event.
bool PointerPress(const QPointF& position, bool left_button);
bool PointerMove(const QPointF& position);
bool PointerRelease(const QPointF& position);

} // namespace ReaderUi
