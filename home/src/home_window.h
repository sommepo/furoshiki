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
#include <QColor>
#include <QFont>
#include <QImage>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include "line_titles.h"
#include "library.h"
#include "look.h"
#include "play_settings.h"
#include "sounds.h"
#include "steam_pad.h"

namespace Home {
namespace Art {
class Finder;
}

/// A top-level window's first size and place, in the middle of its screen. A test run stays at
/// 1280×720: its pictures and clicks are made for that.
void PlaceFirst(QWidget* window);

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
    /// Whether a row of the program's was set by the user, and that row put back to its own.
    void SetSettingReset(std::function<bool(const QString& row)> is_set, std::function<void(const QString& row)> reset) {
        setting_is_set = std::move(is_set);
        setting_reset = std::move(reset);
    }
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
    /// What the reader kept of a title on this device: its lines, oldest first (the newest two
    /// hundred), how many there are in all, and what was read of it on each of its last days.
    struct TitleReading {
        struct Line {
            QString speaker, text;
            qint64 time{};
        };
        QList<Line> lines;
        qint64 line_count{};
        QList<QPair<QString, qint64>> days; ///< Newest first: the day, its characters
    };
    void SetTitleReading(std::function<TitleReading(const QString& game)> read) {
        title_reading = std::move(read);
    }
    /// The study tracker's address ("http://host:8788"; empty: none), a setting of this device.
    /// Says `text` along the page's foot for a few seconds.
    void Say(const QString& text);
    /// Which other device read each game last, and when (services.h): said on a title's stage
    /// where that is later than this device's own last day.
    void SetElsewhere(QHash<QString, QPair<QString, QString>> read) {
        elsewhere = std::move(read);
        update();
    }
    /// The settings pages, shown now; `back` is where leaving them goes (a game they were
    /// opened over), none: the shelf.
    void OpenPages(std::function<void()> back = {});
    /// Characters to read in a day (0: no goal), kept in QSettings (stats/goal).
    [[nodiscard]] int DailyGoal() const;
    void SetDailyGoal(int characters);
    [[nodiscard]] QString TrackerUrl() const;
    void SetTrackerUrl(const QString& url);
    /// The shelf's theme as the text strip over the desktop is drawn with it (text_strip.h).
    struct StripLook {
        QColor ground, ink, muted, accent, rule;
        QFont text, mono;
    };
    [[nodiscard]] StripLook LookOfStrip();
    /// Adds a title read from lines (line_titles.h; `by` "websocket" or "clipboard"), or changes
    /// how one is read; a name that is exactly a title record's is that title. Its game ID, or
    /// nothing with a word said on the page.
    QString AddLinesTitle(const QString& by, const QString& title);
    /// Adds a window of this computer read by OCR (ocr_source.h): its title, the window's own
    /// title, where the text is in its picture (0-1). Its game ID, or nothing with a word said.
    QString AddWindowTitle(const QString& title, const QString& window, const QRectF& area, const QImage& picture = QImage());
    /// Whether the wizard that adds a title is open (it may be running a hook of its own).
    [[nodiscard]] bool Adding() const {
        return wizard_on;
    }
    /// The OCR helper's settings (line_titles.h, Lines::Helper), as the wizard writes them.
    void SetOcrHelper(const QString& address, const QString& source, const QString& token);
    /// Where a WebSocket's lines come from (ws://host:port); said on the page when it is no such address.
    void SetLinesAddress(const QString& address);
    /// Called when the titles read from lines changed: the program writes their file with what
    /// was read of them. Without it the page writes the file itself.
    void SetLinesChanged(std::function<void()> changed) {
        lines_changed = std::move(changed);
    }
    /// A script's words the page does not know go here (the program's: lines-say, strip-lookup).
    void SetScriptCommands(std::function<bool(const QString& word, const QString& rest)> commands) {
        script_commands = std::move(commands);
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
    void leaveEvent(QEvent* event) override;
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
        bool yours{};  ///< A setting the user set: marked, and Backspace puts it back
        bool steps{};  ///< Left and right change it: the pages draw its two arrows for the mouse
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
    void Hover(const QPointF& at, bool lights = true);
    /// The row or tab under the pointer is no longer lit (a key was pressed, the pointer left).
    void HoverNone();
    /// How often a frame is asked for: as often as the screen shows one, if a frame is drawn in time.
    void Pace(double frame_ms);
    void DrawGround(double scale);
    void DrawCard(QPainter& p, const Item& item, const QList<Row>& rows, int chosen);
    /// The list's rows; with `bar` (a row's place, not whole while it glides) the theme's bar, if it has one, there and
    /// every title drawn, the chosen one's colours where the bar is.
    void DrawList(QPainter& p, const Item& item, const QList<Row>& rows, int chosen, double bar = -1);
    /// The chosen row's title as DrawList leaves it to be drawn over the scene.
    [[nodiscard]] Label ListLabel(const QList<Row>& rows, int chosen) const;
    void DrawTabs(QPainter& p, const QList<Item>& items);
    /// The orbs, behind the title (`front` false) or in front of it, at this moment.
    void DrawOrbs(QPainter& p, bool front, double seconds, const QRectF& box) const;
    /// The theme's glow behind the chosen title (`cursor-glow`), at this moment.
    void DrawGlow(QPainter& p, double seconds, const QRectF& box) const;
    static void PaintGlow(QPainter& p, const QColor& colour, double strength, const QRectF& box);

    // Furoshiki 3's page, the shelf (shelf_page.inc; docs/furoshiki-3.md): every title a spine,
    // the one in hand open. Shown instead of the save menu while `shelf_on`.
    struct ShelfLook;
    struct ShelfTitle {
        Game game;
        QString system;
    };
    void ShelfLoad();
    void SetShelf(bool on, bool keep);
    void SetShelfTheme(const QString& key, bool keep);
    [[nodiscard]] const QList<ShelfTitle>& ShelfTitles();
    [[nodiscard]] QStringList ShelfSections() const;
    void PaintShelf();
    void PaintShelfBody(QPainter& p);
    /// The stage: the title in hand as a page of its own (down from the shelf, up to go back).
    void PaintStage(QPainter& p);
    void SetStage(bool on);
    /// The pages: statistics, the readers, the consoles' settings and the program's, up from the
    /// shelf; the save menu's own rows, set as this page sets things.
    void PaintPages(QPainter& p);
    void SetPages(bool on);
    /// The settings a search for `text` finds: rows of the table, by their names and pages.
    [[nodiscard]] QList<int> FoundSettings(const QString& text) const;
    void GoToSetting(int index);
    /// The Log: the lines read of the title in hand, down from its stage.
    void PaintLog(QPainter& p);
    void SetLog(bool on);
    [[nodiscard]] const TitleReading& ReadingOf(const QString& game);
    /// What the study tracker says of a title (its GET /api/media): read and whole length in
    /// characters, Jiten's figures, time, pace. Asked when the shelf loads, on F5 and every five
    /// minutes while the shelf shows.
    struct TrackerTitle {
        qint64 read{}, total{};
        int words{}, unique{}, speed{}, sessions{}, rating{};
        double difficulty{}, coverage{}, hours{};
        QString last; ///< "2026-11-08", or empty
    };
    void TrackerAsk();
    [[nodiscard]] const TrackerTitle* TrackerOf(const QString& game_id) const;
    /// True when the key was the shelf's.
    bool ShelfKey(int key);
    void ShelfMove(int by);
    void ShelfPlay();
    bool ShelfClick(const QPointF& at);
    /// The pointer over the shelf's pages: a hand over what a click takes, and that thing lit.
    void ShelfHover(const QPointF& at, bool lights);
    [[nodiscard]] bool ShelfHovered(int kind, int index) const { return shelf_hover_kind == kind && shelf_hover_index == index; }
    /// Keys along a page's foot, right-aligned at `right`: each a word the pointer can take
    /// (a key of -1 is only said). Returns where the first begins.
    double PutKeys(QPainter& p, const QFont& font, double right, double baseline, const QList<QPair<QString, int>>& keys);
    /// A title's front cover: its app's picture, else the one built in for its title record.
    QPixmap TitleCover(const Game& game);
    /// The spine a game stands with: its box's own (built in, or found here), else one made in its
    /// console's look; none for a game of a console with no look here.
    QPixmap ShelfSpine(const Game& game);
    /// Whether the title in hand lacks a box front or a real spine (made ones do not count).
    [[nodiscard]] bool ArtMissing(const Game& game);
    /// Looks for the title in hand's box art (art_fetch.h), and shows it once found.
    void FindArt();
    [[nodiscard]] QString ConsoleOfGame(const Game& game) const;
    void SetShelfSection(int section);
    /// A shelf's games as they are listed (0: every console's), unsorted; none past the last.
    [[nodiscard]] QList<Game> SectionGames(int section) const;
    /// The shelf as it stands, drawn into a picture: what leaves as the lift goes to another.
    QImage ShelfSnapshot();
    // The shelf's order (S): keys in priority, each up or down; kept as shelf/sort.
    struct ShelfSortKey {
        int key;
        bool down;
    };
    void SortLoad();
    void SortSave() const;
    void SortPress(int key, bool remove);
    void PaintSortLine(QPainter& p, double baseline);
    // Adding a title (wizard_page.inc): the doors, a window and its text's area, its name.
    struct Wizard;
    void OpenWizard(bool helper_only = false);
    void CloseWizard();
    void WizardGo(int step);
    void WizardStep();
    void WizardAccept();
    void WizardBack();
    void WizardArea(const QRectF& area);
    /// The chosen window is read by OCR: the helper's settings first where it has none.
    void WizardOcr();
    /// A script's: the game window whose title has `words` is the one in hand.
    void WizardChooseWindow(const QString& words);
    /// The chosen window's program: how it is read, an Agent script for it first (wizard_hooks.inc).
    void HookMatch();
    void HookStart();
    bool WizardKey(int key);
    /// A key that types (or Backspace) while a line of the wizard is typed on; true when taken.
    bool WizardText(int key, const QString& text);
    bool WizardPress(const QPointF& at);
    void WizardDrag(const QPointF& at, bool done);
    void PaintWizard(QPainter& p);
    // The wizard's door C, a text hook (wizard_hooks.inc): how it is hooked, an Agent script,
    // the program, its threads.
public:
    struct HookSteps;
    /// Adds a title read by a hook Furoshiki starts (line_titles.h; `hook` has its engine,
    /// script, program and threads). Its ID, or nothing when it could not be added.
    QString AddHookTitle(const QString& title, const Lines::Title& hook);
private:
    [[nodiscard]] bool HookStepOn() const;
    void HookOpen();
    void HookClose();
    void HookGo(int step);
    void HookStep();
    void HookAccept();
    void HookBack();
    bool HookKey(int key);
    bool HookText(int key, const QString& text);
    void HookFetch(const QString& kit);
    QString HookTitle(const QString& name);
    [[nodiscard]] QString HookReadBy() const;
    /// The program a hook of Furoshiki's is put in; empty for a texthooker the user runs.
    [[nodiscard]] QString HookProgram() const;
    [[nodiscard]] int HookStepCount() const;
    void PaintHook(QPainter& p);
    void ShelfStep();
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
    bool list_waits{};       ///< The scene's list is drawn again once the cursor stops gliding
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
    bool shelf_on{};       ///< The shelf is the page (look/shape: "shelf")
    std::shared_ptr<ShelfLook> shelf_look;
    QStringList shelf_themes; ///< Their keys, in order
    QList<ShelfTitle> shelf_titles;
    bool shelf_stale{true};
    int shelf_section{};   ///< 0: every title; then a console each
    int shelf_choice{};
    double shelf_at{};     ///< Where the open spine is on its way to the chosen one
    bool shelf_stage{};    ///< The stage is shown, not the shelf
    bool shelf_pages{};    ///< The pages are shown
    QHash<QString, QPair<QString, QString>> elsewhere; ///< A game's ID: the device, the day
    QSet<QString> more_open; ///< The settings pages whose More is opened
    QString find_text;       ///< What is typed to find a setting; null while nothing is looked for
    int find_choice{};       ///< Which of the found is in hand
    // The pages under the mouse (shelf_page.inc): the list scrolls by the wheel apart from the
    // row in hand, what the pointer is over comes up and goes softly, the mark glides.
    struct Eased {
        double at{};
        bool to{};
        int seen{};
    };
    QHash<quint32, Eased> eased; ///< What comes and goes softly, by what it is (EaseKey)
    int ease_frame{};
    bool shelf_busy{};           ///< Something of the pages or the wizard is still moving
    int pages_first{};           ///< The first row shown
    double pages_first_at{};     ///< Where the list stands on its way there
    double pages_bar_at{};       ///< Where the mark of the row in hand stands, in rows
    double page_t{1};            ///< A page just come to: 0 to 1
    QString pages_seen;          ///< The page drawn last
    int pages_seen_row{-1};      ///< The row in hand when it was
    int pages_drag{};            ///< What the held button moves: 1 the slider, 2 the list's bar
    QRectF pages_drag_area;
    int wizard_seen_step{-1};
    double wizard_t{1};          ///< A step of the wizard just come to: 0 to 1
    double wizard_mark_at{};     ///< Where the wizard's mark stands, in rows
    /// The labels the user gave (the Developer page): "Page/Name" to what it is called.
    QHash<QString, QString> labels;
    bool labels_read{};
    bool renaming{};             ///< Enter or a click on a row gives it another label
    /// 0 to 1: `on` come to softly. `kind` and `index` say what it is.
    double Ease(int kind, int index, bool on);
    /// `at` a frame nearer `to` (most of the way in `time` seconds); the page is drawn again while it moves.
    void GlideTo(double& at, double to, double time);
    double shelf_dt{};
    bool shelf_still{};
    [[nodiscard]] QString LabelsFile() const;
    void LabelsLoad();
    void LabelsSave() const;
    /// What a row of a page (or a page: `page` "Page") is called: the user's label, else the one
    /// it has by itself.
    [[nodiscard]] QString Labelled(const QString& page, const QString& name);
    /// Asks for another label for it.
    void Rename(const QString& page, const QString& name);
    /// The wide picture of the consoles that have one (ps2.wide, switch.wide), as one row.
    [[nodiscard]] QString WideValue() const;
    void WideStep(int steps);
    void PagesDrag(const QPointF& at);
    std::function<void()> pages_back; ///< Where leaving the pages goes, when they stand over a game
    bool shelf_log{};      ///< The Log is shown
    double log_at{};       ///< 0 the stage, 1 the Log
    int log_line{-1};      ///< The line in hand, among the title's lines (-1: the newest)
    std::function<TitleReading(const QString& game)> title_reading;
    QHash<QString, TitleReading> title_readings;
    bool shelf_passing{};  ///< A key of the pages is being handed to the rows' own handling
    double pages_at{};     ///< 0 the shelf, 1 the pages
    std::shared_ptr<Wizard> wizard; ///< Adding a title: what was chosen so far
    bool wizard_on{};
    QImage own_picture;    ///< This window as it was when the wizard opened (the stand-in window list)
    double wizard_at{};    ///< 0 what is under it, 1 the wizard
    QString shelf_want;    ///< A title just added: chosen on the shelf once it is listed
    QObject* tracker_net{}; ///< The network's way out (a QNetworkAccessManager), made when first asked
    QHash<qint64, TrackerTitle> tracker_by_jiten;
    QHash<QString, TrackerTitle> tracker_by_title;
    qint64 tracker_asked{}; ///< When (the page's clock, ms); 0: never
    bool tracker_waiting{};
    double stage_at{};     ///< 0 the shelf, 1 the stage, between them on the way
    QElapsedTimer shelf_clock;
    QList<Hit> shelf_hits;
    int shelf_hover_kind{-1}, shelf_hover_index{-1}; ///< What the pointer is over (a hit's kind and index)
    QHash<int, double> spine_lift; ///< How far each spine is drawn out of the shelf under the pointer (0 to 1)
    QList<ShelfSortKey> shelf_sort;
    Art::Finder* art_finder{}; ///< Made when first asked
    QString art_said;          ///< What it said last, of the game it looked for (art_said_of)
    QString art_said_of;
    bool shelf_sorting{};  ///< The sort line is open at the foot
    int sort_choice{};     ///< The key in hand on it
    double shelf_shift{};  ///< The lift: how far the spines stand from their place (sheet px)
    double shelf_fade{1};  ///< and how much of them shows
    double lift_t{1};      ///< 0 the lift just left a shelf, 1 arrived
    int lift_dir{};        ///< 1 going down, -1 going up
    QImage lift_old;       ///< The shelf it left, as it stood
    double floor_at{};     ///< Where the floor mark is on its way
    QHash<int, QString> shelf_kept; ///< The title in hand on each shelf (its file), kept while away
    QHash<QString, QColor> shelf_tints; ///< A cover's own colour, for its spine's band
    QString shelf_minute;
    int dragging{-1};      ///< A slider held with the pointer
    int hover_row{-1};     ///< The row under the pointer (not the chosen one), lit a little
    int hover_tab{-1};     ///< The tab along the foot under the pointer
    double frame_cost{6};  ///< What a frame takes to draw, ms, evened out
    int frame_skip{1};     ///< A frame every so many of the screen's own
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
    std::function<void()> lines_changed;
    std::function<bool(const QString&, const QString&)> script_commands;
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
    std::function<bool(const QString&)> setting_is_set;
    std::function<void(const QString&)> setting_reset;
    std::function<QString(const QString&, const QString&)> popup_export;
    std::function<QString(const QString&, const QString&)> popup_import;
};

} // namespace Home
