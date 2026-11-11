// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The one program's one window (docs/one-program.md): Furoshiki's own page, and the game,
// which a core draws into a widget of this window (the screen) while the reader's windows lie
// over it. Built with HOME_CORES; without it Furoshiki's page is the window by itself.

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <mutex>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>
#include "updater.h"
#include "core_library.h"
#include "core_session.h"
#include "addons.h"
#include "line_titles.h"
#include "play_settings.h"

class QAction;
class QAudioSink;
class QMenu;
class QStackedWidget;
namespace ReaderApp {
struct PresentedLine;
}

namespace Home {

class HomeWindow;

/// The widget a core draws the game into: a native window of its own, painted by nobody but
/// the core. Its keys are the console's buttons; its pointer goes to the reader first.
class GameScreen : public QWidget {
    Q_OBJECT

public:
    explicit GameScreen(QWidget* parent);
    QPaintEngine* paintEngine() const override {
        return nullptr;
    }
    /// The console's buttons the keyboard holds now (by the core's bits).
    [[nodiscard]] uint64_t KeyButtons() const {
        return key_buttons.load();
    }
    /// Which key is which of the console's buttons (by the core's description).
    void SetButtons(const reader_core_info& info);
    /// Where the pointer goes when the reader does not keep it: the game's (a 3DS's lower
    /// screen, a light gun), with READER_CORE_POINTER_* flags; true when the game took it.
    void SetPointerSink(std::function<bool(const QPointF& at, uint32_t flags)> sink) {
        pointer_sink = std::move(sink);
    }

signals:
    void Resized();
    void MenuWanted();
    void ReaderMenuWanted();
    void FullScreenWanted();
    /// F5 and F8: the savestate slot chosen last written, or loaded.
    void QuickStateWanted(bool load);

protected:
    void paintEvent(QPaintEvent*) override {}
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::map<int, int> key_bits; ///< Qt's key to the console's button bit
    std::atomic<uint64_t> key_buttons{0};
    std::function<bool(const QPointF&, uint32_t)> pointer_sink;
    uint32_t game_pointer{}; ///< The flags of a press the game took, until it is let go
};

class SettingsWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    [[nodiscard]] HomeWindow& Home() const {
        return *home;
    }
    /// The cores found beside the program (or in READER_HOME_CORES), by their file's name.
    [[nodiscard]] const QStringList& CoreFiles() const {
        return core_files;
    }
    /// Plays a game here: `app` is the reader app whose game it is ("station-reader"), or
    /// empty to choose the core by the file's type. False, with `error`, when no core plays it.
    bool Play(const QString& path, const QString& app, QString& error);
#ifndef READER_NO_SAVE_SYNC
    /// A pairing link (docs/pairing.md): save sync set up from it, the settings window's Save
    /// sync tab shown. False, with `error`, when it is not one.
    bool UsePairing(const QString& link, QString& error);
#endif
    /// Reads a title from lines (line_source.h) or from a window by OCR (ocr_source.h): `path`
    /// as the lines' file names it ("lines://websocket/<ID>", "lines://ocr/<ID>",
    /// line_titles.h). Its lines stand in the text strip over the desktop. False, with `error`,
    /// when it cannot be read.
    bool ReadLines(const QString& path, QString& error);
    /// Ends the reading from lines: the strip goes, what was read goes into the page's sums.
    void EndLines();
    /// Whether a core here plays the games of this reader app.
    [[nodiscard]] bool Plays(const QString& app);
    /// Ends the game and shows Furoshiki's page again.
    void EndGame();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Loaded {
        Core core;
        std::unique_ptr<CoreSession> session;
    };
    /// The core for this app (or, with none, for this file's type), loaded once.
    Loaded* CoreFor(const QString& app, const QString& path, QString& error);
    /// Every core here loaded (once), for what it says of its console and its settings, and
    /// started (StartSession).
    void LoadAllCores();
    bool StartSession(Loaded* entry, QString& error);
    /// The settings that apply to the running core's console (and game), handed to it.
    void ApplyPlaySettings(const QString& game);
    /// The add-ons of the game starting (addons.h): their scripts into the reader's profiles,
    /// their settings into the game's layer (once), their companion started. True when a
    /// script changed.
    bool StartAddons(const QString& console_key, const QString& game);
    /// The add-on of the game playing that has a companion's page, or none.
    [[nodiscard]] const Addon* PlayingCompanion() const;
    /// The reader's own CRT values (reader.ini), under every layer of them.
    [[nodiscard]] QString CrtBase() const;
    /// The popup's settings (popup_style.h) on every console's list, its CSS files as choices.
    void AddPopupSettings();
    /// The settings window (settings_window.h), made when it is first opened.
    void OpenSettings(const QString& tab = {});
    /// The settings: the shelf's pages, over the game when one plays (it stands still meanwhile).
    void ShowSettingsPages();
    /// Asks the paired save sync service for the user's other services (services.h); `say`: and
    /// says on the page what came of it.
    void AskHomeServer(bool say);
    /// Asks the Files page for another build of the program (updater.h).
    void CheckUpdate(bool say);
    Home::Updater::Offer update_offer;
    QString update_said{QStringLiteral("ENTER LOOKS")};
    QPointer<SettingsWindow> settings_window;
#ifndef READER_NO_SAVE_SYNC
    QStringList sync_folders; ///< Save sync's notes folder of each console started
#endif
    QString popup_folder;

