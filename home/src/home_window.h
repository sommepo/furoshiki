// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Furoshiki's one window: the games every reader app can read, a console at a time; the
// reading statistics of all of them; the way into each app's own menu; the settings they
// share. Drawn by hand on a sheet of 1280x720 that is scaled to the window, in the "Save
// Menu" layout with a theme of the user's choice (look.h, themes/; AGENTS.md says how it is
// drawn), worked with the keys, the pointer or a controller.

#include <functional>
#include <memory>
#include <QImage>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include "library.h"
#include "look.h"
#include "play_settings.h"
#include "sounds.h"
#include "steam_pad.h"

namespace Home {

class HomeWindow : public QWidget {
    Q_OBJECT

public:
    explicit HomeWindow(QWidget* parent = nullptr);
    ~HomeWindow() override;

    /// Commands for a test, one a line: "key Right", "click 640 360", "wait 300",
    /// "grab FILE" (the page at rest), "shot FILE" (as it is, moving or not), "reload",
    /// "theme KEY", "fuzz off|light|heavy", "motion on|off", "quit".
    /// They run one after the other once the window is up; what they set is not kept.
    void RunScript(const QStringList& lines);
    /// Starts nothing: what would be started is written to standard output (tests).
    void SetDryRun(bool dry) {
        dry_run = dry;
    }
    /// Keeps the window out of full screen whatever the setting says (--window).
    void SetWindowed(bool windowed) {
        window_forced = windowed;
    }
    /// Shows the window as the setting (and --window) say.
    void ShowAsSet();
    /// Who plays a game in this program (the cores: main_window.h): asked with the game's app
    /// and file before that app is started; true when it took the game.
    void SetCorePlayer(std::function<bool(const QString& app, const QString& path)> player) {
        core_player = std::move(player);
    }
    /// The consoles this program plays itself (its cores) and the settings for playing them:
    /// with any, a CONSOLES page shows those settings, for all consoles or one.
    void SetConsoles(QList<PlayConsole> list, PlaySettings* store) {
        consoles = std::move(list);
        play = store;
        Changed();
    }
    /// A console's games the program plays, for the CONSOLES page's settings of one game.
    struct ConsoleGame {
        QString id, title;
    };
    void SetConsoleGames(const QString& console, QList<ConsoleGame> games) {
        console_games[console] = std::move(games);
        Changed();
    }
    /// What a console needs from the user (its BIOS, keys) and where it goes: the program's
    /// own knowledge of each console (main_window.cpp); `folders_changed` when the folders to
    /// look for a console's games in were changed on the page.
    struct ConsoleSetup {
        QString system_folder; ///< Where the console's BIOS, firmware or keys go
        QString status;        ///< What is there, or what is missing, in a few words
        bool ready{true};      ///< Nothing missing that its games need
    };
    void SetConsoleSetup(std::function<ConsoleSetup(const QString& console)> setup,
                         std::function<void(const QString& console)> folders_changed) {
        console_setup = std::move(setup);
        console_folders_changed = std::move(folders_changed);
    }
    /// The window's look, for the settings window: its themes' names, the chosen one, the fuzz,
    /// the moving cursor; a change is kept and shown at once.
    [[nodiscard]] QStringList ThemeNames() const;
    [[nodiscard]] int ThemeIndex() const {
        return theme_index;
    }
    void ChooseTheme(int index) {
        SetTheme(index, true);
    }
    [[nodiscard]] QString ChosenFuzz() const {
        return fuzz_choice;
    }
    void ChooseFuzz(const QString& level) {
        SetFuzz(level, true);
    }
    [[nodiscard]] bool ChosenMotion() const {
        return motion;
    }
    void ChooseMotion(bool on) {
        SetMotion(on, true);
    }
    /// Opens the settings window (F2, and the Settings page's first row), at a tab by its name
    /// ("Save sync", "Achievements", "Text server"), or as it was (empty).
    void SetSettingsWindow(std::function<void(const QString& tab)> open) {
        open_settings = std::move(open);
    }
    /// What the Settings page's rows of the settings window's pages show (RetroAchievements,
    /// Text server, Pair a device), by the row's name.
    void SetSettingValue(std::function<QString(const QString& row)> value) {
        setting_value = std::move(value);
    }
    /// A row of the Settings page kept outside it, moved a step by the arrows (Pointer button).
    void SetSettingStep(std::function<void(const QString& row, int step)> step) {
        setting_step = std::move(step);
    }
    /// Shows the settings anew after they were changed elsewhere.
    void SettingsChanged() {
        Changed();
    }
    /// The popup style's export and import on the CONSOLES page, for the layer chosen (console
    /// and game empty: all consoles); each says what came of it.
    void SetPopupStyle(std::function<QString(const QString&, const QString&)> export_style,
                       std::function<QString(const QString&, const QString&)> import_style) {
        popup_export = std::move(export_style);
        popup_import = std::move(import_style);
    }
    /// Save sync in the program (built with its cores): what the Settings page's row shows,
    /// and its window.
    void SetSaveSync(std::function<QString()> state, std::function<void()> open) {
        save_sync_state = std::move(state);
        save_sync_open = std::move(open);
        Changed();
    }
    /// Reads the apps' files again (the program has written its own), keeping the choice.
    void Refresh() {
        ReloadKeepingChoice();
        Changed();
    }
    /// A game plays in the program's window over this page: the page takes no keys, and the
    /// Steam Controller is the game's (Steam(), its ButtonChanged and AxisChanged).
    void SetPlaying(bool on) {
        playing_game = on;
        pad.SetGameMode(on);
    }
    [[nodiscard]] SteamPad& Steam() {
        return pad;
    }
    /// Says something at the foot of the page for a few seconds.
    void Notify(const QString& text) {
        Say(text);
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// What stands along the horizontal line: a console that has games, or one of three pages.
    struct Item {
        enum Kind { System, Statistics, Readers, Consoles, Settings } kind;
        QString label;
        QString system;
    };
    /// What stands down the vertical line.
    struct Row {
        QString title;
        QString meta;  ///< Under the title while the row is the chosen one
        QString value; ///< At the right (not for a console's games)
        QString cover;
        int setting;   ///< Which row of the settings it is, or -1
        bool dim{};    ///< The value is not this layer's own (CONSOLES: "same as all")
    };
    struct Hit {
        QRectF area; ///< On the sheet
        int kind;    ///< 0 a tab along the foot, 1 a row, 2 a slider's line
        int index;
    };
    /// The parts of the scene drawn again on their own when only they change.
    enum Part { PartCard = 1, PartList = 2, PartTabs = 4, PartAll = 7 };
    /// What the page showed when it was drawn last: what changed since says how it moves on.
    struct Shown {
        QString page;
        int row{-1};
        QString theme, fuzz, notice;
        int layer{};
        bool running{};
    };
    /// A piece of the frame drawn over the scene (the clock, the cursor, the gliding bar):
    /// the screen's pixels of `area` on the sheet, fuzzed as the scene is.
    struct Piece {
        QImage image;
        QRect area; ///< Where it goes, in the scene's pixels
    };
    /// What the card above the list shows of the chosen row.
    struct Card;
    /// The theme's values, read once when it is chosen (home_window.cpp).
    struct Look;
    /// The chosen row's title, drawn over the scene with the cursor's orbs round it.
    struct Label {
        bool shown{};
        QRectF box; ///< The title's line on the sheet
        QString text;
        QFont font;
        QColor colour;
        QList<Shadow> shadows;
        double baseline{};
    };

    void Reload();
    /// Reads the apps' files again and keeps the choice on what it was on (the same console,
    /// page and game), wherever the new order has put them.
    void ReloadKeepingChoice();
    void Clamp();
    void Step();
    void Key(int key);
    void Accept();
    void Launch(const App& app, const QStringList& arguments, const QString& what);
    void Finished();
    /// The started app's window, once it is up, is given its place: most of the screen's
    /// height, a little wider than a television picture, in the middle (Windows).
    void PlaceChild();
    void Say(const QString& text);
    void NextScriptLine();
    [[nodiscard]] QList<Item> Items() const;
    [[nodiscard]] QList<Row> Rows(const Item& item);
    [[nodiscard]] int& RowChoice(const Item& item);
    [[nodiscard]] QList<Game> GamesOf(const QString& system) const;
    [[nodiscard]] QStringList Systems() const;
    [[nodiscard]] const App* AppOf(const QString& app) const;
    /// The reader apps that are programs of their own (the READERS page): not Furoshiki's own
    /// file of each console it plays (apps/reader-home-<key>.json), whose games are listed on
    /// the consoles' pages.
    [[nodiscard]] QList<App> ReaderApps() const;
    [[nodiscard]] QPixmap Cover(const QString& path);
    /// A slider of the settings moved by so many steps (0: only read), and its value.
    double Slide(int row, int steps);
    void SetSlide(int row, double value);
    [[nodiscard]] QPointF ToSheet(const QPointF& point) const;

    // The look (themes/*.json): which theme, how much fuzz, whether the cursor moves.
    void SetTheme(int index, bool keep);
    void SetFuzz(const QString& level, bool keep);
    void SetMotion(bool on, bool keep);
    [[nodiscard]] QString FuzzLevel() const;
    /// Something shown changed: the scene (or only those parts of it) is drawn again at the
    /// next paint.
    void Changed();
    void Changed(int parts);
    /// The scene: everything but the chosen title, the orbs and the clock, at the screen's pixels.
    void DrawScene(double scale);
    /// Only some parts of the scene drawn again on its ground, and fuzzed again there.
    void DrawParts(int parts);
    [[nodiscard]] static QRectF PartArea(int part);
    /// The pieces over the scene for this moment.
    [[nodiscard]] Piece MakePiece(const QImage& base, const QRectF& area, const std::function<void(QPainter&)>& draw) const;
    /// The clock: where it stands on the sheet for a time, and the time drawn there.
    [[nodiscard]] QRectF ClockArea(const QString& time) const;
    void DrawClock(QPainter& p, const QString& time) const;
    /// The frame as it is shown: the scene, its pieces, a fading picture of what was before.
    void Compose(QPainter& p) const;
    [[nodiscard]] Shown Now() const;
    /// The bar bumps at the first or last row, and goes back.
    void Bump(int direction);
    /// The cards and lists next to the shown one, drawn once unseen, so that their covers and
    /// type are ready when they are chosen.
    void Prewarm(int generation, int step);
    void Hover(const QPointF& at);
    void DrawGround(double scale);
    void DrawCard(QPainter& p, const Item& item, const QList<Row>& rows, int chosen);
    /// The list's rows; with `bar` (a row's place, not whole while it glides) the theme's bar, if it has one, there and
    /// every title drawn, the chosen one's colours where the bar is.
    void DrawList(QPainter& p, const Item& item, const QList<Row>& rows, int chosen, double bar = -1);
    void DrawTabs(QPainter& p, const QList<Item>& items);
    /// The orbs, behind the title (`front` false) or in front of it, at this moment.
    void DrawOrbs(QPainter& p, bool front, double seconds, const QRectF& box) const;
    /// The theme's glow behind the chosen title (`cursor-glow`), at this moment.
    void DrawGlow(QPainter& p, double seconds, const QRectF& box) const;
    [[nodiscard]] Card CardOf(const Item& item, const QList<Row>& rows, int chosen);

    QList<Theme> themes;
    int theme_index{};
    std::unique_ptr<Look> look;
    QString fuzz_choice;   ///< "off", "light", "heavy", or empty: the theme's own
    bool motion{true};     ///< The orbs turn, drift and spring to a new row (off: they stand still)
    QImage ground;         ///< The background and the panels' boxes, for this theme and size
    QString ground_key;
    QImage scene;          ///< The ground with everything that changes on it but the cursor
    QImage scene_fuzzed;   ///< The scene with the fuzz on it
    bool scene_dirty{true};
    int dirty_parts{};     ///< Parts to draw again without the rest
    Shown shown;
    Piece clock_piece, cursor_piece;
    QString clock_text;    ///< What the clock's piece shows
    bool pieces_stale{true}; ///< The scene under the pieces changed
    // The bar glides to the chosen row (a row's place, its title's width for the orbs).
    double bar_at{}, bar_to{}, width_at{}, width_to{};
    bool gliding{};
    QElapsedTimer frame_clock;
    // What was shown fades out over what is shown now.
    QImage fade_from;
    QRect fade_area;
    double fade_length{};
    QElapsedTimer fade_clock;
    int prewarm_generation{};
    int dragging{-1};      ///< A slider held with the pointer
    double wheel_side{};   ///< A sideways swipe not yet a whole step
    double scene_scale{};
    Label label;
    QString chosen_key;    ///< Which row of which page the orbs are on (they spring to another)
    QElapsedTimer orb_clock; ///< Since the row changed
    double orb_from{1};    ///< How far out the orbs were then (1: at the title's sides)
    QElapsedTimer spin_clock; ///< The rings' turning, never started again

    Library library;
    QJsonObject shared;   ///< The settings the reader apps take from here
    Sounds sounds;
    SteamPad pad;         ///< The Steam Controller, played here as the reader apps play it
    int axis_choice{};
    QHash<QString, int> row_choice; ///< By an item's label
    qint64 last_second{-1};
    QList<Hit> hits;
    QHash<QString, QPixmap> covers;
    QTimer ticker;
    QElapsedTimer clock;
    QProcess child;
    QString running;      ///< What was started and has not ended
    QString notice;
    qint64 notice_until{};
    bool sound_on{true};
    bool full_screen{};
    bool games_windowed{}; ///< Games start in a window of a size of ours, not full screen
    int place_tries{};     ///< Looks still to be taken for the started app's window
    bool dry_run{};
    bool playing_game{}; ///< A game plays over this page (SetPlaying)
    bool window_forced{};  ///< --window: never full screen
    int wheel_rest{};      ///< A wheel's turn not yet a whole step (1/8 degree)
    /// The CONSOLES page: the settings shown for the layer chosen (all consoles, or one).
    [[nodiscard]] QList<PlaySetting> LayerSettings() const;
    [[nodiscard]] QString LayerConsole() const;
    /// A CONSOLES row's setting moved by so many steps; 0 with `clear` takes it back.
    void ChangeConsoleSetting(int row, int steps, bool clear = false);
    /// The CONSOLES page's rows before its settings: "Settings for", and with one console
    /// chosen its game, its folders and its system files.
    [[nodiscard]] int ConsoleHeadRows() const {
        return PopupHeadRow() + 2;
    }
    /// The CONSOLES page's two rows for the popup style: export, then import.
    [[nodiscard]] int PopupHeadRow() const {
        return console_layer > 0 ? 4 : 1;
    }
    /// The game whose own settings the page shows (empty: the console's).
    [[nodiscard]] QString LayerGame() const;
    /// Enter (or Backspace, `remove`) on one of the head rows.
    void ConsoleHeadAction(int row, bool remove);

    QStringList script;
    std::function<bool(const QString&, const QString&)> core_player;
    QList<PlayConsole> consoles;
    PlaySettings* play{};
    int console_layer{}; ///< 0 all consoles, else the console's place in `consoles` plus one
    int console_game{};  ///< 0 every game of that console, else the game's place plus one
    QHash<QString, QList<ConsoleGame>> console_games;
    std::function<ConsoleSetup(const QString&)> console_setup;
    std::function<void(const QString&)> console_folders_changed;
    std::function<QString()> save_sync_state;
    std::function<void()> save_sync_open;
    std::function<void(const QString&)> open_settings;
    std::function<QString(const QString&)> setting_value;
    std::function<void(const QString&, int)> setting_step;
    std::function<QString(const QString&, const QString&)> popup_export;
    std::function<QString(const QString&, const QString&)> popup_import;
};

} // namespace Home
