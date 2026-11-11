// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QTimer>
#include "reader/core/types.h"
#include "reader/desktop/discord_presence.h"
#include "reader/desktop/screen_text.h"
#include "reader/qt/host.h"
#include "reader/qt/lookup_flow.h"
#include "reader/qt/reader_cursor.h"
#include "reader/qt/reader_overlay.h"

class QAction;
class QDialog;
class QMainWindow;
class QMenu;

namespace Core {
class System;
}

namespace ReaderUi {

/**
 * The reader layer of the desktop app: makes the text a game shows something to click. A
 * click on a word of the game's own text looks it up and shows the popup beside it, no click
 * that begins in a text box reaches the game, and the Reader menu has the rest (cards, the
 * backlog, statistics, dictionaries, the settings). The desktop sibling of the Android app's
 * ReaderOverlay.
 */
class ReaderController : public QObject, public ReaderPointerFilter {
    Q_OBJECT

public:
    /// What the emulator's window does for the reader (see host.h).
    using Host = EmulatorHost;

    ReaderController(QMainWindow* main_window_, QWidget* render_window_, Host host_);
    ~ReaderController() override;

    /// The Reader menu, for the main window's menu bar.
    [[nodiscard]] QMenu* Menu() const {
        return menu;
    }
    /// Adds an entry of the app's own to the Reader menu (before its last part) that opens one of
    /// the reader's windows (run with the game paused, RunDialog): the Save sync window, say.
    QAction* AddWindowEntry(const QString& text, const QString& name,
                            std::function<void(QWidget* parent)> run);

    /// Reads from `source` too: a session begins when it says so (ReadingSource::Began) and ends
    /// with it. The core's (CoreSource) is used from the first; an app adds its own.
    void Use(class ReadingSource* source);

    // ---- From the main window ----

    /// A game was loaded and is about to run: the core's source begins.
    void OnGameBooted(const QString& game_id, const QString& title);
    void OnGameStopped();
    /// The game runs, or is paused.
    void OnEmulationRunning(bool running);
    void OnApplicationActive(bool active);

    // ---- ReaderPointerFilter ----

    bool OnPointerPress(const QPointF& position, bool left_button) override;
    void OnPointerMove(const QPointF& position) override;
    void OnPointerRelease(const QPointF& position) override;

    // ---- For the development control channel and for tests ----

    [[nodiscard]] ReaderOverlay* Overlay() const {
        return overlay.data();
    }
    [[nodiscard]] LookupFlow& Lookups() {
        return *lookups;
    }
    [[nodiscard]] StickCursor* Cursor() const {
        return cursor;
    }
    [[nodiscard]] const QList<ReaderApp::PlacedText>& Placed();
    /// Where the game's picture is in the render window, in the window's own coordinates.
    [[nodiscard]] QRectF PictureArea() const;
    /// Where one of the console's screens (0 the upper, 1 the lower) is drawn in the render
    /// window; nothing when it is not shown there.
    [[nodiscard]] QRectF ScreenArea(int screen) const;
    /// The game's picture as it is drawn now, no wider than `max_width`. A null image when
    /// there is none to be had.
    void CaptureFrame(int max_width, std::function<void(const QImage&)> callback);
    /// The render window as drawn now, where the emulator can give it (host.window_picture);
    /// a null image otherwise.
    [[nodiscard]] QImage WindowPicture() const;
    void TriggerAction(const QString& name);
    /// Says a sentence over the game's picture for a moment (what the app's save sync did, say).
    void Notify(const QString& text);
    /// A controller's back button went down (1 to 5): what the settings say it does.
    void OnBackButton(int button);
    /// The game goes on again though the reader did not say so (the emulator's own pause
    /// key, say): the reader's "paused" is over too.
    void GamePauseEnded();
    /// Whether the game stands still because the user asked the reader for it.
    [[nodiscard]] bool GamePausedByUser() const {
        return user_paused;
    }
    /// The Steam Controller without Steam (Windows), for the development channel.
    [[nodiscard]] class SteamControllerBridge* Steam() const {
        return steam;
    }
    /// The controller's back buttons, for the development channel.
    [[nodiscard]] class ReaderPad* Pad() const {
        return pad;
    }

    /**
     * The app has no setup screen. In its place, at the first start and once each: whether
     * saves are to be synced (and how), then a dictionary, then Anki. Each question opens the
     * window that does it, or is put off with "Later"; what is set up already is not asked.
     */
    void FirstStart();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Swallow: the press called a card's question off and does nothing else.
    // GoOn and Answer are touch play's: a press beside the text, and one on a choice's answer.
    enum class DownKind { None, Scrim, Box, Character, Swallow, GoOn, Answer };
    struct Down {
        DownKind kind{DownKind::None};
        QPointF position;
        bool moved{};
        qint64 at{};    ///< when the press began (ms): a long one on an answer looks it up
        int answer{-1}; ///< which answer of the choice, for Answer
        ReaderApp::PresentedLine presented;
        int offset{};
        QRectF rows;
        qreal near_x{};
    };