    /// A game of a console the program plays, as its own file of that console names it.
    struct OwnGame {
        QString id, title, path;
    };
    /// Looks (in the background) for each console's games: in the folders of the games the
    /// apps' files name, the folders the settings name, and where games were played; each file
    /// of the core's types that it says is a game with a text profile. Then WriteOwnFile.
    void FindGames();
    /// Furoshiki's file for a console the program plays (apps/reader-home-<key>.json, the
    /// format every reader app writes): its games, and with `stats` what was read of them.
    void WriteOwnFile(const QString& key, bool stats);
    /// A game played here is kept in program/games.json, so that it stays listed.
    void RememberGame(const QString& key, const QString& id, const QString& path);
    void ShowGame(bool shown);
    void SendWindow();
    void PollPads();
    void OpenMenu();
    /// The pointer at `at` (the screen widget's units) to the game's screen under it, where
    /// the core takes one there.
    bool PointerToGame(const QPointF& at, uint32_t flags);
    /// A controller's motors (the core's rumble), on the main thread.
    void Rumble(uint32_t port, uint16_t strong, uint16_t weak);
    /// The program's menu's parts that change with the game: savestate slots with their
    /// pictures, discs, controllers, patches.
    void FillStateMenu(QMenu* into, bool load);
    void FillDiscMenu(QMenu* into);
    void FillPortMenu(QMenu* into);
    void FillPatchMenu(QMenu* into);
    void SaveStateTo(int slot);
    void LoadStateFrom(int slot);
    /// Where a savestate's picture is kept: states/pictures/<game>/<slot>.png of its console.
    [[nodiscard]] QString StatePicture(int slot) const;
    /// The running game's save folder, opened in the system's file manager.
    void OpenSaveFolder();
    /// The cheats of the running game (READER_CORE_SETTING_CODES), as the user writes them.
    void EditCodes();
    void ToggleFullScreen();
#ifndef READER_NO_SAVE_SYNC
    /// Save sync for the console of a core just started (save_sync/host.h), where the core
    /// tells its save moments and save sync knows the console's saves.
    void StartSaveSync(Loaded* entry, const QString& saves, const QString& app_sync, const QString& program);
    /// The Save sync window (the reader apps' own), over the game or the page.
    void OpenSaveSync();
#endif
    /// The reading statistics to the service, as the apps send theirs (with save sync on;
    /// nothing in a build without it).
    void SendStats();
    /// The statistics changed under the page (reading time tidied): its files made again and sent.
    void StatsChanged();
    /// The development channel's commands for what the menu does (reader_host.cpp asks).
    bool ProgramCommand(const QString& name, const QStringList& parts, const std::function<void(const QString&)>& reply);

    /// The reader's windows and session, made once: before the first game or title read.
    void InstallReader();
    /// A page script's commands for the lines and the strip (HomeWindow::SetScriptCommands).
    bool LinesCommand(const QString& word, const QString& rest);
    /// The lines' file of the page (apps/reader-home-lines.json) with what was read of them.
    void WriteLinesFile();
    class LineSource* lines{};
    /// A title read by a hook Furoshiki starts itself (hook_source.h), and the programs seen
    /// running that such titles are hooked in.
    class HookSource* hook{};
    QSet<QString> hooks_seen;
    bool ReadHook(const Lines::Title& title, QString& error);
    void WatchHooks();
    class TextStrip* strip{};
    /// A window read by OCR (ocr_source.h), and the reader's layer over that window.
    class OcrSource* ocr{};
    class OcrOverlay* overlay{};
    // A hooked line placed over the game's own text by OCR (GameSentenceMiner's and Kimochi's way):
    // the game window's picture read by the helper as each line comes, the two lined up.
    class OcrClient* placer{};
    int place_generation{};
    QRectF place_area{0, 0, 1, 1}; ///< Where in the window the text was last: read there first
    void PlaceHooked(const ReaderApp::PresentedLine& presented);
    void HookedPlaced(const ReaderApp::PresentedLine& presented, bool ok, const QString& seen, const QList<QRectF>& layout);
    /// The strip, made once, for whichever source's lines have no place on a screen.
    void MakeStrip();
    /// A title read by a hook or OCR has begun: its cards take the game window's picture and the
    /// computer's sound (loopback_audio.h), as a core's take the game's.
    void CardsFromWindow();
    /// The title of the game's window being read, where it is known.
    [[nodiscard]] QString GameWindow() const;
    /// The layer over the read window: where the window is now, whether its line stands there.
    void PlaceOverlay();

    HomeWindow* home{};
    GameScreen* screen{};
    QStackedWidget* pages{};
    QStringList core_files;
    std::map<QString, std::unique_ptr<Loaded>> loaded; ///< By file
    Loaded* current{};
    QTimer pads_timer;
    QTimer stats_timer; ///< SendStats every few minutes while a game is read
    std::mutex pads_mutex;
    static constexpr int MaxPorts = 8;
    reader_core_pad gamepads[MaxPorts]{}; ///< The controllers' state by port, read on the main thread
    /// SDL's controllers, in the order they came: the first is port 1's (with the keyboard and
    /// the Steam Controller), the next port 2's, and so on.
    std::vector<struct _SDL_GameController*> sdl_pads;
    struct _SDL_GameController* port_pads[MaxPorts]{}; ///< Which of them is on each port (rumble)
    /// The Steam Controller while a game plays (Windows, without Steam: SteamPad in game mode),
    /// by SDL's button numbers and axes; read with SDL's controller in PollPads.
    uint32_t steam_buttons{};
    int steam_axes[6]{};
    bool steam_seen{};
    bool reader_installed{};
    PlaySettings play_settings;     ///< program/settings.json in Furoshiki's folder
    QList<Addon> addons;            ///< The game add-ons found when the program started
    Companions* companions{};       ///< Their companions that run
    std::map<QString, QList<OwnGame>> own_games; ///< By console: found and played
    QString playing;                ///< The file of the game being played
    QString playing_id;             ///< Its ID, once known (identify, or the core's game_loaded)
    QString console_folder;         ///< program/consoles/<key> of the running core
    int state_slot{1};
    int pointer_screen{-1};         ///< The game's screen the pointer was last on              ///< The savestate slot chosen last (F5 and F8 use it)
    QList<PlayConsole> consoles;    ///< What each core here says of its console
    std::map<QString, QString> system_folders; ///< By console: the system folder its core was given
    /// What a console needs of the user's (its BIOS, keys) and whether it is there.
    [[nodiscard]] HomeWindow::ConsoleSetup SetupOf(const QString& key) const;
    /// The page's list of each console's games (for a game's own settings).
    void ShowConsoleGames(const QString& key);
    /// The sound of a core that has no output of its own (main_window.cpp, SoundOut).
    void PlaySound(const int16_t* samples, size_t frames, uint32_t rate);
    class SoundOut* sound{};
    /// The sound output's own thread: the sound card is fed there, never held up by the window
    /// (drawing, the reader's layer, the popup page), which was the clicks in a core's sound.
    QThread sound_thread;
    QAudioSink* sink{}; ///< On sound_thread
    std::atomic<uint32_t> sink_rate{};
    QMenu* menu{};             ///< The program's own menu over the game (Escape)
    QAction* reader_entry{};   ///< Where the Reader menu goes in it
};

} // namespace Home