    void SourceBegan(class ReadingSource* source, const QString& game_id, const QString& title);
    void SourceEnded(class ReadingSource* source);
    /// Tells the session whether what it has is being read now: the game goes (or stands still
    /// at the user's word), or the source has no game to stand still; the app in front, none of
    /// the reader's windows open.
    void UpdateReading();
    /// The source being read, while a session runs.
    QPointer<class ReadingSource> source;
    /// Whether that source has a game under the reader (true while there is none, as before).
    bool source_has_game{true};
    void BuildMenu();
    void RefreshPlaced();
    /// Tells the text server's clients that asked where the texts on screen are.
    void PublishPlaced();
    [[nodiscard]] ReaderApp::PlacedText::Hit HitAt(const QPointF& position);
    /**
     * True while the pointer at `position` (with `hit` under it) is the open popup's: on it, on
     * its word, on a word it covers, or between its word and it (ReaderApp::KeptByPopup).
     * Nothing there is looked up, and it does not count as leaving the popup.
     */
    [[nodiscard]] bool KeptByPopup(const QPointF& position,
                                   const ReaderApp::PlacedText::Hit& hit) const;
    void LookUp(const ReaderApp::PresentedLine& presented, int offset, const QRectF& rows,
                qreal near_x, bool quiet);
    [[nodiscard]] LookupFlow::Anchor AnchorFor(const QRectF& rows, qreal near_x) const;
    void UpdateMarks();
    void UpdateSessionLine();
    /// The Discord status follows the session: the game, what was read, how fast.
    void UpdateDiscord();
    ReaderApp::DiscordPresence discord{this};
    qint64 session_started_s{};
    void UpdateMenu();
    void SyncWindows();
    void HoverAt(const QPointF& position);
    /// Looks up the word at `position`, at once, unless it is the open popup's (KeptByPopup).
    void RestOn(const QPointF& position);
    /// The pointer (the mouse, or the controller's cursor) is at `position` of the picture,
    /// or on the popup: tells the lookups whether that is on the open popup or its word.
    void PointerOver(const QPointF& position, bool on_popup);
    /// Whether the pointer is on the open popup or on its word right now.
    [[nodiscard]] bool PointerInsideNow();
    /// Whether the pointer is over the popup or the question about a card's audio now.
    [[nodiscard]] bool PointerOnOwnWindow() const;
    void ApplyCursorSettings();
    /// Now and then, a picture of the game's text box for the text server's clients.
    void PublishBoxPicture();
    void HomeCursor();
    void OnCursorMoved(bool by_player);
    void OnCursorActive(bool active);
    void CursorClick();
    void ShowCursor();
    void RefreshSticks();
    void MineSentence();
    /// Runs one of the reader's windows with the game paused, and lets the game go on after.
    void RunDialog(const std::function<void(QWidget* parent)>& run);
    /// The Reader menu in the middle of the game's picture, for the controller's menu button.
    void OpenMenuOverGame();
    /// Gives the controller to the reader's windows, or back to the game. Calls nest.
    void CapturePad(bool capture);
    /// A controller button went down while a window or the menu of the reader's had it.
    void OnPadButton(std::size_t button);
    void OnSettingsChanged();
    void TryCapture(int max_width, std::function<void(const QImage&)> callback, int tries_left);

    QMainWindow* main_window;
    QWidget* render_window;
    Host host;

    QMenu* menu{};
    QAction* action_sentence_card{};
    QAction* action_backlog{};
    QAction* action_explain{};
    QAction* action_word_card{};
    QAction* action_undo_card{};
    QAction* action_reload_profile{};
    QAction* action_pause_stats{};
    QAction* action_status{};
    QAction* action_statistics{};
    QMenu* menu_card_audio{};
    /// What the running game's profile offers to do in the game (reader.action); filled each
    /// time the menu opens, hidden when there is nothing.
    QMenu* menu_game_actions{};
    /// The four choices of the running game's line audio: as in the settings, always, ask,
    /// never.
    QList<QAction*> card_audio_actions;

    QPointer<ReaderOverlay> overlay;
    std::unique_ptr<LookupFlow> lookups;

    QList<ReaderApp::PlacedText> placed;
    /// The word that is marked: a line of the backlog and a range of its text, or none.
    qint64 mark_line{-1};
    int mark_first{-1};
    int mark_last{-1};
    /// Where the open popup's text is, to keep the popup beside it when the window moves.
    QRectF anchor_rows;
    qreal anchor_near_x{};

    Down down;
    bool running{};
    /// The user paused the game through the reader, to read what is on screen.
    bool user_paused{};
    class ReaderPad* pad{};
    class SteamControllerBridge* steam{};
    QAction* action_pause_game{};
    QAction* action_next_choice{};
    QAction* action_progress{};
    bool application_active{true};
    int dialogs_open{};
    int pad_captures{};
    /// The menu holds the controller: once, however often a parent menu shows it again.
    bool menu_captures{};

    QTimer sync_timer;
    QTimer hover_timer;
    QElapsedTimer box_picture_clock;
    /// Looks where the pointer is, where the emulator's window does not say (host.watch_pointer).
    QTimer pointer_watch;
    QPoint pointer_seen;
    /// Touch play: one step of moving the game's cursor to the answer picked, then confirming.
    QTimer pick_timer;
    int pick_answer{-1};
    int pick_presses{};
    int pick_last{};
    int pick_stuck{};
    /// Touch play: the profile of the running game says it is played by pressing its buttons.
    [[nodiscard]] bool PlayingByPresses() const;
    void PressGameButton(int input);
    void GoOn();
    void PickAnswer(int answer);
    void PickStep();
    /// What the pointer rests on, to be looked up once it has rested there for a moment.
    std::optional<Down> resting;
    qint64 hovered_line{-1};
    int hovered_offset{-1};

    /// The cursor steered with a controller's sticks.
    StickCursor* cursor{};
    /// The line the cursor was last brought to the start of: -2 before it was put anywhere,
    /// -1 when it was put in the middle of the picture.
    qint64 cursor_homed_for{-2};
    /**
     * What the cursor rests on is something the player pointed at. Not so for a cursor that
     * was left where it was while a new line came, or that was just switched on again: what
     * happens to be under it is only looked up once the cursor has been moved.
     */
    bool cursor_pointed{true};
    QList<qint64> placed_lines;
};

} // namespace ReaderUi
