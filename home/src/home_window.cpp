// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QUrl>
#include "hook_fetch.h"
#include "hook_processes.h"
#include "art_fetch.h"
#include "hook_runner.h"
#include "hook_scripts.h"
#include "line_feed.h"
#include "line_titles.h"
#include "ocr_client.h"
#include "ocr_helper.h"
#include "window_list.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QRawFont>
#include <QRegularExpression>
#include <QTime>
#ifdef HOME_TRACKER
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSettings>
#include <QStyle>
#include <QWheelEvent>
#include "app_icon.h"
#include "home_window.h"
#include "services.h"
#include "look_paint.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Home {

namespace {

// The look ("Save Menu", after the save screens of the visual novels of around 2000): on a
// sheet of 1280x720, a tab with the page's name at the top left and a clock at the top
// right; a card with the chosen row's details and its picture in a frame; under it a panel
// of numbered rows, eight at a time, the chosen one on a bar with two orbs round its title;
// along the foot the consoles and the pages, L and R going along. Where each thing stands
// is the Design boards' CSS, in px; its colours, type and shadows are the theme's
// (themes/*.json, look.h). AGENTS.md, "The look and its themes", says how it is drawn.
constexpr double SheetW = 1280, SheetH = 720;
constexpr double PanelLeft = 62, PanelRight = 1218;     // the panels' sides (left/right 62px)
constexpr double TabLeft = 110, TabTop = -4, TabWidth = 470;
constexpr double CardTop = 68, CardHeight = 290, CardPadY = 16;
constexpr double ListTop = 376, ListHeight = 288, ListPadY = 9, RowHeight = 33;
constexpr int RowsShown = 8;
constexpr double NumberColumn = 62, WhenColumn = 228, TitlePad = 18, WhenPad = 24;
constexpr double BarLeft = 28, BarRight = 246;         // the bar's ends within a row
constexpr double ThumbWidth = 370, ThumbHeight = 252, ThumbBorder = 7, CardGap = 28;
constexpr double StatsLabel = 290;
constexpr double FootTop = 676, FootHeight = 36, FootGap = 10, FootPadX = 14, FootPadY = 3;
constexpr double Border = 2;                            // the panels' and the tab's borders
constexpr double RailWidth = 7, DivotWidth = 5, DivotHeight = 12, DivotInset = 8;
constexpr double GrooveLeft = 14, GrooveWidth = 6, GrooveEnds = 0.2; // the accent: 20% short of each end
constexpr double OrbAside = 24;                         // the orbs' centres beside the title
// How the page moves: the bar glides to a row (a time constant: nine tenths of the way in
// about a tenth of a second, faster at first), and what was shown fades out over what is.
constexpr double GlideTime = 0.045, BumpRows = 0.14;
constexpr double FadeCard = 0.16, FadePage = 0.2, FadeFoot = 0.18, FadeOpen = 0.3;

/// A console's name on its tab, and the gradient a game of it without a cover is shown on.
struct SystemLook {
    const char* name;
    const char* label;
    const char* art;
};
constexpr SystemLook SystemLooks[] = {
    {"Nintendo Switch", "Switch", "linear-gradient(135deg,#b8323c,#3a1016)"},
    {"Nintendo 3DS", "3DS", "linear-gradient(135deg,#8a8f99,#2c2f36)"},
    {"PlayStation", "PlayStation", "linear-gradient(135deg,#5b5b66,#16161c)"},
    {"PlayStation Portable", "PSP", "linear-gradient(135deg,#2d3a52,#0c0f18)"},
    {"PSP", "PSP", "linear-gradient(135deg,#2d3a52,#0c0f18)"},
    {"PlayStation Vita", "PS Vita", "linear-gradient(135deg,#3b4a63,#10141c)"},
    {"PlayStation 2", "PS2", "radial-gradient(ellipse at 40% 30%,#3c5466,#121a22 75%)"},
    {"Dreamcast", "Dreamcast", "linear-gradient(135deg,#d9752b,#4a2210)"},
    {"PC", "PC", "linear-gradient(135deg,#3a3f4a,#101216)"},
    {"PlayStation 3", "PS3", "linear-gradient(135deg,#3d3d44,#0e0e12)"},
};

const SystemLook* LookOf(const QString& system) {
    for (const SystemLook& known : SystemLooks) {
        if (system == QString::fromUtf8(known.name)) {
            return &known;
        }
    }
    return nullptr;
}

QString SystemLabel(const Library& library, const QString& system) {
    const SystemLook* known = LookOf(system);
    return known != nullptr ? QString::fromUtf8(known->label) : ShortSystem(library, system);
}

Fill SystemArt(const QString& system) {
    const SystemLook* known = LookOf(system);
    return ParseFill(QString::fromUtf8(known != nullptr ? known->art : "linear-gradient(135deg,#6b6f7a,#1c1e24)"));
}

/// Play time as a save screen has it: "12:05".
QString PlayClock(qint64 active_ms) {
    const qint64 minutes = active_ms / 60000;
    return QStringLiteral("%1:%2").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

/// "2026-10-22" as "2026/10/22", or "10/22" short.
QString DayText(const QString& day, bool year) {
    if (day.size() < 10) {
        return QStringLiteral("- -");
    }
    return (year ? day.left(4) + QLatin1Char('/') : QString()) + day.mid(5, 2) + QLatin1Char('/') + day.mid(8, 2);
}

/// The Settings page's rows. A slider has a key in the shared settings' "steam" (the Steam
/// Controller without Steam, which every reader app plays itself on Windows), its ends and
/// its step; a switch there has a key and no ends.
struct SettingRow {
    const char* name;
    const char* about;
    const char* key; ///< In "steam"; null for a row of Furoshiki's own
    double low, high, step, normal;
    /// The shelf's page it stands on (Reading, Cards, Sources, Controller, Furoshiki; "Save
    /// menu": only the old page has it). The old page lists them all as one.
    const char* section;
    /// Whose it is: 0 this window's, by its name; else the program's (SetSettingValue says it,
    /// SetSettingStep changes it): 1 a switch or a choice (Enter goes on to the next), 2
    /// something Enter opens, 3 a number (the arrows only).
    int host;
    /// Stands behind its page's "More" row: what few change.
    bool more;
};
constexpr SettingRow SettingRows[] = {
    {"Click words in the game's text", "", nullptr, 0, 0, 0, 0, "Reading", 1, false},
    {"Look up by pointing", "", nullptr, 0, 0, 0, 0, "Reading", 1, false},
    {"Mark the word looked up", "", nullptr, 0, 0, 0, 0, "Reading", 1, false},
    {"Dictionaries", "", nullptr, 0, 0, 0, 0, "Reading", 2, false},
    {"Explain with", "Ctrl+E on a line: its English and its grammar, by Claude Code on this computer", nullptr, 0, 0, 0, 0, "Reading", 1, false},
    {"More", "", nullptr, 0, 0, 0, 0, "Reading", 0, false},
    {"Play by touch", "", nullptr, 0, 0, 0, 0, "Reading", 1, true},
    {"Outline what can be clicked", "", nullptr, 0, 0, 0, 0, "Reading", 1, true},
    {"Session line over the game", "", nullptr, 0, 0, 0, 0, "Reading", 1, true},
    {"Session line's look", "", nullptr, 0, 0, 0, 0, "Reading", 1, true},
    {"Pause the reading clock", "", nullptr, 0, 0, 0, 0, "Reading", 1, true},
    {"Idle after", "A line with no new one for this long stops the reading clock", nullptr, 0, 0, 0, 0, "Reading", 3, true},
    {"Tidy reading time", "", nullptr, 0, 0, 0, 0, "Reading", 2, true},
    {"Daily goal", "Characters to read in a day; the statistics say how far today is", nullptr, 0, 0, 0, 0, "Reading", 0, true},
    {"Discord status", "", nullptr, 0, 0, 0, 0, "Reading", 1, true},
    {"Discord application ID", "", nullptr, 0, 0, 0, 0, "Reading", 2, true},
    {"Customise the popup", "Its look and every part of it, with a preview", nullptr, 0, 0, 0, 0, "Popup", 2, false},
    {"How solid", "Lower lets the game show through the popup; as its look has it at the left end", nullptr, 0, 0, 0, 0, "Popup", 3, false},
    {"Text size", "", nullptr, 0, 0, 0, 0, "Popup", 3, false},
    {"Height, at most", "", nullptr, 0, 0, 0, 0, "Popup", 3, false},
    {"More", "", nullptr, 0, 0, 0, 0, "Popup", 0, false},
    {"Close after", "", nullptr, 0, 0, 0, 0, "Popup", 3, true},
    {"Close when the pointer leaves", "", nullptr, 0, 0, 0, 0, "Popup", 1, true},
    {"Add button's corner", "", nullptr, 0, 0, 0, 0, "Popup", 1, true},
    {"Anki", "Where cards go: the deck, the note type and what each field gets", nullptr, 0, 0, 0, 0, "Cards", 2, false},
    {"Line audio", "", nullptr, 0, 0, 0, 0, "Cards", 1, false},
    {"Cut the clip to the voice", "", nullptr, 0, 0, 0, 0, "Cards", 1, false},
    {"Lines before, in {context}", "", nullptr, 0, 0, 0, 0, "Cards", 3, false},
    {"More", "", nullptr, 0, 0, 0, 0, "Cards", 0, false},
    {"Keep the game's audio", "", nullptr, 0, 0, 0, 0, "Cards", 1, true},
    {"Clip, at most", "", nullptr, 0, 0, 0, 0, "Cards", 3, true},
    {"Start the clip earlier by", "", nullptr, 0, 0, 0, 0, "Cards", 3, true},
    {"Pictures kept", "", nullptr, 0, 0, 0, 0, "Cards", 3, true},
    {"Picture width, at most", "", nullptr, 0, 0, 0, 0, "Cards", 3, true},
    {"Add a title", "A disc or file of a console, a game's window on this computer read by OCR, or a text hook's lines: Enter, and it asks what it needs", nullptr, 0, 0, 0, 0, "Sources", 0, false},
    {"OCR helper", "Furochibi's service, which reads a game window's pictures: its address, this device's picture source there and its token; Enter to change", nullptr, 0, 0, 0, 0, "Sources", 0, false},
    {"Lines address", "The WebSocket a title read from lines listens to: a texthooker's, such as ws://localhost:6677; Enter to change", nullptr, 0, 0, 0, 0, "Sources", 0, false},
    {"More", "", nullptr, 0, 0, 0, 0, "Sources", 0, false},
    {"Copy each line to the clipboard", "", nullptr, 0, 0, 0, 0, "Sources", 1, true},
#ifndef READER_NO_TEXT_SERVER
    {"Text server", "Every line read, sent to a WebSocket for texthooker pages", nullptr, 0, 0, 0, 0, "Sources", 1, true},
#endif
#ifndef READER_NO_TEXT_SERVER
    {"Text server's port", "", nullptr, 0, 0, 0, 0, "Sources", 3, true},
#endif
#ifndef READER_NO_TEXT_SERVER
    {"Other devices may connect", "", nullptr, 0, 0, 0, 0, "Sources", 1, true},
#endif
#ifndef READER_NO_TEXT_SERVER
    {"Text server's format", "", nullptr, 0, 0, 0, 0, "Sources", 1, true},
#endif
#ifndef READER_NO_TEXT_SERVER
    {"Speaker's name before the line", "", nullptr, 0, 0, 0, 0, "Sources", 1, true},
#endif
    {"Read again what the reader apps say", "", nullptr, 0, 0, 0, 0, "Sources", 0, true},
    {"Pointer button", "Held, the sticks move the reader's pointer instead of the game; none: every button is the game's. The mouse and the Steam Controller's pad always point", nullptr, 0, 0, 0, 0, "Controller", 1, false},
    {"Pointer button: press once", "", nullptr, 0, 0, 0, 0, "Controller", 1, false},
    {"Pointer speed", "", nullptr, 0, 0, 0, 0, "Controller", 3, false},
    {"Reader menu button", "", nullptr, 0, 0, 0, 0, "Controller", 1, false},
    {"Trackpad speed", "How far the pointer goes for a swipe of the right pad", "sensitivity", 0.2, 3.0, 0.1, 1.0, "Controller", 0, false},
    {"More", "", nullptr, 0, 0, 0, 0, "Controller", 0, false},
    {"Reading clock button", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Frame the character pointed at", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Pointer jumps to each new line", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Grip 1", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Grip 2", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Grip 3", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Grip 4", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"The \"...\" button", "", nullptr, 0, 0, 0, 0, "Controller", 1, true},
    {"Trackpad acceleration", "Extra speed for fast swipes", "acceleration", 0.0, 2.0, 0.25, 1.0, "Controller", 0, true},
    {"Scroll speed", "How far a swipe of the left pad turns the wheel", "scroll", 0.2, 3.0, 0.1, 1.0, "Controller", 0, true},
    {"Pointer rolls on", "After a flick, as a trackball does", "trackball", 0, 0, 0, 1, "Controller", 0, true},
    {"Pad ticks", "The pads tick under the finger and click when pressed", "haptics", 0, 0, 0, 1, "Controller", 0, true},
    {"Tick strength", "", "tick_strength", 0.0, 3.0, 0.25, 1.0, "Controller", 0, true},
    {"Click strength", "", "click_strength", 0.0, 3.0, 0.25, 1.0, "Controller", 0, true},
    // Keys: what each does, said once (the controller's own are the Controller page's rows).
    {"A card of the sentence", "Ctrl+D", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"A card of the word looked up", "Ctrl+W", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Take the last card back", "Ctrl+Z", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Explain the line", "Ctrl+E", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Backlog", "Ctrl+H", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Pause the game", "Ctrl+G", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Pause the reading clock", "Ctrl+T", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Next choice", "Ctrl+N", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Reader menu", "F1, or the right button", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Menu over the game", "Escape, or Guide", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Settings", "F2", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Find a setting", "/ on the settings", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Full screen", "F11", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Next look", "F4 on the shelf", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Sort the shelf", "S", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Find box art", "F on a title", nullptr, 0, 0, 0, 0, "Keys", 0, false},
    {"Look", "This window's theme; left and right change it", nullptr, 0, 0, 0, 0, "Furoshiki", 0, false},
    {"Wide picture", "Games drawn for a wide screen (16:9, 21:9, 32:9), never stretched: the PS2 and the Switch, for the games that have a checked recipe; from the next game started", nullptr, 0, 0, 0, 0, "Furoshiki", 0, false},
    {"CRT picture", "The games shown as on a tube: for all consoles, one console or one game", nullptr, 0, 0, 0, 0, "Furoshiki", 2, false},
    {"Full screen", "", nullptr, 0, 0, 0, 0, "Furoshiki", 0, false},
#ifndef READER_NO_SAVE_SYNC
    {"Save sync", "The games' saves kept the same on every device, through a save sync service of your own", nullptr, 0, 0, 0, 0, "Furoshiki", 0, false},
#endif
    {"RetroAchievements", "Your login and the hardcore switch, for the cores that have achievements; Enter to change", nullptr, 0, 0, 0, 0, "Furoshiki", 0, false},
    {"More", "", nullptr, 0, 0, 0, 0, "Furoshiki", 0, false},
    {"Icon", "The program's icon, on its window and the task bar; left and right change it", nullptr, 0, 0, 0, 0, "Furoshiki", 0, true},
    {"Menu sounds", "", nullptr, 0, 0, 0, 0, "Furoshiki", 0, true},
    {"Games start in a window", "Most of the screen's height, in the middle; off: full screen where the app says how", nullptr, 0, 0, 0, 0, "Furoshiki", 0, true},
#ifndef READER_NO_SAVE_SYNC
    {"Pair a device", "Save sync set up from the pairing link the service's page shows: Enter, then paste it", nullptr, 0, 0, 0, 0, "Furoshiki", 0, true},
#endif
    {"Study tracker", "Where your study tracker answers: its figures stand on the shelf", nullptr, 0, 0, 0, 0, "Furoshiki", 2, true},
#ifndef READER_NO_SAVE_SYNC
    {"Update", "A newer build of the program from your Files page: Enter looks, and Enter again takes it for the next start", nullptr, 0, 0, 0, 0, "Furoshiki", 2, true},
    {"Home server's services", "The tracker, Furochibi, the guides, the Files and Anki, as the save sync service this device is paired with names them: Enter asks it again", nullptr, 0, 0, 0, 0, "Furoshiki", 2, true},
#endif
    {"Fuzz", "A little colour fringe, softness, grain and fine lines, as an old screen had; off, light or heavy", nullptr, 0, 0, 0, 0, "Save menu", 0, false},
    {"Moving cursor", "The orbs turn and spring to the chosen title, its glow breathes; off: they stand still", nullptr, 0, 0, 0, 0, "Save menu", 0, false},
    {"Exit", "", nullptr, 0, 0, 0, 0, "Furoshiki", 0, true},
    // Developer: the pages' own words, changed in place and handed back for the next build.
    {"Rename labels", "On: Enter or a click on any row of any page asks what it is to be called; a click on the page's own name renames the page", nullptr, 0, 0, 0, 0, "Developer", 0, false},
    {"Copy my labels", "Every label changed here, as text on the clipboard, for the next build", nullptr, 0, 0, 0, 0, "Developer", 0, false},
    {"Open the labels file", "", nullptr, 0, 0, 0, 0, "Developer", 0, false},
    {"Put every label back", "", nullptr, 0, 0, 0, 0, "Developer", 0, false},
};
constexpr int SettingCount = static_cast<int>(sizeof(SettingRows) / sizeof(SettingRows[0]));

/// What stands for a file in a card's cover: this and an icon's key.
const QString IconCover = QStringLiteral("icon:");

bool IsSetting(int row, const char* name) {
    return row >= 0 && row < SettingCount && qstrcmp(SettingRows[row].name, name) == 0;
}

/// The shelf's settings pages, in their order.
constexpr const char* SettingSections[] = {"Reading", "Popup", "Cards", "Sources", "Controller", "Keys", "Furoshiki", "Developer"};

/// What a row is called on the shelf's pages, where that is not its name in the table (the
/// maintainer, 2026-11-11: labels that say what the row does, and no "reader" in them). The
/// table's names stay what the code, the kept settings and the tests know a row by. A name two
/// pages have is told apart as "Page/Name". The user's own labels (the Developer page,
/// program/labels.json) come before these; one that stays is written in here for the next build.
struct RowLabel {
    const char* name;
    const char* label;
};
constexpr RowLabel RowLabels[] = {
    {"Click words in the game's text", "Click a word to look it up"},
    {"Look up by pointing", "Look up on hover"},
    {"Mark the word looked up", "Highlight the looked-up word"},
    {"Explain with", "Explain lines with"},
    {"Play by touch", "Tap the game to advance"},
    {"Outline what can be clicked", "Outline clickable text"},
    {"Session line over the game", "Session figures over the game"},
    {"Session line's look", "Session figures style"},
    {"Reading/Pause the reading clock", "Reading timer paused"},
    {"Idle after", "Stop the timer when idle for"},
    {"Tidy reading time", "Recalculate reading time"},
    {"Customise the popup", "Popup design"},
    {"How solid", "Opacity"},
    {"Height, at most", "Maximum height"},
    {"Close after", "Close by itself after"},
    {"Add button's corner", "Add button position"},
    {"Anki", "Anki setup"},
    {"Line audio", "Audio on cards"},
    {"Cut the clip to the voice", "Trim audio to the voice"},
    {"Lines before, in {context}", "Context lines before"},
    {"Keep the game's audio", "Record game audio"},
    {"Clip, at most", "Maximum audio length"},
    {"Start the clip earlier by", "Start audio earlier by"},
    {"Pictures kept", "Screenshots kept"},
    {"Picture width, at most", "Maximum screenshot width"},
    {"Lines address", "Texthooker address"},
    {"Text server", "Send lines to texthooker pages"},
    {"Text server's port", "Text server port"},
    {"Other devices may connect", "Allow other devices"},
    {"Text server's format", "Text server format"},
    {"Speaker's name before the line", "Include the speaker's name"},
    {"Read again what the reader apps say", "Refresh the library"},
    {"Pointer button: press once", "Pointer button toggles"},
    {"Reader menu button", "Quick menu button"},
    {"Reading clock button", "Timer pause button"},
    {"Frame the character pointed at", "Box the character under the pointer"},
    {"The \"...\" button", "The … button"},
    {"Pointer rolls on", "Trackball glide"},
    {"Pad ticks", "Trackpad haptics"},
    {"Tick strength", "Haptic tick strength"},
    {"Click strength", "Haptic click strength"},
    {"A card of the sentence", "Make a card of the line"},
    {"A card of the word looked up", "Make a card of the looked-up word"},
    {"Take the last card back", "Undo the last card"},
    {"Keys/Pause the reading clock", "Pause the reading timer"},
    {"Next choice", "Skip to the next choice"},
    {"Reader menu", "Quick menu"},
    {"Menu over the game", "Game menu"},
    {"Next look", "Next theme"},
    {"Look", "Theme"},
    {"Wide picture", "Widescreen"},
    {"CRT picture", "CRT effect"},
    {"Games start in a window", "Start games in a window"},
    {"Home server's services", "Home server services"},
    {"Update", "Check for an update"},
    {"Statistics/Days in a row", "Day streak"},
    {"Statistics/Days with reading", "Days read"},
    {"Statistics/Games begun", "Games started"},
};

/// The label a row has by itself: RowLabels', else its name.
QString OwnLabel(const QString& page, const QString& name) {
    const QString full = page + QLatin1Char('/') + name;
    for (const RowLabel& each : RowLabels) {
        if (full == QLatin1String(each.name)) {
            return QString::fromUtf8(each.label);
        }
    }
    for (const RowLabel& each : RowLabels) {
        if (name == QLatin1String(each.name)) {
            return QString::fromUtf8(each.label);
        }
    }
    return name;
}

/// Whether a row of the table stands on `section`'s page, with its More opened or not; the old
/// page (no section) has them all as one list, and no More rows.
bool SettingShown(int index, const QString& section, bool more) {
    const SettingRow& row = SettingRows[index];
    if (section.isEmpty()) {
        return qstrcmp(row.name, "More") != 0;
    }
    return section == QLatin1String(row.section) && (more || !row.more);
}

/// Which row of the table the page's `row` is, or -1.
int SettingAt(const QString& section, bool more, int row) {
    for (int i = 0; i < SettingCount; ++i) {
        if (SettingShown(i, section, more) && row-- == 0) {
            return i;
        }
    }
    return -1;
}

/// The daily goal's steps: off, then every 500 characters.
constexpr int GoalStep = 500, GoalMost = 30000;

/// Whether every key the page takes is written to the log (with where it came from): while a
/// file "trace-keys" is in the shared folder's "program" folder (a controller that doubles keys).
bool TraceKeys() {
    static const bool on = QFileInfo::exists(SharedFolder() + QStringLiteral("/program/trace-keys"));
    return on;
}

/// Whether the time each frame and each key takes is written to the log: while a file
/// "trace-frames" is in the shared folder's "program" folder (scrolling that lags).
bool TraceFrames() {
    static const bool on = QFileInfo::exists(SharedFolder() + QStringLiteral("/program/trace-frames"));
    return on;
}

/// The frames' times while traced, and how long a key waited for the frame that shows it.
struct FrameTrace {
    QElapsedTimer since;
    qint64 key_waiting{-1}; ///< When the first key not yet shown came (ns), or -1
    QList<double> frames, waits;

    void Key() {
        if (!since.isValid()) {
            since.start();
        }
        if (key_waiting < 0) {
            key_waiting = since.nsecsElapsed();
        }
    }
    void Frame(double ms, const char* what) {
        if (!since.isValid()) {
            since.start();
        }
        frames.append(ms);
        double wait = -1;
        if (key_waiting >= 0) {
            wait = (since.nsecsElapsed() - key_waiting) / 1e6;
            waits.append(wait);
            key_waiting = -1;
        }
        qInfo("frame %.1f ms%s%s", ms, what, wait >= 0 ? qPrintable(QStringLiteral(", a key %1 ms before").arg(wait, 0, 'f', 1)) : "");
    }
    void Summary() {
        const auto line = [](const char* name, QList<double> v) {
            if (v.isEmpty()) {
                return;
            }
            std::sort(v.begin(), v.end());
            double sum = 0;
            for (const double x : v) {
                sum += x;
            }
            qInfo("%s: %lld, mean %.1f ms, median %.1f, 90th %.1f, most %.1f", name, static_cast<long long>(v.size()), sum / v.size(),
                  v[v.size() / 2], v[std::min<qsizetype>(v.size() - 1, v.size() * 9 / 10)], v.last());
        };
        line("frames", frames);
        line("key to its frame", waits);
        frames.clear();
        waits.clear();
    }
};

FrameTrace& Trace() {
    static FrameTrace trace;
    return trace;
}

/// Whether the system asks for less motion (Windows: its animations are off).
bool SystemWantsStillness() {
#ifdef _WIN32
    BOOL animate = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0)) {
        return animate == FALSE;
    }
#endif
    return false;
}

/// CSS's cubic-bezier() timing: the share of the way at a share of the time.
double Bezier(double x1, double y1, double x2, double y2, double x) {
    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double cx = 3 * x1 * t * (1 - t) * (1 - t) + 3 * x2 * t * t * (1 - t) + t * t * t - x;
        const double dx = 3 * x1 * (1 - t) * (1 - t) + 6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
        if (std::fabs(cx) < 1e-5 || std::fabs(dx) < 1e-6) {
            break;
        }
        t = std::clamp(t - cx / dx, 0.0, 1.0);
    }
    return 3 * y1 * t * (1 - t) * (1 - t) + 3 * y2 * t * t * (1 - t) + t * t * t;
}

/// How the orbs move on to another row, as in the KID choice dialogs: they close in on the
/// new title's middle, crossing over and under its glyphs (`OrbClose` s, ease-in-out), then
/// spring back out to its sides, a little past and once a little in again (a damped spring,
/// `OrbDamping` of critical, a swing of `OrbSwing` s), and are still at the sides after
/// `OrbSettle` s. `from` is how far out they were when the row changed (1: at the sides).
constexpr double Pi = 3.141592653589793;
constexpr double OrbClose = 0.22, OrbDamping = 0.45, OrbSwing = 0.55, OrbSettle = 1.4;
constexpr double OrbSpin = 2.4;          // a loop of the rings' turning
constexpr double OrbBob = 1.5, OrbBobPx = 2; // the drift up and down at rest, each way
constexpr double OrbNear = 0.18;         // how much nearer (larger) orb A, farther orb B, in the middle
constexpr double OrbReach = 140;         // the farthest they close in (a title's middle, if nearer)

double OrbSpread(double t, double from) {
    if (t >= OrbSettle) {
        return 1;
    }
    if (t < OrbClose) {
        return from * (1 - Bezier(0.42, 0, 0.58, 1, t / OrbClose));
    }
    const double s = t - OrbClose;
    const double wd = 2 * Pi / OrbSwing;
    const double wn = wd / std::sqrt(1 - OrbDamping * OrbDamping);
    const double decay = std::exp(-OrbDamping * wn * s);
    return 1 - decay * (std::cos(wd * s) + OrbDamping * wn / wd * std::sin(wd * s));
}

/// The glow behind the chosen title: up at once on a new row, brighter at first, then
/// breathing slowly about its rest.
constexpr double GlowRest = 0.88, GlowFlare = 1, GlowRise = 0.06, GlowFall = 0.5, GlowBreath = 2.6, GlowBreathBy = 0.08;
constexpr double GlowWide = 34, GlowTall = 0.62; // past each end of the title (px); half its height as a share of the line
constexpr double HoverGlow = 0.34; // of that glow, behind the title under the pointer

double GlowAt(double t) {
    if (t < GlowRise) {
        return GlowFlare * t / GlowRise;
    }
    if (t < GlowRise + GlowFall) {
        return GlowFlare + (GlowRest - GlowFlare) * Bezier(0.42, 0, 0.58, 1, (t - GlowRise) / GlowFall);
    }
    return GlowRest + GlowBreathBy * std::sin(2 * Pi * (t - GlowRise - GlowFall) / GlowBreath);
}

} // namespace

/// The theme's values, read from its file once when it is chosen.
struct HomeWindow::Look {
    explicit Look(const Theme& given) : theme(given) {
        const Theme& t = theme;
        page = t.Colour(QStringLiteral("page"), QColor(0x80, 0x80, 0x80));
        txt = t.Colour(QStringLiteral("txt"));
        sel = t.Colour(QStringLiteral("sel-txt"));
        dim = t.Colour(QStringLiteral("dim"));
        tab_txt = t.Colour(QStringLiteral("tab-txt"));
        frame = t.Colour(QStringLiteral("frame"));
        card_frame = t.Colour(QStringLiteral("card-frame"), frame);
        divot = t.Colour(QStringLiteral("divot"));
        line = t.Colour(QStringLiteral("line"));
        thumb_frame = t.Colour(QStringLiteral("thumb-frame"));
        row_rule = t.Colour(QStringLiteral("row-rule"));
        bg = t.FillOf(QStringLiteral("bg"));
        panel_bg = t.FillOf(QStringLiteral("panel-bg"));
        bar = t.FillOf(QStringLiteral("bar"));
        tab_bg = t.FillOf(QStringLiteral("tab-bg"));
        tab_on = t.FillOf(QStringLiteral("tab-on"));
        groove = t.FillOf(QStringLiteral("accent-groove"));
        rail = ParseFill(QStringLiteral("linear-gradient(90deg,%1 0 2px,%2 2px 4px,rgba(0,0,0,0) 4px)")
                             .arg(t.Text(QStringLiteral("rail-hi")), t.Text(QStringLiteral("rail-lo"))));
        card_rail = t.Has(QStringLiteral("card-rail")) ? t.FillOf(QStringLiteral("card-rail")) : rail;
        txt_sh = t.Shadows(QStringLiteral("txt-sh"));
        sel_sh = t.Shadows(QStringLiteral("sel-sh"));
        dim_sh = t.Shadows(QStringLiteral("dim-sh"));
        tab_sh = t.Shadows(QStringLiteral("tab-sh"));
        panel_shadow = t.Shadows(QStringLiteral("panel-shadow"));
        card_shadow = t.Has(QStringLiteral("card-shadow")) ? t.Shadows(QStringLiteral("card-shadow")) : panel_shadow;
        tab_shadow = t.Shadows(QStringLiteral("tab-shadow"));
        thumb_shadow = t.Shadows(QStringLiteral("thumb-shadow"));
        glow = t.Shadows(QStringLiteral("accent-glow"));
        divot_sh = t.Shadows(QStringLiteral("divot-sh"));
        line_sh = t.Shadows(QStringLiteral("line-sh"));
        tab_on_shadow = t.Shadows(QStringLiteral("tab-on-shadow"));
        orb_glow = t.Shadows(QStringLiteral("orb-glow"));
        cursor_glow = t.Has(QStringLiteral("cursor-glow")) ? t.Colour(QStringLiteral("cursor-glow")) : QColor();
        // "3px #f78fc9": an outline's width and colour.
        const QStringList outline = SplitTop(t.Text(QStringLiteral("thumb-outline")), QLatin1Char(' '));
        if (outline.size() >= 2) {
            thumb_outline_width = ParseLength(outline[0]);
            thumb_outline = ParseColour(outline[1]);
        }
        // A pixel font made larger is drawn smoothed by the theme's "smooth" (0: as it is).
        for (const QString& role : TypeRoles()) {
            Paint::SetSmoothing(Paint::FontOf(t.Type(role)).family(), t.Number(QStringLiteral("smooth"), 0));
        }
        head = Paint::FontOf(t.Type(QStringLiteral("head")));
        clock = Paint::FontOf(t.Type(QStringLiteral("clock")));
        file = Paint::FontOf(t.Type(QStringLiteral("file")));
        title = Paint::FontOf(t.Type(QStringLiteral("title")));
        sub = Paint::FontOf(t.Type(QStringLiteral("sub")));
        stats = Paint::FontOf(t.Type(QStringLiteral("stats")));
        number = Paint::FontOf(t.Type(QStringLiteral("number")));
        row = Paint::FontOf(t.Type(QStringLiteral("row")));
        empty = Paint::FontOf(t.Type(QStringLiteral("empty")));
        when = Paint::FontOf(t.Type(QStringLiteral("when")));
        tab = Paint::FontOf(t.Type(QStringLiteral("tab")));
        lr = Paint::FontOf(t.Type(QStringLiteral("lr")));
        // The card's notes: the stats' type without its wide spacing.
        note = stats;
        note.setLetterSpacing(QFont::AbsoluteSpacing, 0.04 * t.Type(QStringLiteral("stats")).size);
        title_line = t.Type(QStringLiteral("title")).line;
        sub_line = t.Type(QStringLiteral("sub")).line;
        stats_line = std::max(1.0, t.Type(QStringLiteral("stats")).line);
        panel_blur = t.Number(QStringLiteral("panel-blur"), 0);
        bar_inset = t.Number(QStringLiteral("bar-inset"), 2);
        tab_height = t.Number(QStringLiteral("tab-height"), 54);
        detail_pad = t.Number(QStringLiteral("layout"), QStringLiteral("detail-pad"), 30);
        list_left = t.Number(QStringLiteral("layout"), QStringLiteral("list-pad-left"), 30);
        list_right = t.Number(QStringLiteral("layout"), QStringLiteral("list-pad-right"), 18);
        vline_left = t.Number(QStringLiteral("layout"), QStringLiteral("vline-left"), 90);
        vline_right = t.Number(QStringLiteral("layout"), QStringLiteral("vline-right"), 246);
        clock_top = t.Number(QStringLiteral("clock"), QStringLiteral("top"), 10);
        clock_format = t.Text(QStringLiteral("clock"), QStringLiteral("format"));
        const QJsonObject box = t.Object(QStringLiteral("clock"));
        clock_box = box.contains(QStringLiteral("background"));
        clock_colour = clock_box ? ParseColour(t.Text(QStringLiteral("clock"), QStringLiteral("color"))) : txt;
        clock_bg = ParseFill(t.Text(QStringLiteral("clock"), QStringLiteral("background")));
        clock_shadow = ParseShadows(t.Text(QStringLiteral("clock"), QStringLiteral("shadow")));
        clock_text_sh = clock_box ? ParseShadows(t.Text(QStringLiteral("clock"), QStringLiteral("text-sh"))) : txt_sh;
        const QJsonArray padding = box.value(QStringLiteral("padding")).toArray();
        clock_pad_y = padding.size() > 0 ? padding[0].toDouble() : 0;
        clock_pad_x = padding.size() > 1 ? padding[1].toDouble() : clock_pad_y;
        const QStringList border = SplitTop(t.Text(QStringLiteral("clock"), QStringLiteral("border")), QLatin1Char(' '));
        if (border.size() >= 2) {
            clock_border = ParseLength(border[0]);
            clock_border_colour = ParseColour(border[1]);
        }
        file_text = t.Text(QStringLiteral("file-text"));
        head_upper = t.Text(QStringLiteral("head-case")) == QStringLiteral("upper");
        fuzz_default = t.Text(QStringLiteral("fuzz"), QStringLiteral("default"));
    }

    [[nodiscard]] Paint::FuzzSpec Fuzz(const QString& level) const {
        Paint::FuzzSpec spec;
        spec.level = theme.Fuzz(level);
        spec.grain = theme.Number(QStringLiteral("fuzz"), QStringLiteral("grain"), 0);
        spec.grain_frequency = theme.Number(QStringLiteral("fuzz"), QStringLiteral("grain-frequency"), 0.9);
        spec.interlace = theme.Number(QStringLiteral("fuzz"), QStringLiteral("interlace"), 0);
        return spec;
    }

    Theme theme;
    QColor page, txt, sel, dim, tab_txt, frame, card_frame, divot, line, thumb_frame, row_rule, thumb_outline, cursor_glow;
    Fill bg, panel_bg, bar, tab_bg, tab_on, groove, rail, card_rail, clock_bg;
    QList<Shadow> txt_sh, sel_sh, dim_sh, tab_sh, panel_shadow, card_shadow, tab_shadow, thumb_shadow, glow, divot_sh,
        line_sh, tab_on_shadow, orb_glow, clock_shadow, clock_text_sh;
    QFont head, clock, file, title, sub, stats, note, number, row, empty, when, tab, lr;
    double title_line{}, sub_line{}, stats_line{36}, panel_blur{}, bar_inset{2}, tab_height{54}, detail_pad{30},
        list_left{30}, list_right{18}, vline_left{90}, vline_right{246}, thumb_outline_width{};
    double clock_top{10}, clock_pad_y{}, clock_pad_x{}, clock_border{};
    QColor clock_colour, clock_border_colour;
    QString clock_format, file_text, fuzz_default;
    bool clock_box{}, head_upper{};
};

/// What the card shows of the chosen row.
struct HomeWindow::Card {
    QString file;  ///< "FILE 3  SLPM-65550"
    QString title;
    QString sub;   ///< Under the title: the console and n／total, or a setting's value
    bool sub_dim{};
    QList<QPair<QString, QString>> stats; ///< Up to three, at the card's foot
    QString note;  ///< Words at the foot where there are no stats
    enum Picture { Art, Chart, Value } picture{Art};
    QString cover; ///< Art: the cover's file, else the console's gradient
    QString system;
    int slider{-1}; ///< Value: a setting's slider drawn under it
};

void PlaceFirst(QWidget* window) {
    if (QGuiApplication::platformName() == QStringLiteral("offscreen") ||
        qEnvironmentVariableIsSet("READER_DESKTOP_CONTROL")) {
        return;
    }
    const QRect room = window->screen()->availableGeometry();
    const int title = window->style()->pixelMetric(QStyle::PM_TitleBarHeight);
    const QSize size = FirstWindowSize(room.size() - QSize(0, title));
    window->resize(size);
    window->move(room.x() + (room.width() - size.width()) / 2, room.y() + (room.height() - size.height() - title) / 2);
}

HomeWindow::HomeWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Furoshiki"));
    setMinimumSize(640, 360);
    resize(1280, 720);
    if (parent == nullptr) {
        PlaceFirst(this);
    }
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true); // the pointer shows what it is over
    // Where the system lists no windows (not Windows), the program's own stands in for one.
    // While the wizard that lists the windows is open, as it looked before that.
    Windows::SetOwn(QStringLiteral("Furoshiki"), [this] { return wizard_on ? own_picture : grab().toImage(); });
    QSettings settings;
    sound_on = settings.value(QStringLiteral("sounds"), true).toBool();
    full_screen = settings.value(QStringLiteral("full_screen"), false).toBool();
#ifdef _WIN32
    const bool windowed_as_it_comes = true; // a desk with a monitor
#else
    const bool windowed_as_it_comes = false;
#endif
    games_windowed = settings.value(QStringLiteral("games_windowed"), windowed_as_it_comes).toBool();
    // The look: the theme chosen last (the first of them at first), the fuzz if one was chosen
    // (else the theme's own), the orbs moving unless the system asks for stillness.
    themes = LoadThemes();
    if (themes.isEmpty()) {
        themes.append(Theme(QStringLiteral("none"), {}));
    }
    const QString theme_key = settings.value(QStringLiteral("look/theme")).toString();
    for (int i = 0; i < themes.size(); ++i) {
        if (themes[i].Key() == theme_key) {
            theme_index = i;
        }
    }
    look = std::make_unique<Look>(themes[theme_index]);
    fuzz_choice = settings.value(QStringLiteral("look/fuzz")).toString();
    motion = settings.value(QStringLiteral("look/motion"), !SystemWantsStillness()).toBool();
    sounds.SetEnabled(sound_on);
    clock.start();
    orb_clock.start();
    spin_clock.start();
    Reload();
    Clamp();
    connect(&ticker, &QTimer::timeout, this, &HomeWindow::Step);
    ShelfLoad();
    ticker.setTimerType(Qt::PreciseTimer); // a coarse timer's frames come unevenly
    ticker.start(16);
    Pace(frame_cost);
    connect(&child, &QProcess::finished, this, [this] { Finished(); });
    // The controller's buttons arrive from the thread that reads it.
    connect(&pad, &SteamPad::KeyPressed, this, [this](int key) {
        if (TraceKeys()) {
            qInfo("key from the Steam Controller: %s", qPrintable(QKeySequence(key).toString()));
        }
        Key(key);
    }, Qt::QueuedConnection);
    connect(&child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const QString what = running;
            running.clear();
            ShowAsSet();
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("%1 did not start: %2").arg(what, child.errorString()));
        }
    });
}

HomeWindow::~HomeWindow() = default;

QStringList HomeWindow::ThemeNames() const {
    QStringList names;
    for (const Theme& theme : themes) {
        names.append(theme.Name());
    }
    return names;
}

void HomeWindow::SetTheme(int index, bool keep) {
    if (themes.isEmpty()) {
        return;
    }
    theme_index = ((index % themes.size()) + themes.size()) % themes.size();
    look = std::make_unique<Look>(themes[theme_index]);
    Paint::ForgetShadows();
    ground_key.clear();
    if (keep) {
        QSettings().setValue(QStringLiteral("look/theme"), themes[theme_index].Key());
    }
    Changed();
}

void HomeWindow::SetFuzz(const QString& level, bool keep) {
    fuzz_choice = level;
    if (keep) {
        QSettings().setValue(QStringLiteral("look/fuzz"), level);
    }
    Changed();
}

void HomeWindow::SetMotion(bool on, bool keep) {
    motion = on;
    orb_clock.restart();
    if (keep) {
        QSettings().setValue(QStringLiteral("look/motion"), on);
    }
    Changed();
}

QString HomeWindow::FuzzLevel() const {
    const QString level = fuzz_choice.isEmpty() ? look->fuzz_default : fuzz_choice;
    return level == QStringLiteral("light") || level == QStringLiteral("heavy") ? level : QStringLiteral("off");
}

void HomeWindow::Changed() {
    scene_dirty = true;
    update();
}

void HomeWindow::Changed(int parts) {
    dirty_parts |= parts;
    update();
}

void HomeWindow::ShowAsSet() {
    // The window this page is in: itself, or the one program's window (main_window.h).
    QWidget* top = window();
    full_screen && !window_forced ? top->showFullScreen() : top->showNormal();
}

void HomeWindow::ReloadKeepingChoice() {
    // What is chosen, by what it is rather than where it is: the order changes as games are
    // read (the last read first) and as apps add consoles.
    QList<Item> items = Items();
    const QString label = items.isEmpty() ? QString() : items[axis_choice].label;
    QHash<QString, QString> games; // a console's label -> the chosen game's app and ID
    for (const Item& item : items) {
        if (item.kind == Item::System) {
            const QList<Game> list = GamesOf(item.system);
            const int row = row_choice.value(item.label);
            if (row >= 0 && row < list.size()) {
                games.insert(item.label, list[row].app + QLatin1Char('\n') + list[row].id + QLatin1Char('\n') + list[row].path);
            }
        }
    }
    Reload();
    items = Items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].label == label) {
            axis_choice = i;
        }
        if (items[i].kind == Item::System && games.contains(items[i].label)) {
            const QList<Game> list = GamesOf(items[i].system);
            for (int row = 0; row < list.size(); ++row) {
                if (list[row].app + QLatin1Char('\n') + list[row].id + QLatin1Char('\n') + list[row].path ==
                    games.value(items[i].label)) {
                    row_choice[items[i].label] = row;
                }
            }
        }
    }
    // Every page's row within its rows, not only the one shown.
    for (const Item& item : items) {
        if (row_choice.contains(item.label)) {
            int& row = row_choice[item.label];
            row = std::clamp(row, 0, std::max(0, static_cast<int>(Rows(item).size()) - 1));
        }
    }
    Clamp();
}

void HomeWindow::Reload() {
    library = Load(SharedFolder());
    shelf_stale = true;
    tracker_asked = 0; // asked again, as the files are read again
    title_readings.clear();
    shared = LoadShared(SharedFolder());
    pad.SetTuning(TuningOf(shared.value(QStringLiteral("steam")).toObject()));
    covers.clear();
}

QStringList HomeWindow::Systems() const {
    QStringList out;
    for (const App& app : library.apps) {
        for (const Game& game : app.games) {
            if (!out.contains(game.system)) {
                out.append(game.system);
            }
        }
    }
    return out;
}

QList<HomeWindow::Item> HomeWindow::Items() const {
    QList<Item> items;
    for (const QString& system : Systems()) {
        items.append({Item::System, ShortSystem(library, system), system});
    }
    items.append({Item::Statistics, QStringLiteral("STATISTICS"), {}});
    if (!shelf_on) {
        // The old page: the apps' own menus, and every setting as one list.
        items.append({Item::Readers, QStringLiteral("READERS"), {}});
        if (!consoles.isEmpty() && play != nullptr) {
            items.append({Item::Consoles, QStringLiteral("CONSOLES"), {}});
        }
        items.append({Item::Settings, QStringLiteral("SETTINGS"), {}});
        return items;
    }
    // The shelf's: the settings by what they are for, the consoles' among them.
    for (const char* section : SettingSections) {
        const QString name = QString::fromLatin1(section);
        items.append({Item::Settings, name.toUpper(), name});
        if (name == QStringLiteral("Sources") && !consoles.isEmpty() && play != nullptr) {
            items.append({Item::Consoles, QStringLiteral("CONSOLES"), {}});
        }
    }
    return items;
}

QList<Game> HomeWindow::GamesOf(const QString& system) const {
    // A game the program plays itself (its file "reader-home-<console>") and an app lists too
    // is one row: the program's, with what was read of it in the app added to its figures.
    const auto own = [](const Game& game) { return game.app.startsWith(QStringLiteral("reader-home-")); };
    QList<Game> out;
    for (const Game& game : library.games) {
        if (game.system == system && own(game)) {
            out.append(game);
        }
    }
    for (const Game& game : library.games) {
        if (game.system != system || own(game)) {
            continue;
        }
        bool merged = false;
        for (Game& kept : out) {
            if (own(kept) && kept.id == game.id && !game.id.isEmpty()) {
                kept.chars += game.chars;
                kept.active_ms += game.active_ms;
                kept.last_day = std::max(kept.last_day, game.last_day);
                if (kept.cover.isEmpty()) {
                    kept.cover = game.cover;
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            out.append(game);
        }
    }
    // The last read first, as the library has them.
    std::stable_sort(out.begin(), out.end(), [](const Game& a, const Game& b) { return a.last_day > b.last_day; });
    return out;
}

QList<HomeWindow::Row> HomeWindow::Rows(const Item& item) {
    QList<Row> rows;
    const auto figures = [](qint64 chars, qint64 active_ms) {
        return chars > 0 ? QStringLiteral("%1 CHARACTERS  ·  %2").arg(CountText(chars), TimeText(active_ms).toUpper())
                         : QStringLiteral("NOT READ YET");
    };
    switch (item.kind) {
    case Item::System:
        for (const Game& game : GamesOf(item.system)) {
            QString meta = game.id + QStringLiteral("  ·  ") + figures(game.chars, game.active_ms);
            if (!game.last_day.isEmpty()) {
                meta += QStringLiteral("  ·  READ ") + game.last_day.mid(5).replace(QLatin1Char('-'), QStringLiteral(" / "));
            }
            const QString when = game.last_day.isEmpty() ? QStringLiteral("- -")
                                                         : DayText(game.last_day, false) + QLatin1Char(' ') + PlayClock(game.active_ms);
            rows.append({game.title, meta, when, game.cover, -1});
        }
        break;
    case Item::Statistics: {
        // A game's discs (and two copies of one) share its figures: each game counts once.
        const QList<Game> distinct = DistinctGames(library.games);
        int begun = 0;
        for (const Game& game : distinct) {
            begun += game.chars > 0 ? 1 : 0;
        }
        {
            const QString today = QDate::currentDate().toString(Qt::ISODate);
            const qint64 read = CharsOn(library.days, today);
            const int goal = DailyGoal();
            rows.append({QStringLiteral("Today"), {},
                         goal > 0 ? QStringLiteral("%1／%2").arg(CountText(read), CountText(goal)) : CountText(read), {}, -1});
            const int streak = Streak(library.days, today);
            rows.append({QStringLiteral("Days in a row"), {}, QString::number(streak), {}, -1, streak == 0});
        }
        rows.append({QStringLiteral("Characters read"), QStringLiteral("EVERY APP TOGETHER"), CountText(library.chars), {}, -1});
        rows.append({QStringLiteral("Time spent reading"), TimeText(library.active_ms).toUpper(), PlayClock(library.active_ms), {}, -1});
        rows.append({QStringLiteral("Days with reading"), {}, QString::number(library.days.size()), {}, -1});
        rows.append({QStringLiteral("Games begun"), {}, QStringLiteral("%1／%2").arg(begun).arg(distinct.size()), {}, -1});
        for (const App& app : library.apps) {
            // Furoshiki's own lines by console ("Furoshiki · PlayStation"), not six "Furoshiki".
            const QString name = app.app.startsWith(QStringLiteral("reader-home-"))
                                     ? QStringLiteral("%1 · %2").arg(app.name, SystemLabel(library, app.system))
                                     : QString(app.name).remove(QRegularExpression(QStringLiteral(" Reader$"))); // the old apps' names
            rows.append({name, figures(app.chars, app.active_ms), CountText(app.chars), {}, -1});
        }
        QList<Game> most = distinct;
        std::stable_sort(most.begin(), most.end(), [](const Game& a, const Game& b) { return a.chars > b.chars; });
        for (int i = 0; i < std::min<int>(5, most.size()) && most[i].chars > 0; ++i) {
            rows.append({most[i].title, ShortSystem(library, most[i].system) + QStringLiteral("  ·  ") + figures(most[i].chars, most[i].active_ms),
                         CountText(most[i].chars), {}, -1});
        }
        break;
    }
    case Item::Readers:
        for (const App& app : ReaderApps()) {
            rows.append({app.name,
                         app.program_found ? QStringLiteral("ITS OWN MENU  ·  %1").arg(app.program)
                                           : QStringLiteral("NOT FOUND WHERE IT SAID IT WAS  ·  %1").arg(app.program),
                         QStringLiteral("%1 TO READ").arg(app.games.size()), {}, -1});
        }
        if (rows.isEmpty()) {
            rows.append({QStringLiteral("No app has reported its games yet"),
                         QStringLiteral("START EACH APP ONCE, THEN PRESS F5 HERE"), {}, {}, -1});
        }
        for (const QString& problem : library.problems) {
            rows.append({problem, {}, {}, {}, -1});
        }
        break;
    case Item::Consoles: {
        // The settings for playing (play_settings.h): for all consoles, or where one differs.
        const QString console = LayerConsole();
        const QString game = LayerGame();
        const QString layer_name = console_layer == 0 ? QStringLiteral("ALL CONSOLES") : consoles[console_layer - 1].name;
        rows.append({QStringLiteral("Settings for"), QStringLiteral("LEFT RIGHT  ALL CONSOLES, OR ONE WHERE IT DIFFERS"),
                     layer_name, {}, -1});
        rows.last().steps = true;
        if (!console.isEmpty()) {
            const QList<ConsoleGame> games = console_games.value(console);
            rows.append({QStringLiteral("Game"),
                         games.isEmpty() ? QStringLiteral("NO GAME OF IT FOUND YET")
                                         : QStringLiteral("LEFT RIGHT  EVERY GAME, OR ONE WHERE IT DIFFERS (%1 GAMES)").arg(games.size()),
                         game.isEmpty() ? QStringLiteral("EVERY GAME") : games[console_game - 1].title, {}, -1});
            rows.last().steps = true;
            const QStringList folders = play->Folders(console);
            rows.append({QStringLiteral("Game folders"),
                         (folders.isEmpty() ? QStringLiteral("ENTER  ADD A FOLDER TO LOOK FOR ITS GAMES IN")
                                            : QStringLiteral("ENTER  ADD ONE  ·  BACKSPACE  FORGET %1").arg(QDir::toNativeSeparators(folders.last())))
                             .toUpper(),
                         QString::number(folders.size()), {}, -1});
            const ConsoleSetup setup = console_setup ? console_setup(console) : ConsoleSetup{};
            rows.append({QStringLiteral("System files"),
                         QStringLiteral("%1  ·  ENTER  ADD FILES  ·  %2").arg(setup.status, QDir::toNativeSeparators(setup.system_folder)).toUpper(),
                         setup.ready ? QStringLiteral("READY") : QStringLiteral("MISSING"), {}, -1, setup.ready});
        }
        // The popup's look as one file, which the Android apps read and write too.
        rows.append({QStringLiteral("Export popup style"),
                     QStringLiteral("ENTER  THE LOOK, ITS SETTINGS AND YOUR CSS HERE, AS ONE FILE"), {}, {}, -1});
        rows.append({QStringLiteral("Import popup style"),
                     QStringLiteral("ENTER  A STYLE FILE, FROM HERE OR AN ANDROID APP, FOR %1").arg(layer_name), {}, {}, -1});
        for (const PlaySetting& setting : LayerSettings()) {
            const QString label = setting.label.isEmpty() ? CommonLabel(setting.key) : setting.label;
            QString meta;
            bool dim = false;
            QString value;
            if (!game.isEmpty()) {
                const PlaySettings::Resolved resolved = play->Value(setting, console, game);
                value = resolved.value;
                dim = resolved.from != PlaySettings::Layer::Game;
                meta = resolved.from == PlaySettings::Layer::Game ? QStringLiteral("ONLY FOR THIS GAME  ·  BACKSPACE  AS ITS CONSOLE HAS IT")
                                                                  : QStringLiteral("AS ITS CONSOLE HAS IT");
            } else if (console.isEmpty()) {
                const bool set = play->IsSet(setting.key);
                value = play->Value(setting, {}).value; // all consoles' layer, or the core's own
                meta = set ? QStringLiteral("FOR EVERY CONSOLE  ·  BACKSPACE  AS EACH CONSOLE HAS IT")
                           : QStringLiteral("AS EACH CONSOLE HAS IT");
                dim = !set;
            } else {
                const PlaySettings::Resolved resolved = play->Value(setting, console);
                value = resolved.value;
                dim = resolved.from != PlaySettings::Layer::Console;
                if (!setting.Common()) {
                    meta = resolved.from == PlaySettings::Layer::Console
                               ? QStringLiteral("%1'S OWN  ·  BACKSPACE  AS IT COMES").arg(layer_name)
                               : QStringLiteral("%1'S OWN  ·  AS IT COMES").arg(layer_name);
                } else if (resolved.from == PlaySettings::Layer::Console) {
                    meta = QStringLiteral("ONLY FOR %1  ·  BACKSPACE  SAME AS ALL CONSOLES").arg(layer_name);
                } else {
                    meta = QStringLiteral("SAME AS ALL CONSOLES");
                }
            }
            rows.append({label, meta, setting.Shown(value), {}, -1, dim});
            rows.last().steps = true;
        }
        break;
    }
    case Item::Settings:
        for (int i = 0; i < SettingCount; ++i) {
            const bool opened = more_open.contains(item.system);
            if (!SettingShown(i, item.system, opened)) {
                continue;
            }
            const SettingRow& setting = SettingRows[i];
            const QString name = QString::fromLatin1(setting.name);
            QString value;
            if (name == QStringLiteral("More")) {
                // How many stand behind it; opened, the way to put them away again.
                int behind = 0;
                for (int other = 0; other < SettingCount; ++other) {
                    behind += SettingRows[other].more && item.system == QLatin1String(SettingRows[other].section) ? 1 : 0;
                }
                rows.append({opened ? QStringLiteral("Fewer") : QStringLiteral("More"), {},
                             opened ? QString() : QStringLiteral("+%1").arg(behind), {}, i, true});
                continue;
            }
            if (qstrcmp(setting.section, "Keys") == 0) {
                rows.append({name, {}, QString::fromLatin1(setting.about), {}, i});
                continue;
            }
            if (name == QStringLiteral("Daily goal")) {
                value = DailyGoal() > 0 ? CountText(DailyGoal()) : QStringLiteral("OFF");
            } else if (setting.key != nullptr) {
                value = setting.step != 0 ? QStringLiteral("%1%").arg(std::lround(Slide(i, 0) * 100))
                                          : (Slide(i, 0) != 0 ? QStringLiteral("ON") : QStringLiteral("OFF"));
            } else if (name == QStringLiteral("Look")) {
                value = look->theme.Name().toUpper();
            } else if (name == QStringLiteral("Icon")) {
                value = IconLabel(ChosenIcon()).toUpper();
            } else if (name == QStringLiteral("Fuzz")) {
                value = FuzzLevel().toUpper();
            } else if (name == QStringLiteral("Moving cursor")) {
                value = motion ? QStringLiteral("ON") : QStringLiteral("OFF");
            } else if (name == QStringLiteral("Menu sounds")) {
                value = sound_on ? QStringLiteral("ON") : QStringLiteral("OFF");
            } else if (name == QStringLiteral("Wide picture")) {
                value = WideValue();
            } else if (name == QStringLiteral("Rename labels")) {
                value = renaming ? QStringLiteral("ON") : QStringLiteral("OFF");
            } else if (name == QStringLiteral("Copy my labels")) {
                LabelsLoad();
                value = labels.isEmpty() ? QStringLiteral("NONE CHANGED") : QStringLiteral("%1 CHANGED").arg(labels.size());
            } else if (name == QStringLiteral("Full screen")) {
                value = full_screen ? QStringLiteral("ON") : QStringLiteral("OFF");
            } else if (name == QStringLiteral("Games start in a window")) {
                value = games_windowed ? QStringLiteral("ON") : QStringLiteral("OFF");
            } else if (name == QStringLiteral("Save sync")) {
                value = save_sync_state ? save_sync_state() : QStringLiteral("NONE");
            } else if (name == QStringLiteral("Lines address")) {
                value = Lines::Load(SharedFolder()).address;
            } else if (name == QStringLiteral("OCR helper")) {
                const QString address = Lines::Load(SharedFolder()).ocr.address;
                value = address.isEmpty() ? QStringLiteral("NOT SET") : address;
            } else if (setting_value) {
                value = setting_value(name);
            }
            QString meta = QString::fromLatin1(setting.about).toUpper();
            if (name == QStringLiteral("Look")) {
                meta = look->theme.Text(QStringLiteral("about")).toUpper();
            }
            if (setting.key != nullptr && qstrcmp(setting.key, "sensitivity") == 0) {
                meta += QStringLiteral("  ·  ") + pad.Describe().toUpper();
            }
            // Set by the user: a switch or slider of the pads that is not as it comes, a row of
            // the program's that has a value kept.
            const bool yours = setting.key != nullptr ? std::abs(Slide(i, 0) - setting.normal) > 1e-6
                                                      : (setting.host == 1 || setting.host == 3) && setting_is_set && setting_is_set(name);
            rows.append({name, meta, value, {}, i, false, yours});
            rows.last().steps = setting.host == 1 || setting.host == 3 || (setting.key != nullptr && setting.step != 0) ||
                                name == QStringLiteral("Look") || name == QStringLiteral("Icon") || name == QStringLiteral("Fuzz") ||
                                name == QStringLiteral("Daily goal") || name == QStringLiteral("Wide picture");
        }
        break;
    }
    return rows;
}

int& HomeWindow::RowChoice(const Item& item) {
    return row_choice[item.label];
}

void HomeWindow::Clamp() {
    const QList<Item> items = Items();
    axis_choice = std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1);
    int& row = RowChoice(items[axis_choice]);
    row = std::clamp(row, 0, std::max(0, static_cast<int>(Rows(items[axis_choice]).size()) - 1));
}

const App* HomeWindow::AppOf(const QString& app) const {
    for (const App& candidate : library.apps) {
        if (candidate.app == app) {
            return &candidate;
        }
    }
    return nullptr;
}

QList<App> HomeWindow::ReaderApps() const {
    QList<App> out;
    for (const App& app : library.apps) {
        if (!app.app.startsWith(QStringLiteral("reader-home-"))) {
            out.append(app);
        }
    }
    return out;
}

QPixmap HomeWindow::Cover(const QString& path) {
    if (path.isEmpty()) {
        return {};
    }
    // The Icon row's card shows the choice itself.
    if (path.startsWith(IconCover)) {
        return QPixmap::fromImage(IconImage(path.mid(IconCover.size()), 512));
    }
    const auto found = covers.constFind(path);
    if (found != covers.constEnd()) {
        return *found;
    }
    QPixmap picture(path);
    // Kept at about the size a large screen shows it (the slot is 370x252 on the sheet).
    if (!picture.isNull() && (picture.width() > 1024 || picture.height() > 1024)) {
        picture = picture.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    covers.insert(path, picture);
    return picture;
}

QPointF HomeWindow::ToSheet(const QPointF& point) const {
    const double scale = std::min(width() / SheetW, height() / SheetH);
    return {(point.x() - (width() - SheetW * scale) / 2) / scale, (point.y() - (height() - SheetH * scale) / 2) / scale};
}

int HomeWindow::DailyGoal() const {
    return std::clamp(QSettings().value(QStringLiteral("stats/goal"), 0).toInt(), 0, GoalMost);
}

void HomeWindow::SetDailyGoal(int characters) {
    QSettings().setValue(QStringLiteral("stats/goal"), std::clamp(characters / GoalStep * GoalStep, 0, GoalMost));
}

double HomeWindow::Slide(int row, int steps) {
    const SettingRow& setting = SettingRows[row];
    if (setting.key == nullptr) {
        return 0;
    }
    const QJsonObject steam = shared.value(QStringLiteral("steam")).toObject();
    if (setting.step == 0) { // a switch
        bool on = steam.value(QLatin1String(setting.key)).toBool(setting.normal != 0);
        if (steps != 0) {
            on = !on;
            SetSlide(row, on ? 1 : 0);
        }
        return on ? 1 : 0;
    }
    double value = steam.value(QLatin1String(setting.key)).toDouble(setting.normal);
    if (steps != 0) {
        value = std::clamp(std::round((value + steps * setting.step) / setting.step) * setting.step, setting.low, setting.high);
        SetSlide(row, value);
    }
    return value;
}

void HomeWindow::SetSlide(int row, double value) {
    const SettingRow& setting = SettingRows[row];
    QJsonObject steam = shared.value(QStringLiteral("steam")).toObject();
    if (setting.step == 0) {
        steam.insert(QLatin1String(setting.key), value != 0);
    } else {
        steam.insert(QLatin1String(setting.key), std::clamp(value, setting.low, setting.high));
    }
    shared.insert(QStringLiteral("steam"), steam);
    pad.SetTuning(TuningOf(steam)); // felt here at once
    if (!SaveShared(SharedFolder(), shared)) {
        Say(QStringLiteral("The settings could not be written to %1").arg(SharedFolder()));
    }
}

QString HomeWindow::LayerConsole() const {
    return console_layer > 0 && console_layer <= consoles.size() ? consoles[console_layer - 1].key : QString();
}

QList<PlaySetting> HomeWindow::LayerSettings() const {
    QList<PlaySetting> out;
    if (console_layer > 0 && console_layer <= consoles.size()) {
        for (const PlaySetting& setting : consoles[console_layer - 1].settings) {
            if (!setting.Hidden()) {
                out.append(setting);
            }
        }
        return out;
    }
    // All consoles: every common setting some core understands, once (the first core's
    // description of it).
    QStringList keys;
    for (const PlayConsole& console : consoles) {
        for (const PlaySetting& setting : console.settings) {
            if (setting.Common() && !setting.Hidden() && !keys.contains(setting.key)) {
                keys.append(setting.key);
                out.append(setting);
            }
        }
    }
    return out;
}

void HomeWindow::ChangeConsoleSetting(int row, int steps, bool clear) {
    if (play == nullptr) {
        return;
    }
    if (row == 0) {
        if (steps == 0) {
            return;
        }
        const int n = static_cast<int>(consoles.size()) + 1;
        console_layer = ((console_layer + steps) % n + n) % n;
        console_game = 0;
        sounds.Play(Sound::Move);
        Changed(PartCard | PartList);
        return;
    }
    if (row == 1 && console_layer > 0) {
        const int n = static_cast<int>(console_games.value(LayerConsole()).size()) + 1;
        if (steps == 0 || n == 1) {
            return;
        }
        console_game = ((console_game + steps) % n + n) % n;
        sounds.Play(Sound::Move);
        Changed(PartCard | PartList);
        return;
    }
    if (row < ConsoleHeadRows()) {
        return; // the folders and the system files take Enter and Backspace
    }
    const QList<PlaySetting> settings = LayerSettings();
    const int index = row - ConsoleHeadRows();
    if (index >= settings.size()) {
        return;
    }
    const PlaySetting& setting = settings[index];
    const QString console = LayerConsole();
    const QString game = LayerGame();
    if (clear) {
        if (!play->IsSet(setting.key, console, game)) {
            sounds.Play(Sound::Refuse);
            return;
        }
        play->Clear(setting.key, console, game);
        sounds.Play(Sound::Select);
    } else {
        const QString now = play->Value(setting, console, game).value;
        const QString next = setting.Moved(now, steps);
        if (next == now && play->IsSet(setting.key, console, game)) {
            return;
        }
        play->Set(setting.key, next, console, game);
        sounds.Play(Sound::Move);
    }
    if (!play->Save()) {
        Say(QStringLiteral("The settings could not be written to %1").arg(play->File()));
    }
    Changed(PartCard | PartList);
}

QString HomeWindow::LayerGame() const {
    const QList<ConsoleGame> games = console_games.value(LayerConsole());
    return console_game > 0 && console_game <= games.size() ? games[console_game - 1].id : QString();
}

void HomeWindow::ConsoleHeadAction(int row, bool remove) {
    if (row == PopupHeadRow() || row == PopupHeadRow() + 1) {
        const auto& action = row == PopupHeadRow() ? popup_export : popup_import;
        if (remove || !action) {
            return;
        }
        const QString said = action(LayerConsole(), LayerGame());
        if (!said.isEmpty()) {
            sounds.Play(Sound::Select);
            Say(said);
        }
        Changed();
        return;
    }
    const QString console = LayerConsole();
    if (console.isEmpty() || play == nullptr) {
        return;
    }
    if (row == 2) {
        // Game folders: one more (a folder picker), or the last forgotten.
        QStringList folders = play->Folders(console);
        if (remove) {
            if (folders.isEmpty()) {
                sounds.Play(Sound::Refuse);
                return;
            }
            folders.removeLast();
        } else {
            const QString chosen = QFileDialog::getExistingDirectory(
                this, QStringLiteral("A folder of %1 games").arg(consoles[console_layer - 1].name),
                folders.isEmpty() ? QDir::homePath() : folders.last());
            if (chosen.isEmpty()) {
                return;
            }
            folders.removeAll(chosen);
            folders.append(chosen);
        }
        play->SetFolders(console, folders);
        if (!play->Save()) {
            Say(QStringLiteral("The settings could not be written to %1").arg(play->File()));
        }
        sounds.Play(Sound::Select);
        if (console_folders_changed) {
            console_folders_changed(console);
        }
        Changed();
    } else if (row == 3 && !remove && console_setup) {
        // System files: the BIOS, firmware or keys the user has, copied where the core looks.
        const ConsoleSetup setup = console_setup(console);
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("%1: BIOS, firmware or keys").arg(consoles[console_layer - 1].name), QDir::homePath());
        int copied = 0;
        QDir().mkpath(setup.system_folder);
        for (const QString& file : files) {
            const QString target = setup.system_folder + QLatin1Char('/') + QFileInfo(file).fileName();
            if (QFileInfo::exists(target)) {
                Say(QStringLiteral("%1 is there already").arg(QFileInfo(file).fileName()));
                continue;
            }
            copied += QFile::copy(file, target) ? 1 : 0;
        }
        if (copied > 0) {
            sounds.Play(Sound::Select);
            Say(QStringLiteral("%1 file(s) put in %2").arg(copied).arg(QDir::toNativeSeparators(setup.system_folder)));
        }
        Changed();
    }
}

QString HomeWindow::LabelsFile() const {
    return SharedFolder() + QStringLiteral("/program/labels.json");
}

void HomeWindow::LabelsLoad() {
    if (labels_read) {
        return;
    }
    labels_read = true;
    QFile file(LabelsFile());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
        for (auto each = all.constBegin(); each != all.constEnd(); ++each) {
            if (!each.value().toString().trimmed().isEmpty()) {
                labels.insert(each.key(), each.value().toString());
            }
        }
    }
}

void HomeWindow::LabelsSave() const {
    QJsonObject all;
    for (auto each = labels.cbegin(); each != labels.cend(); ++each) {
        all.insert(each.key(), each.value());
    }
    QDir().mkpath(QFileInfo(LabelsFile()).absolutePath());
    QSaveFile file(LabelsFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

QString HomeWindow::Labelled(const QString& page, const QString& name) {
    LabelsLoad();
    const auto own = labels.constFind(page + QLatin1Char('/') + name);
    return own != labels.constEnd() ? own.value() : OwnLabel(page, name);
}

void HomeWindow::Rename(const QString& page, const QString& name) {
    LabelsLoad();
    bool ok = false;
    const QString by_itself = OwnLabel(page, name);
    const QString typed = QInputDialog::getText(this, QStringLiteral("Rename"),
                                                QStringLiteral("What \"%1\" is to be called (empty: as it comes)").arg(by_itself), QLineEdit::Normal,
                                                Labelled(page, name), &ok).trimmed();
    if (!ok) {
        return;
    }
    const QString key = page + QLatin1Char('/') + name;
    if (typed.isEmpty() || typed == by_itself) {
        labels.remove(key);
    } else {
        labels.insert(key, typed);
    }
    LabelsSave();
    sounds.Play(Sound::Select);
    update();
}

namespace {
/// The wide shapes, in the row's order; a console without one of them is given "off" for it.
const QStringList WideShapes{QStringLiteral("off"), QStringLiteral("16:9"), QStringLiteral("21:9"), QStringLiteral("32:9")};

const PlaySetting* WideOf(const PlayConsole& console) {
    for (const PlaySetting& setting : console.settings) {
        if (setting.key.endsWith(QStringLiteral(".wide"))) {
            return &setting;
        }
    }
    return nullptr;
}
} // namespace

QString HomeWindow::WideValue() const {
    // The widest any console is set to: what was chosen here (a console without that shape is off).
    int widest = -1;
    for (const PlayConsole& console : consoles) {
        if (const PlaySetting* wide = play != nullptr ? WideOf(console) : nullptr) {
            widest = std::max(widest, std::max<int>(0, WideShapes.indexOf(play->Value(*wide, console.key).value)));
        }
    }
    return widest < 0 ? QStringLiteral("NO CORE HAS IT") : WideShapes[widest].toUpper();
}

void HomeWindow::WideStep(int steps) {
    const QString now = WideValue().toLower();
    if (play == nullptr || !WideShapes.contains(now)) {
        sounds.Play(Sound::Refuse);
        return;
    }
    const int count = static_cast<int>(WideShapes.size());
    const QString next = WideShapes[((WideShapes.indexOf(now) + steps) % count + count) % count];
    QStringList given;
    for (const PlayConsole& console : consoles) {
        if (const PlaySetting* wide = WideOf(console)) {
            const bool has = wide->choices.contains(next);
            play->Set(wide->key, has ? next : QStringLiteral("off"), console.key);
            if (has && next != QStringLiteral("off")) {
                given.append(console.name);
            }
        }
    }
    if (!play->Save()) {
        Say(QStringLiteral("The settings could not be written to %1").arg(play->File()));
    } else if (next == QStringLiteral("off")) {
        Say(QStringLiteral("Games are drawn in their own shape"));
    } else {
        Say(QStringLiteral("%1 for %2, where a game has a checked recipe; from the next game started").arg(next, given.isEmpty() ? QStringLiteral("no console") : given.join(QStringLiteral(" and "))));
    }
    sounds.Play(Sound::Move);
    Changed();
}

void HomeWindow::Say(const QString& text) {
    notice = text;
    notice_until = clock.elapsed() + 5000;
    Changed(PartTabs);
}

void HomeWindow::Step() {
    if (notice_until != 0 && clock.elapsed() > notice_until) {
        notice_until = 0;
        notice.clear();
        Changed(PartTabs);
    }
    // The controller is this window's while it is the one in use and nothing it started runs.
    pad.SetActive(running.isEmpty() && isActiveWindow());
    if (shelf_on) {
        ShelfStep();
        return;
    }
    bool again = false;
    const qint64 second = QDateTime::currentSecsSinceEpoch(); // the clock's
    if (second != last_second) {
        last_second = second;
        if (!running.isEmpty()) {
            Changed(); // the clock stands under the veil
        }
        again = true; // its own piece
    }
    // On another row the orbs close in on its title and spring out again, from where they are.
    const QList<Item> items = Items();
    const Item& item = items[std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1)];
    const QString key = QStringLiteral("%1\n%2").arg(item.label).arg(row_choice.value(item.label));
    if (key != chosen_key) {
        chosen_key = key;
        orb_from = motion ? std::clamp(OrbSpread(orb_clock.elapsed() / 1000.0, orb_from), 0.0, 1.3) : 1;
        orb_clock.restart();
    }
    if (gliding || !fade_from.isNull()) {
        again = true;
    } else if (motion && label.shown && isVisible() && !isMinimized() && !playing_game && running.isEmpty()) {
        again = true; // the orbs (not for a window put away: sixty frames a second that nobody sees)
    }
    if (again && isVisible()) {
        update();
    }
}

void HomeWindow::Bump(int direction) {
    // The bar leans past the end a little and comes back, as a list's edge gives.
    if (!label.shown || SystemWantsStillness()) {
        return;
    }
    if (!gliding) {
        bar_at = bar_to;
        width_at = width_to;
    }
    bar_at = std::clamp(bar_at + BumpRows * direction, bar_to - 0.3, bar_to + 0.3);
    gliding = true;
    frame_clock.restart(); // its first frame a frame's step on, not the time since the last
    update();
}

void HomeWindow::Key(int key) {
    if (!running.isEmpty() || playing_game) {
        return;
    }
    if (key == Qt::Key_F3) {
        // The two pages while Furoshiki 3 is being made: the shelf, and the save menu.
        SetShelf(!shelf_on, true);
        sounds.Play(Sound::Tab);
        return;
    }
    if (shelf_on && !shelf_passing && ShelfKey(key)) {
        return;
    }
    HoverNone(); // the keys have it now: no second mark stays where the pointer was left
    const QList<Item> items = Items();
    const Item& item = items[axis_choice];
    int& row = RowChoice(item);
    const int rows = Rows(item).size();
    // On a settings page: which row of the settings' table the one in hand is.
    const int srow = item.kind == Item::Settings ? SettingAt(item.system, more_open.contains(item.system), row) : -1;
    int along = 0;
    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
        // On a slider or a switch of the settings the two change it; elsewhere they go along
        // the line (which Q and E, a controller's bumpers, always do).
        if (item.kind == Item::Consoles) {
            ChangeConsoleSetting(row, key == Qt::Key_Left ? -1 : 1);
            return;
        }
        if (srow >= 0 && (SettingRows[srow].host == 1 || SettingRows[srow].host == 3)) {
            if (setting_step) {
                setting_step(QString::fromLatin1(SettingRows[srow].name), key == Qt::Key_Left ? -1 : 1);
                sounds.Play(Sound::Move);
                Changed();
            }
            return;
        }
        if (IsSetting(srow, "Wide picture")) {
            WideStep(key == Qt::Key_Left ? -1 : 1);
            return;
        }
        if (IsSetting(srow, "Daily goal")) {
            SetDailyGoal(DailyGoal() + (key == Qt::Key_Left ? -GoalStep : GoalStep));
            sounds.Play(Sound::Move);
            Changed();
            return;
        }
        if (IsSetting(srow, "Icon")) {
            const QList<IconColours>& choices = IconChoices();
            int index = 0;
            while (index < choices.size() && choices[index].key != ChosenIcon()) {
                ++index;
            }
            ChooseIcon(choices[(index + (key == Qt::Key_Left ? -1 : 1) + choices.size()) % choices.size()].key);
            sounds.Play(Sound::Move);
            Changed();
            return;
        }
        if ((IsSetting(srow, "Look") || IsSetting(srow, "Fuzz"))) {
            const int step = key == Qt::Key_Left ? -1 : 1;
            if (IsSetting(srow, "Look")) {
                SetTheme(theme_index + step, true);
            } else {
                static const QStringList levels{QStringLiteral("off"), QStringLiteral("light"), QStringLiteral("heavy")};
                SetFuzz(levels[(levels.indexOf(FuzzLevel()) + step + 3) % 3], true);
            }
            sounds.Play(Sound::Move);
            return;
        }
        if (srow >= 0 && SettingRows[srow].key != nullptr) {
            const double before = Slide(srow, 0);
            if (Slide(srow, key == Qt::Key_Left ? -1 : 1) != before) {
                sounds.Play(Sound::Move);
                Changed(PartCard | PartList);
            }
            return;
        }
        along = key == Qt::Key_Left ? -1 : 1;
        break;
    case Qt::Key_Q:
    case Qt::Key_PageUp:
        along = -1;
        break;
    case Qt::Key_E:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
        along = 1;
        break;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int next = std::clamp(row + (key == Qt::Key_Up ? -1 : 1), 0, std::max(0, rows - 1));
        if (next != row) {
            row = next;
            sounds.Play(Sound::Move);
            Changed(PartCard | PartList);
        } else if (rows > 0) {
            Bump(key == Qt::Key_Up ? -1 : 1);
        }
        return;
    }
    case Qt::Key_Escape:
        // Back: to the first row of the page.
        if (row != 0) {
            row = 0;
            sounds.Play(Sound::Back);
            Changed(PartCard | PartList);
        }
        return;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        // A setting the user set, back to what it is by itself.
        if (srow >= 0 && SettingRows[srow].key != nullptr) {
            SetSlide(srow, SettingRows[srow].normal);
            sounds.Play(Sound::Back);
            Changed(PartCard | PartList);
            return;
        }
        if (srow >= 0 && (SettingRows[srow].host == 1 || SettingRows[srow].host == 3) && setting_reset) {
            setting_reset(QString::fromLatin1(SettingRows[srow].name));
            sounds.Play(Sound::Back);
            Changed();
            return;
        }
        // CONSOLES: a setting back to the next layer's (all consoles', or the core's own).
        if (item.kind == Item::Consoles && row >= ConsoleHeadRows()) {
            ChangeConsoleSetting(row, 0, true);
        } else if (item.kind == Item::Consoles && row > 0) {
            ConsoleHeadAction(row, true);
        }
        return;
    case Qt::Key_F2:
        if (open_settings) {
            open_settings(QString());
        }
        return;
    case Qt::Key_F5:
        ReloadKeepingChoice();
        sounds.Play(Sound::Tab);
        Say(QStringLiteral("Library refreshed"));
        return;
    case Qt::Key_F11:
        full_screen = !full_screen;
        QSettings().setValue(QStringLiteral("full_screen"), full_screen);
        window_forced = false; // asked for here: --window no longer holds
        ShowAsSet();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Accept();
        return;
    default:
        return;
    }
    const int next = std::clamp(axis_choice + along, 0, static_cast<int>(items.size()) - 1);
    if (next != axis_choice) {
        axis_choice = next;
        sounds.Play(Sound::Tab);
        Changed();
    }
}

void HomeWindow::Accept() {
    const QList<Item> items = Items();
    const Item& item = items[axis_choice];
    const int row = RowChoice(item);
    if (item.kind == Item::System) {
        const QList<Game> games = GamesOf(item.system);
        if (row >= games.size()) {
            sounds.Play(Sound::Refuse);
            return;
        }
        const Game& game = games[row];
        // Played here, in this window, where a core of this program plays that app's games.
        if (core_player && core_player(game.app, game.path)) {
            sounds.Play(Sound::Launch);
            return;
        }
        const App* app = AppOf(game.app);
        if (app == nullptr || !app->program_found) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("%1 is not where it said it was: start it once by hand").arg(app ? app->name : game.app));
            return;
        }
        if (NeedsFile(*app) && !QFileInfo::exists(game.path)) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("The game's file is not there: %1").arg(game.path));
            return;
        }
        Launch(*app, GameArguments(*app, game, games_windowed), game.title);
    } else if (item.kind == Item::Readers) {
        const QList<App> apps = ReaderApps();
        if (row >= apps.size()) {
            return;
        }
        const App& app = apps[row];
        if (!app.program_found) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("%1 is not where it said it was: start it once by hand").arg(app.name));
            return;
        }
        Launch(app, app.menu_args, app.name);
    } else if (item.kind == Item::Consoles) {
        // Enter flips a switch and goes on to a choice's next; a number has its arrows.
        const QList<PlaySetting> settings = LayerSettings();
        const int index = row - ConsoleHeadRows();
        if (row == 0 || (row == 1 && console_layer > 0)) {
            ChangeConsoleSetting(row, 1);
        } else if (row < ConsoleHeadRows()) {
            ConsoleHeadAction(row, false);
        } else if (index < settings.size() && settings[index].kind != PlaySetting::Number) {
            ChangeConsoleSetting(row, 1);
        }
    } else if (const int srow = item.kind == Item::Settings ? SettingAt(item.system, more_open.contains(item.system), row) : -1; srow >= 0) {
        QSettings settings;
        const QString name = QString::fromLatin1(SettingRows[srow].name);
        if (qstrcmp(SettingRows[srow].section, "Keys") == 0) {
            return; // said, not set
        }
        if (name == QStringLiteral("More")) {
            // The rest of the page's settings come, or go again; the row stays in hand.
            if (!more_open.remove(item.system)) {
                more_open.insert(item.system);
            }
            sounds.Play(Sound::Select);
        } else if (SettingRows[srow].host == 2) {
            // The program's own windows (Anki, the dictionaries, the popup...).
            if (setting_step) {
                sounds.Play(Sound::Select);
                setting_step(name, 0);
            } else {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("That is in the program that plays the games itself"));
            }
        } else if (SettingRows[srow].host == 3) {
            // A number: the arrows change it.
        } else if (SettingRows[srow].key != nullptr) {
            if (SettingRows[srow].step == 0) {
                Slide(srow, 1);
                sounds.Play(Sound::Select);
            }
        } else if (name == QStringLiteral("Wide picture")) {
            WideStep(1);
            return;
        } else if (name == QStringLiteral("Rename labels")) {
            renaming = !renaming;
            sounds.Play(Sound::Select);
            Say(renaming ? QStringLiteral("Enter or a click on any row gives it another label") : QStringLiteral("Labels are no longer renamed"));
        } else if (name == QStringLiteral("Copy my labels")) {
            LabelsLoad();
            QJsonObject all;
            for (auto each = labels.cbegin(); each != labels.cend(); ++each) {
                all.insert(each.key(), each.value());
            }
            QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Indented)));
            sounds.Play(Sound::Select);
            Say(labels.isEmpty() ? QStringLiteral("No label has been changed yet") : QStringLiteral("Copied %1: paste them to whoever makes the next build").arg(labels.size()));
        } else if (name == QStringLiteral("Open the labels file")) {
            LabelsLoad();
            LabelsSave(); // there, even if empty
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(LabelsFile()).absolutePath()));
            sounds.Play(Sound::Select);
        } else if (name == QStringLiteral("Put every label back")) {
            LabelsLoad();
            const int count = static_cast<int>(labels.size());
            labels.clear();
            LabelsSave();
            sounds.Play(Sound::Back);
            Say(QStringLiteral("%1 put back").arg(count == 1 ? QStringLiteral("1 label") : QStringLiteral("%1 labels").arg(count)));
        } else if (name == QStringLiteral("Look") || name == QStringLiteral("Icon") || name == QStringLiteral("Fuzz")
                   || name == QStringLiteral("Daily goal") || SettingRows[srow].host == 1) {
            Key(Qt::Key_Right);
            return;
        } else if (name == QStringLiteral("Moving cursor")) {
            SetMotion(!motion, true);
            sounds.Play(Sound::Select);
        } else if (name == QStringLiteral("Menu sounds")) {
            sound_on = !sound_on;
            settings.setValue(QStringLiteral("sounds"), sound_on);
            sounds.SetEnabled(sound_on);
            sounds.Play(Sound::Select);
        } else if (name == QStringLiteral("Full screen")) {
            Key(Qt::Key_F11);
        } else if (name == QStringLiteral("Games start in a window")) {
            games_windowed = !games_windowed;
            settings.setValue(QStringLiteral("games_windowed"), games_windowed);
            sounds.Play(Sound::Select);
        } else if (name == QStringLiteral("Save sync")) {
            if (save_sync_open) {
                sounds.Play(Sound::Select);
                save_sync_open();
            } else {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("Save sync is in the program that plays the games itself"));
            }
        } else if (name == QStringLiteral("Lines address")) {
            Lines::Sources sources = Lines::Load(SharedFolder());
            bool ok = false;
            const QString address = QInputDialog::getText(this, QStringLiteral("Lines address"), QStringLiteral("The WebSocket lines come from"),
                                                          QLineEdit::Normal, sources.address, &ok).trimmed();
            if (ok && !address.isEmpty()) {
                SetLinesAddress(address);
            }
        } else if (name == QStringLiteral("Add a title") || name == QStringLiteral("OCR helper")) {
            // The shelf's own (wizard_page.inc); from the save menu, the shelf comes with it.
            if (!shelf_on) {
                SetShelf(true, false);
            }
            OpenWizard(name == QStringLiteral("OCR helper"));
        } else if (name == QStringLiteral("Pair a device") || name == QStringLiteral("RetroAchievements")) {
            if (open_settings) {
                sounds.Play(Sound::Select);
                open_settings(name == QStringLiteral("Pair a device")       ? QStringLiteral("Save sync")
                                                                            : QStringLiteral("Achievements"));
            } else {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("That is in the program that plays the games itself"));
            }
        } else if (name == QStringLiteral("Exit")) {
            window()->close();
        } else {
            Key(Qt::Key_F5);
        }
        Changed(PartCard | PartList);
    }
}

void HomeWindow::Launch(const App& app, const QStringList& given, const QString& what) {
    sounds.Play(Sound::Launch);
    // In a window: the apps' own word for full screen is left out of their arguments.
    QStringList arguments = given;
    if (games_windowed) {
        arguments = WithoutFullScreen(app, arguments);
    }
    if (dry_run) {
        std::printf("start %s", qPrintable(app.program));
        for (const QString& argument : arguments) {
            std::printf(" [%s]", qPrintable(argument));
        }
        std::printf("\n");
        std::fflush(stdout);
        Say(QStringLiteral("Would start %1").arg(what));
        Changed();
        return;
    }
    running = what;
    pad.SetActive(false); // the app plays the controller itself
    Changed();
    child.setProgram(app.program);
    child.setArguments(arguments);
    child.setWorkingDirectory(QFileInfo(app.program).absolutePath());
    // The window goes once the sound has had its moment, and comes back when the app ends.
    QTimer::singleShot(650, this, [this] {
        if (running.isEmpty()) {
            return;
        }
        // Hidden first: a program that cannot be started is reported from within start(), and
        // the window shown again there must stay shown.
        window()->hide();
        child.start();
        if (running.isEmpty()) {
            return;
        }
        if (games_windowed) {
            place_tries = 80; // some twenty seconds for its window to come up
            QTimer::singleShot(250, this, &HomeWindow::PlaceChild);
        }
    });
}

#ifdef _WIN32
namespace {
struct Found {
    DWORD process;
    HWND window;
    long area;
};
BOOL CALLBACK EachWindow(HWND window, LPARAM parameter) {
    auto* found = reinterpret_cast<Found*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != found->process || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    RECT box{};
    GetWindowRect(window, &box);
    const long area = (box.right - box.left) * (box.bottom - box.top);
    if (area > found->area) {
        found->area = area;
        found->window = window;
    }
    return TRUE;
}
} // namespace
#endif

void HomeWindow::PlaceChild() {
#ifdef _WIN32
    if (running.isEmpty() || child.state() == QProcess::NotRunning || place_tries <= 0) {
        return;
    }
    --place_tries;
    Found found{static_cast<DWORD>(child.processId()), nullptr, 200 * 150};
    EnumWindows(&EachWindow, reinterpret_cast<LPARAM>(&found));
    if (found.window == nullptr) {
        QTimer::singleShot(250, this, &HomeWindow::PlaceChild);
        return;
    }
    // The screen it came up on, without the task bar.
    MONITORINFO screen{};
    screen.cbSize = sizeof(screen);
    GetMonitorInfoW(MonitorFromWindow(found.window, MONITOR_DEFAULTTONEAREST), &screen);
    const long room_w = screen.rcWork.right - screen.rcWork.left, room_h = screen.rcWork.bottom - screen.rcWork.top;
    const long h = room_h * 96 / 100;
    const long w = std::min(room_w * 96 / 100, h * 19 / 10);
    if (IsZoomed(found.window) || IsIconic(found.window)) {
        ShowWindow(found.window, SW_RESTORE);
    }
    SetWindowPos(found.window, nullptr, screen.rcWork.left + (room_w - w) / 2, screen.rcWork.top + (room_h - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetForegroundWindow(found.window);
    // Once more a moment later: an app puts its window where it was last time after showing it.
    if (place_tries > 2) {
        place_tries = 2;
        QTimer::singleShot(900, this, &HomeWindow::PlaceChild);
    }
#endif
}

void HomeWindow::Finished() {
    if (running.isEmpty()) {
        return;
    }
    running.clear();
    ReloadKeepingChoice();
    ShowAsSet();
    window()->raise();
    window()->activateWindow();
    sounds.Play(Sound::Return);
    Changed();
}

void HomeWindow::keyPressEvent(QKeyEvent* event) {
    if (TraceKeys()) {
        qInfo("key from the keyboard: %s (virtual key %u, scan code %u%s)", qPrintable(QKeySequence(event->key()).toString()),
              event->nativeVirtualKey(), event->nativeScanCode(), event->isAutoRepeat() ? ", repeated" : "");
    }
    if (TraceFrames()) {
        Trace().Key();
        QElapsedTimer took;
        took.start();
        if (!WizardText(event->key(), event->matches(QKeySequence::Paste) ? QGuiApplication::clipboard()->text() : event->text())) {
            Key(event->key());
        }
        qInfo("key %s%s %.1f ms", qPrintable(QKeySequence(event->key()).toString()), event->isAutoRepeat() ? " (repeated)" : "",
              took.nsecsElapsed() / 1e6);
        return;
    }
    // A line of the wizard being typed on takes what types; every other key is the page's.
    if (!WizardText(event->key(), event->matches(QKeySequence::Paste) ? QGuiApplication::clipboard()->text() : event->text())) {
        Key(event->key());
    }
}

void HomeWindow::mousePressEvent(QMouseEvent* event) {
    if (!running.isEmpty() || playing_game) {
        return;
    }
    if (event->button() == Qt::BackButton || (event->button() == Qt::RightButton && shelf_on && (shelf_pages || shelf_stage || shelf_log || wizard_on))) {
        Key(Qt::Key_Escape);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF at = ToSheet(event->position());
    if (shelf_on) {
        ShelfClick(at);
        return;
    }
    const QList<Item> items = Items();
    for (const Hit& hit : hits) {
        if (!hit.area.contains(at)) {
            continue;
        }
        if (hit.kind == 0) {
            if (hit.index != axis_choice) {
                axis_choice = hit.index;
                sounds.Play(Sound::Tab);
                Changed();
            }
        } else if (hit.kind == 2) {
            // The slider follows the pointer while the button is held.
            dragging = hit.index;
            mouseMoveEvent(event);
        } else {
            // A click chooses a row, a second one takes it.
            int& row = RowChoice(items[axis_choice]);
            if (row == hit.index) {
                Accept();
            } else {
                row = hit.index;
                sounds.Play(Sound::Move);
                Changed(PartCard | PartList);
            }
        }
        return;
    }
}

void HomeWindow::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = ToSheet(event->position());
    if (shelf_on && wizard_on) {
        if (event->buttons() & Qt::LeftButton) {
            WizardDrag(at, false);
        }
        ShelfHover(at, event->source() == Qt::MouseEventNotSynthesized);
        return;
    }
    if (shelf_on && pages_drag != 0 && (event->buttons() & Qt::LeftButton)) {
        PagesDrag(at);
        return;
    }
    if (shelf_on) {
        ShelfHover(at, event->source() == Qt::MouseEventNotSynthesized);
        return;
    }
    if (dragging >= 0 && (event->buttons() & Qt::LeftButton)) {
        for (const Hit& hit : hits) {
            if (hit.kind == 2 && hit.index == dragging) {
                const SettingRow& setting = SettingRows[dragging];
                const double share = std::clamp((at.x() - hit.area.left()) / hit.area.width(), 0.0, 1.0);
                const double before = Slide(dragging, 0);
                SetSlide(dragging, std::round((setting.low + share * (setting.high - setting.low)) / setting.step) * setting.step);
                if (Slide(dragging, 0) != before) {
                    sounds.Play(Sound::Move);
                    Changed(PartCard | PartList);
                }
                break;
            }
        }
        return;
    }
    // A finger's tap comes as a mouse's too, and leaves no pointer standing over a row.
    Hover(at, event->source() == Qt::MouseEventNotSynthesized);
}

void HomeWindow::leaveEvent(QEvent* event) {
    HoverNone();
    if (shelf_hover_kind >= 0) {
        shelf_hover_kind = shelf_hover_index = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void HomeWindow::HoverNone() {
    if (hover_row >= 0) {
        hover_row = -1;
        Changed(PartList);
    }
    if (hover_tab >= 0) {
        hover_tab = -1;
        Changed(PartTabs);
    }
}

void HomeWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (shelf_on && wizard_on && event->button() == Qt::LeftButton) {
        WizardDrag(ToSheet(event->position()), true);
    }
    if (event->button() == Qt::LeftButton) {
        dragging = -1;
        if (pages_drag != 0) {
            pages_drag = 0;
            update();
        }
    }
}

void HomeWindow::Hover(const QPointF& at, bool lights) {
    // A hand over what a click takes, as a link has it; and the row or tab under it lit a
    // little (a row: a faint glow of the cursor's behind its title; a tab: its text no longer
    // dim), so that the mouse shows what it would choose before it does.
    bool over = false;
    int row = -1, tab = -1;
    const bool idle = running.isEmpty() && !playing_game;
    for (const Hit& hit : hits) {
        if (!hit.area.contains(at)) {
            continue;
        }
        over = true;
        if (hit.kind == 1) {
            row = hit.index;
        } else if (hit.kind == 0) {
            tab = hit.index;
        }
    }
    const Qt::CursorShape shape = over && idle ? Qt::PointingHandCursor : Qt::ArrowCursor;
    if (cursor().shape() != shape) {
        setCursor(shape);
    }
    if (!idle || !lights) {
        row = tab = -1;
    }
    if (row != hover_row) {
        hover_row = row;
        Changed(PartList);
    }
    if (tab != hover_tab) {
        hover_tab = tab;
        Changed(PartTabs);
    }
}

void HomeWindow::wheelEvent(QWheelEvent* event) {
    // A row for each notch (120); a touchpad's small turns add up to one. A sideways swipe
    // (more across than down) goes along the line, a page for a longer turn.
    const QPoint turn = event->angleDelta();
    if (shelf_on) {
        // The shelf: a title for each notch, either way the wheel or the pad is turned; with Shift
        // held, a shelf (the lift) for each.
        const int by = std::abs(turn.y()) >= std::abs(turn.x()) ? turn.y() : turn.x();
        if (event->modifiers() & Qt::ShiftModifier) {
            wheel_side += by;
            const int shelves = static_cast<int>(wheel_side / 120);
            if (shelves != 0 && running.isEmpty() && !playing_game && !shelf_stage && !shelf_pages && !wizard_on) {
                wheel_side -= shelves * 120;
                ShelfKey(shelves > 0 ? Qt::Key_Up : Qt::Key_Down);
            }
            return;
        }
        wheel_side += by;
        if (shelf_pages && !wizard_on) {
            // The pages: the list of rows goes, by a touchpad's small turns too.
            const int rows = static_cast<int>(wheel_side / 40);
            wheel_side -= rows * 40;
            if (rows != 0 && find_text.isNull()) {
                pages_first -= rows; // held to the list's ends where it is drawn
                update();
            }
            return;
        }
        const int steps = static_cast<int>(wheel_side / 120);
        if (steps != 0 && running.isEmpty() && !playing_game) {
            wheel_side -= steps * 120;
            if (wizard_on) {
                ShelfKey(steps > 0 ? Qt::Key_Up : Qt::Key_Down); // the wizard's rows
            } else if (shelf_log) {
                for (int i = 0; i < std::abs(steps) * 2; ++i) {
                    ShelfKey(steps > 0 ? Qt::Key_Up : Qt::Key_Down); // the Log's lines
                }
            } else {
                ShelfMove(-steps);
            }
        }
        return;
    }
    if (TraceFrames()) {
        Trace().Key();
        qInfo("wheel %d,%d (pixels %d,%d, phase %d, from %d%s), %d left over", turn.x(), turn.y(), event->pixelDelta().x(),
              event->pixelDelta().y(), static_cast<int>(event->phase()), static_cast<int>(event->source()),
              event->inverted() ? ", inverted" : "", wheel_rest);
    }
    if (std::abs(turn.x()) > std::abs(turn.y())) {
        wheel_side += turn.x();
        while (std::abs(wheel_side) >= 240) {
            Key(wheel_side > 0 ? Qt::Key_Q : Qt::Key_E);
            wheel_side += wheel_side > 0 ? -240 : 240;
        }
        return;
    }
    wheel_rest += turn.y();
    while (std::abs(wheel_rest) >= 120) {
        Key(wheel_rest > 0 ? Qt::Key_Up : Qt::Key_Down);
        wheel_rest += wheel_rest > 0 ? -120 : 120;
    }
}

namespace {

Fill Solid(const QColor& colour) {
    Fill fill;
    Layer layer;
    layer.colour = colour;
    fill.layers.append(layer);
    return fill;
}

/// A shape with square left corners and round right ones (a divot in a rail).
QPainterPath RightRounded(const QRectF& box, double radius) {
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);
    QPainterPath square;
    square.addRect(QRectF(box.left(), box.top(), box.width() - radius, box.height()));
    return path.united(square).simplified();
}

QPainterPath Rounded(const QRectF& box, double radius) {
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);
    return path;
}

/// The page's name in the tab, and on the tabs along the foot.
QString PageTitle(int kind) {
    switch (kind) {
    case 1:
        return QStringLiteral("Statistics");
    case 2:
        return QStringLiteral("Readers");
    case 3:
        return QStringLiteral("Consoles");
    case 4:
        return QStringLiteral("Settings");
    default:
        return QStringLiteral("Library");
    }
}
QString PageTab(int kind) {
    switch (kind) {
    case 1:
        return QStringLiteral("統計");
    case 2:
        return QStringLiteral("リーダー");
    case 3:
        return QStringLiteral("本体");
    default:
        return QStringLiteral("設定");
    }
}

/// Words broken into lines of a width (at spaces, or anywhere in text without them).
QStringList Wrap(const QString& text, const QFont& font, double width, int most) {
    const QFontMetricsF metrics(font);
    QStringList lines;
    QString line;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& word : words) {
        const QString tried = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
        if (metrics.horizontalAdvance(tried) <= width || line.isEmpty()) {
            line = tried;
        } else {
            lines.append(line);
            line = word;
        }
    }
    if (!line.isEmpty()) {
        lines.append(line);
    }
    if (lines.size() > most) {
        lines = lines.mid(0, most);
        lines.last() = metrics.elidedText(lines.last() + QStringLiteral(" …"), Qt::ElideRight, width);
    }
    for (QString& each : lines) {
        each = metrics.elidedText(each, Qt::ElideRight, width);
    }
    return lines;
}

} // namespace

HomeWindow::Card HomeWindow::CardOf(const Item& item, const QList<Row>& rows, int chosen) {
    Card card;
    const int n = chosen + 1, total = static_cast<int>(rows.size());
    const auto file = [this](int number, const QString& what) { return look->file_text.arg(number).arg(what); };
    if (rows.isEmpty() || chosen >= total) {
        card.file = QStringLiteral("NO FILE");
        card.title = item.kind == Item::System ? SystemLabel(library, item.system) : PageTitle(item.kind);
        card.system = item.system;
        return card;
    }
    const Row& row = rows[chosen];
    switch (item.kind) {
    case Item::System: {
        const QList<Game> games = GamesOf(item.system);
        const Game& game = games[std::min(chosen, static_cast<int>(games.size()) - 1)];
        card.file = file(n, game.id);
        card.title = game.title;
        card.sub = SystemLabel(library, item.system) + QStringLiteral("  %1／%2").arg(n).arg(total);
        card.stats = {{QStringLiteral("Characters"), CountText(game.chars)},
                      {QStringLiteral("Play Time"), PlayClock(game.active_ms)},
                      {QStringLiteral("Last Read"), DayText(game.last_day, true)}};
        card.cover = game.cover;
        card.system = item.system;
        break;
    }
    case Item::Readers:
        if (const QList<App> apps = ReaderApps(); chosen < apps.size()) {
            const App& app = apps[chosen];
            card.file = file(n, QString(app.program).replace(QLatin1Char('\\'), QLatin1Char('/')).section(QLatin1Char('/'), -1));
            card.title = app.name;
            card.sub = SystemLabel(library, app.system) + QStringLiteral("  %1／%2").arg(n).arg(apps.size());
            card.stats = {{QStringLiteral("Games"), QString::number(app.games.size())},
                          {QStringLiteral("Characters"), CountText(app.chars)},
                          {QStringLiteral("Play Time"), PlayClock(app.active_ms)}};
            card.system = app.system;
        } else {
            card.file = file(n, QStringLiteral("NOTE"));
            card.title = row.title;
            card.note = row.meta;
        }
        break;
    case Item::Statistics:
        card.file = file(n, QStringLiteral("STATISTICS"));
        card.title = row.title;
        card.sub = row.value;
        card.stats = {{QStringLiteral("Characters"), CountText(library.chars)},
                      {QStringLiteral("Play Time"), PlayClock(library.active_ms)},
                      {QStringLiteral("Days Read"), QString::number(library.days.size())}};
        card.picture = Card::Chart;
        break;
    case Item::Consoles:
    case Item::Settings:
        card.file = file(n, item.kind == Item::Consoles ? QStringLiteral("CONSOLES") : QStringLiteral("SETTINGS"));
        card.title = row.title;
        card.sub = PageTab(item.kind) + QStringLiteral("  %1／%2").arg(n).arg(total);
        card.note = row.meta;
        card.picture = Card::Value;
        if (item.kind == Item::Settings && row.setting >= 0 && IsSetting(row.setting, "Icon")) {
            card.picture = Card::Art;
            card.cover = IconCover + ChosenIcon();
        }
        card.sub_dim = row.dim;
        if (row.setting >= 0 && SettingRows[row.setting].step != 0) {
            card.slider = row.setting;
        }
        break;
    }
    return card;
}

void HomeWindow::DrawGround(double scale) {
    const QString key = QStringLiteral("%1|%2").arg(look->theme.Key()).arg(scale);
    if (key == ground_key && !ground.isNull()) {
        return;
    }
    ground_key = key;
    const QSize size(static_cast<int>(std::lround(SheetW * scale)), static_cast<int>(std::lround(SheetH * scale)));
    ground = QImage(size, QImage::Format_ARGB32_Premultiplied);
    ground.fill(look->page);
    QPainter p;
    const auto begin = [&] {
        p.begin(&ground);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        p.scale(scale, scale);
    };
    begin();
    Paint::PaintFill(p, QRectF(0, 0, SheetW, SheetH), look->bg);
    Paint::PaintBox(p, QRectF(TabLeft, TabTop, TabWidth, look->tab_height), look->tab_bg, look->tab_shadow,
                    {Border, Border, Border, Border, look->frame});
    const Fill divot = Solid(look->divot);
    const auto panel = [&](const QRectF& box, const QList<Shadow>& shadows, const QColor& frame, const Fill& rail, bool accent) {
        // What is behind a panel is seen through it a little blurred (backdrop-filter).
        if (look->panel_blur > 0) {
            p.end();
            const double sigma = look->panel_blur * scale;
            const int margin = static_cast<int>(std::ceil(sigma * 3)) + 1;
            const QRect area = QRectF(box.left() * scale, box.top() * scale, box.width() * scale, box.height() * scale)
                                   .toAlignedRect()
                                   .adjusted(-margin, -margin, margin, margin)
                                   .intersected(ground.rect());
            QImage behind = ground.copy(area);
            Paint::Blur(behind, sigma);
            begin();
            p.save();
            p.setClipRect(box);
            p.drawImage(QRectF(area.x() / scale, area.y() / scale, area.width() / scale, area.height() / scale), behind);
            p.restore();
        }
        Paint::PaintBox(p, box, look->panel_bg, shadows, {Border, 0, Border, 0, frame});
        // The thicker sides: a rail of two tones with a divot near each end, and on the list
        // the accent in its groove, mirrored on the right.
        for (int side = 0; side < 2; ++side) {
            p.save();
            if (side == 0) {
                p.translate(box.left(), 0);
            } else {
                p.translate(box.right(), 0);
                p.scale(-1, 1);
            }
            const QRectF railbox(0, box.top() + Border, RailWidth, box.height() - 2 * Border);
            Paint::PaintFill(p, railbox, rail);
            for (const double y : {railbox.top() + DivotInset, railbox.bottom() - DivotInset - DivotHeight}) {
                Paint::PaintShape(p, RightRounded(QRectF(0, y, DivotWidth, DivotHeight), 3), divot, look->divot_sh);
            }
            if (accent) {
                const QRectF groove(GrooveLeft, railbox.top() + railbox.height() * GrooveEnds, GrooveWidth,
                                    railbox.height() * (1 - 2 * GrooveEnds));
                Paint::PaintShape(p, Rounded(groove, 3), look->groove, look->glow);
            }
            p.restore();
        }
    };
    panel(QRectF(PanelLeft, CardTop, PanelRight - PanelLeft, CardHeight), look->card_shadow, look->card_frame, look->card_rail,
          false);
    const QRectF list(PanelLeft, ListTop, PanelRight - PanelLeft, ListHeight);
    panel(list, look->panel_shadow, look->frame, look->rail, true);
    // The list's two dividers: after the numbers, before the dates.
    const Fill line = Solid(look->line);
    for (const double x : {list.left() + look->vline_left, list.right() - look->vline_right - 2}) {
        Paint::PaintBox(p, QRectF(x, list.top() + Border, 2, list.height() - 2 * Border), line, look->line_sh, {});
    }
}

QRectF HomeWindow::ClockArea(const QString& time) const {
    // The clock and room round it for its shadows (and its box's).
    const double time_width = QFontMetricsF(look->clock).horizontalAdvance(time);
    const double room = 16;
    if (look->clock_box) {
        const double edge = look->clock_border;
        const double w = time_width + 2 * (look->clock_pad_x + edge), h = Paint::NormalLine(look->clock) + 2 * (look->clock_pad_y + edge);
        return QRectF(PanelRight - w, look->clock_top, w, h).adjusted(-room, -room, room, room);
    }
    return QRectF(PanelRight - time_width, look->clock_top, time_width, Paint::NormalLine(look->clock)).adjusted(-room, -room, room, room);
}

void HomeWindow::DrawClock(QPainter& p, const QString& time) const {
    const double time_width = QFontMetricsF(look->clock).horizontalAdvance(time);
    if (look->clock_box) {
        const double edge = look->clock_border;
        const QRectF box(PanelRight - time_width - 2 * (look->clock_pad_x + edge), look->clock_top, time_width + 2 * (look->clock_pad_x + edge),
                         Paint::NormalLine(look->clock) + 2 * (look->clock_pad_y + edge));
        Paint::PaintBox(p, box, look->clock_bg, look->clock_shadow, {edge, edge, edge, edge, look->clock_border_colour});
        Paint::PutText(p, look->clock,
                        QPointF(box.left() + edge + look->clock_pad_x, Paint::BoxBaseline(look->clock, box.top() + edge + look->clock_pad_y)),
                        time, look->clock_colour, look->clock_text_sh);
    } else {
        Paint::PutText(p, look->clock, QPointF(PanelRight, Paint::BoxBaseline(look->clock, look->clock_top)), time, look->clock_colour,
                        look->clock_text_sh, 1);
    }
}

void HomeWindow::DrawScene(double scale) {
    scene_dirty = false;
    list_waits = false;
    dirty_parts = 0;
    pieces_stale = true;
    scene_scale = scale;
    DrawGround(scale);
    scene = ground.copy();
    hits.clear();
    label = {};
    QPainter p(&scene);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.scale(scale, scale);
    const QList<Item> items = Items();
    const Item& item = items[axis_choice];
    const QList<Row> rows = Rows(item);
    const int chosen = RowChoice(item);

    // The tab's name. The clock is a piece of its own (it changes every second), but under the
    // veil while an app starts.
    QString head = PageTitle(item.kind);
    if (look->head_upper) {
        head = head.toUpper();
    }
    Paint::PutText(p, look->head,
                    QPointF(TabLeft + TabWidth / 2, Paint::CentredBaseline(look->head, TabTop + look->tab_height / 2)), head,
                    look->tab_txt, look->tab_sh, 2);

    DrawCard(p, item, rows, chosen);
    DrawList(p, item, rows, chosen);
    DrawTabs(p, items);

    if (!running.isEmpty()) {
        DrawClock(p, QDateTime::currentDateTime().toString(look->clock_format));
        // An app is starting: the page steps back behind a card with its name.
        label = {};
        QColor veil = look->page;
        veil.setAlphaF(0.86);
        p.fillRect(QRectF(0, 0, SheetW, SheetH), veil);
        const QRectF box(240, 262, 800, 196);
        Paint::PaintBox(p, box, look->panel_bg, look->card_shadow, {Border, 0, Border, 0, look->card_frame});
        Paint::PutText(p, look->title, QPointF(box.center().x(), Paint::CentredBaseline(look->title, box.center().y() - 16)),
                        Paint::Fit(running, look->title, box.width() - 80), look->txt, look->txt_sh, 2);
        Paint::PutText(p, look->file, QPointF(box.center().x(), Paint::CentredBaseline(look->file, box.center().y() + 44)),
                        QStringLiteral("STARTING"), look->txt, look->txt_sh, 2);
    }
    p.end();
    if (FuzzLevel() != QStringLiteral("off")) {
        scene_fuzzed = scene.copy();
        Paint::ApplyFuzz(scene_fuzzed, look->Fuzz(FuzzLevel()), scale, QPoint());
    } else {
        scene_fuzzed = QImage();
    }
}

QRectF HomeWindow::PartArea(int part) {
    // Inside the panels' rails and borders (the ground's), and the band along the foot.
    switch (part) {
    case PartCard:
        return {PanelLeft + RailWidth, CardTop + Border, PanelRight - PanelLeft - 2 * RailWidth, CardHeight - 2 * Border};
    case PartList:
        return {PanelLeft + RailWidth, ListTop + Border, PanelRight - PanelLeft - 2 * RailWidth, ListHeight - 2 * Border};
    default:
        return {0, FootTop - 8, SheetW, SheetH - FootTop + 8};
    }
}

void HomeWindow::DrawParts(int parts) {
    dirty_parts = 0;
    if ((parts & PartList) != 0) {
        list_waits = false;
    }
    pieces_stale = true;
    const double scale = scene_scale;
    const QList<Item> items = Items();
    const Item& item = items[axis_choice];
    const QList<Row> rows = Rows(item);
    const int chosen = RowChoice(item);
    const QRect canvas(QPoint(), scene.size());
    QList<QRect> drawn;
    for (const int part : {PartCard, PartList, PartTabs}) {
        if ((parts & part) == 0) {
            continue;
        }
        const QRectF sheet = PartArea(part);
        const QRect area = QRectF(sheet.x() * scale, sheet.y() * scale, sheet.width() * scale, sheet.height() * scale)
                               .toAlignedRect()
                               .intersected(canvas);
        // What was found there goes with what is drawn again.
        const int kind = part == PartTabs ? 0 : part == PartList ? 1 : 2;
        hits.erase(std::remove_if(hits.begin(), hits.end(), [kind](const Hit& hit) { return hit.kind == kind; }), hits.end());
        QPainter p(&scene);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(area.topLeft(), ground, area);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setClipRect(area);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        p.scale(scale, scale);
        if (part == PartCard) {
            DrawCard(p, item, rows, chosen);
        } else if (part == PartList) {
            label = {};
            DrawList(p, item, rows, chosen);
        } else {
            DrawTabs(p, items);
        }
        drawn.append(area);
    }
    // The fuzz again where it was drawn, from a little round it so that it lines up.
    if (scene_fuzzed.isNull()) {
        return;
    }
    const Paint::FuzzSpec spec = look->Fuzz(FuzzLevel());
    const int margin = static_cast<int>(std::ceil((spec.level.shift + 3 * spec.level.blur) * scale)) + 2;
    QPainter q(&scene_fuzzed);
    q.setCompositionMode(QPainter::CompositionMode_Source);
    for (const QRect& area : drawn) {
        const QRect outer = area.adjusted(-margin, -margin, margin, margin).intersected(canvas);
        QImage piece = scene.copy(outer);
        Paint::ApplyFuzz(piece, spec, scale, outer.topLeft());
        q.drawImage(area.topLeft(), piece, QRect(area.topLeft() - outer.topLeft(), area.size()));
    }
}

HomeWindow::Piece HomeWindow::MakePiece(const QImage& base, const QRectF& area, const std::function<void(QPainter&)>& draw) const {
    const double scale = scene_scale;
    const QString level = FuzzLevel();
    const bool fuzz = level != QStringLiteral("off");
    const Paint::FuzzSpec spec = look->Fuzz(level);
    const int margin = fuzz ? static_cast<int>(std::ceil((spec.level.shift + 3 * spec.level.blur) * scale)) + 2 : 0;
    const QRect canvas(QPoint(), base.size());
    const QRect inner = QRectF(area.x() * scale, area.y() * scale, area.width() * scale, area.height() * scale)
                            .toAlignedRect()
                            .intersected(canvas);
    if (inner.isEmpty()) {
        return {};
    }
    const QRect outer = inner.adjusted(-margin, -margin, margin, margin).intersected(canvas);
    QImage piece = base.copy(outer);
    {
        QPainter q(&piece);
        q.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        q.translate(-outer.topLeft());
        q.scale(scale, scale);
        draw(q);
    }
    if (fuzz) {
        Paint::ApplyFuzz(piece, spec, scale, outer.topLeft());
    }
    return {piece.copy(QRect(inner.topLeft() - outer.topLeft(), inner.size())), inner};
}

void HomeWindow::Compose(QPainter& p) const {
    // The scene as it was drawn (fuzzed then or not), whatever the fuzz is now.
    const bool fuzz = !scene_fuzzed.isNull();
    p.drawImage(QPoint(), fuzz ? scene_fuzzed : scene);
    for (const Piece* piece : {&clock_piece, &cursor_piece}) {
        if (!piece->image.isNull()) {
            p.drawImage(piece->area.topLeft(), piece->image);
        }
    }
    if (!fade_from.isNull()) {
        // What was shown, going: most of it in the first part of the time, so that the two are
        // seldom seen at once at half strength.
        const double t = std::clamp(fade_clock.elapsed() / 1000.0 / fade_length, 0.0, 1.0);
        p.save();
        p.setClipRect(fade_area, Qt::IntersectClip);
        p.setOpacity((1 - t) * (1 - t));
        p.drawImage(QPoint(), fade_from);
        p.restore();
    }
}

HomeWindow::Shown HomeWindow::Now() const {
    const QList<Item> items = Items();
    const Item& item = items[std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1)];
    return {item.label, row_choice.value(item.label), look->theme.Key(), FuzzLevel(), notice, console_layer, !running.isEmpty()};
}

void HomeWindow::DrawCard(QPainter& p, const Item& item, const QList<Row>& rows, int chosen) {
    const Card card = CardOf(item, rows, chosen);
    const Look& l = *look;
    const double left = PanelLeft + l.detail_pad, right = PanelRight - l.detail_pad;
    const double top = CardTop + Border + CardPadY, bottom = CardTop + CardHeight - Border - CardPadY;
    // The picture's box: the slot at the right, or a cover's own shape fitted in it (at the
    // right, in the middle of the slot's height), the frame round the picture itself.
    QRectF thumb(right - ThumbWidth, top, ThumbWidth, ThumbHeight);
    const QPixmap cover = card.picture == Card::Art ? Cover(card.cover) : QPixmap();
    if (!cover.isNull()) {
        const QSizeF fitted = QSizeF(cover.size()).scaled(thumb.size() - QSizeF(2 * ThumbBorder, 2 * ThumbBorder), Qt::KeepAspectRatio);
        const QSizeF framed = fitted + QSizeF(2 * ThumbBorder, 2 * ThumbBorder);
        thumb = QRectF(right - framed.width(), top + (ThumbHeight - framed.height()) / 2, framed.width(), framed.height());
    }
    const double width = thumb.left() - CardGap - left;
    double y = top;
    Paint::PutText(p, l.file, QPointF(left, Paint::BoxBaseline(l.file, y)), Paint::Fit(card.file, l.file, width), l.txt, l.txt_sh);
    y += Paint::NormalLine(l.file) + 2;
    Paint::PutText(p, l.title, QPointF(left, Paint::BoxBaseline(l.title, y, l.title_line)), Paint::Fit(card.title, l.title, width), l.txt,
                    l.txt_sh);
    y += l.title_line > 0 ? l.title_line : Paint::NormalLine(l.title);
    if (!card.sub.isEmpty()) {
        Paint::PutText(p, l.sub, QPointF(left + 36, Paint::BoxBaseline(l.sub, y, l.sub_line)), Paint::Fit(card.sub, l.sub, width - 36),
                        card.sub_dim ? l.dim : l.txt, card.sub_dim ? l.dim_sh : l.txt_sh);
    }
    // At the foot: three figures, or a few words.
    const double foot = bottom - 3 * l.stats_line;
    if (!card.stats.isEmpty()) {
        for (int i = 0; i < card.stats.size() && i < 3; ++i) {
            const double baseline = Paint::BoxBaseline(l.stats, foot + i * l.stats_line, l.stats_line);
            Paint::PutText(p, l.stats, QPointF(left, baseline), card.stats[i].first, l.txt, l.txt_sh);
            Paint::PutText(p, l.stats, QPointF(left + StatsLabel, baseline),
                            Paint::Fit(card.stats[i].second, l.stats, width - StatsLabel), l.txt, l.txt_sh);
        }
    } else if (!card.note.isEmpty()) {
        const QStringList lines = Wrap(card.note, l.note, width, 3);
        const double from = bottom - lines.size() * l.stats_line;
        for (int i = 0; i < lines.size(); ++i) {
            Paint::PutText(p, l.note, QPointF(left, Paint::BoxBaseline(l.note, from + i * l.stats_line, l.stats_line)), lines[i], l.txt,
                            l.txt_sh);
        }
    }

    // The picture in its frame: the shadows outside, what is in it, the inset line and border.
    QList<Shadow> outer, inset;
    for (const Shadow& shadow : l.thumb_shadow) {
        (shadow.inset ? inset : outer).append(shadow);
    }
    const Fill ground = card.picture == Card::Art ? SystemArt(card.system) : l.tab_bg;
    Paint::PaintBox(p, thumb, ground, outer, {});
    const QRectF inside = thumb.adjusted(ThumbBorder, ThumbBorder, -ThumbBorder, -ThumbBorder);
    p.save();
    p.setClipRect(inside);
    if (card.picture == Card::Art) {
        if (!cover.isNull()) {
            p.drawPixmap(inside, cover, QRectF(cover.rect()));
        }
    } else if (card.picture == Card::Chart) {
        // The last thirty days, a bar a day, today at the right in the accent.
        QMap<QString, qint64> by_day;
        for (const Day& day : library.days) {
            by_day.insert(day.day, day.chars);
        }
        const QDate today = QDate::currentDate();
        qint64 most = 1;
        for (int back = 0; back < 30; ++back) {
            most = std::max(most, by_day.value(today.addDays(-back).toString(Qt::ISODate)));
        }
        const QRectF plot = inside.adjusted(18, 22, -18, -48);
        p.fillRect(QRectF(plot.left(), plot.bottom(), plot.width(), 2), l.tab_txt);
        const double step = plot.width() / 30;
        const QColor accent = l.theme.Colour(QStringLiteral("accent"), l.tab_txt);
        for (int back = 0; back < 30; ++back) {
            const qint64 chars = by_day.value(today.addDays(-back).toString(Qt::ISODate));
            if (chars <= 0) {
                continue;
            }
            const double tall = std::max(3.0, plot.height() * chars / static_cast<double>(most));
            const QRectF bar(plot.right() - (back + 1) * step + 1.5, plot.bottom() - tall, step - 3, tall);
            if (!l.tab_sh.isEmpty()) {
                p.fillRect(bar.translated(l.tab_sh.first().dx, l.tab_sh.first().dy), l.tab_sh.first().colour);
            }
            p.fillRect(bar, back == 0 ? accent : l.tab_txt);
        }
        const double base = inside.bottom() - 16;
        Paint::PutText(p, l.note, QPointF(plot.left(), base), QStringLiteral("30 DAYS"), l.tab_txt, l.tab_sh);
        Paint::PutText(p, l.note, QPointF(plot.right(), base), QStringLiteral("MOST %1").arg(CountText(most > 1 ? most : 0)), l.tab_txt,
                        l.tab_sh, 1);
    } else {
        // A setting's value, large, and its slider under it.
        // A row with nothing to show (one that opens or does something) says so as the list
        // does for a game never read, in the dim colour: an empty box looked unfinished.
        const QString value = chosen < rows.size() ? rows[chosen].value : QString();
        const bool none = value.isEmpty();
        Paint::PutText(p, l.title, QPointF(inside.center().x(), Paint::CentredBaseline(l.title, inside.center().y() - 14)),
                        none ? QStringLiteral("- -") : Paint::Fit(value, l.title, inside.width() - 24),
                        card.sub_dim || none ? l.dim : l.tab_txt, none ? l.dim_sh : l.tab_sh, 2);
        if (card.slider >= 0) {
            const SettingRow& setting = SettingRows[card.slider];
            const QRectF groove(inside.left() + 36, inside.center().y() + 52, inside.width() - 72, 4);
            const double share = (Slide(card.slider, 0) - setting.low) / (setting.high - setting.low);
            const double normal = (setting.normal - setting.low) / (setting.high - setting.low);
            Paint::PaintShape(p, Rounded(groove, 2), Solid(l.theme.Colour(QStringLiteral("rail-lo"))), l.divot_sh);
            Paint::PaintShape(p, Rounded(QRectF(groove.left(), groove.top(), groove.width() * share, groove.height()), 2),
                              Solid(l.theme.Colour(QStringLiteral("accent"))), l.glow);
            p.fillRect(QRectF(groove.left() + groove.width() * normal - 1, groove.bottom() + 4, 2, 6), l.tab_txt);
            Paint::PaintShape(p, Rounded(QRectF(groove.left() + groove.width() * share - 5, groove.center().y() - 11, 10, 22), 3),
                              Solid(l.thumb_frame), l.divot_sh);
            hits.append({groove.adjusted(0, -16, 0, 16), 2, card.slider});
        }
    }
    p.restore();
    Paint::PaintBox(p, thumb, Fill{}, inset, {ThumbBorder, ThumbBorder, ThumbBorder, ThumbBorder, l.thumb_frame});
    if (l.thumb_outline_width > 0) {
        QPainterPath outline;
        outline.setFillRule(Qt::OddEvenFill);
        const double w = l.thumb_outline_width;
        outline.addRect(thumb.adjusted(-w, -w, w, w));
        outline.addRect(thumb);
        p.fillPath(outline, l.thumb_outline);
    }
}

void HomeWindow::DrawList(QPainter& p, const Item& item, const QList<Row>& rows, int chosen, double bar) {
    const Look& l = *look;
    const double left = PanelLeft + l.list_left, right = PanelRight - l.list_right;
    const int total = static_cast<int>(rows.size());
    const int first = total == 0 ? 0 : (std::clamp(chosen, 0, total - 1) / RowsShown) * RowsShown;
    const bool games = item.kind == Item::System;
    const bool glide = bar >= 0;
    // A theme without a bar marks the chosen row only with the glow and the orbs, its title in
    // the text's own colours (as the KID choice dialogs do).
    const bool barred = !l.bar.Empty();
    const bool cut = glide && barred;
    const double rows_top = ListTop + Border + ListPadY;
    // While it glides, the bar stands on the screen's pixels: the titles are cut along its
    // edges, the chosen colours inside it and their own outside, and the two must meet.
    QRectF bar_box;
    if (cut) {
        const double scale = std::sqrt(std::fabs(p.worldTransform().determinant()));
        const auto snap = [scale](double v) { return std::round(v * scale) / scale; };
        const double top = rows_top + bar * RowHeight + l.bar_inset;
        bar_box = QRectF(QPointF(snap(left + BarLeft), snap(top)), QPointF(snap(right - BarRight), snap(top + RowHeight - 2 * l.bar_inset)));
    }
    // One pass at rest; while gliding three: the rows outside the bar, the bar, the rows in it.
    for (int pass = 0; pass < (cut ? 3 : 1); ++pass) {
        if (pass == 1) {
            Paint::PaintFill(p, bar_box, l.bar);
            continue;
        }
        p.save();
        if (cut && pass == 0) {
            QPainterPath outside;
            outside.setFillRule(Qt::OddEvenFill);
            outside.addRect(QRectF(0, 0, SheetW, SheetH));
            outside.addRect(bar_box);
            p.setClipPath(outside, Qt::IntersectClip);
        } else if (cut) {
            p.setClipRect(bar_box, Qt::IntersectClip);
        }
        for (int slot = 0; slot < RowsShown; ++slot) {
            const int i = first + slot;
            const double top = rows_top + slot * RowHeight;
            const double centre = top + RowHeight / 2;
            if (l.row_rule.alpha() > 0 && pass == 0) {
                p.fillRect(QRectF(left, top + RowHeight - 1, right - left, 1), l.row_rule);
            }
            const bool filled = i < total;
            const bool on = filled && (glide ? pass == 2 : i == chosen);
            if (on && !glide && barred) {
                Paint::PaintFill(p, QRectF(left + BarLeft, top + l.bar_inset, right - BarRight - left - BarLeft, RowHeight - 2 * l.bar_inset),
                                 l.bar);
            }
            const QColor colour = on && barred ? l.sel : filled ? l.txt : l.dim;
            const QList<Shadow>& shadows = on && barred ? l.sel_sh : filled ? l.txt_sh : l.dim_sh;
            if (filled || games) {
                Paint::PutText(p, l.number, QPointF(left + NumberColumn / 2, Paint::CentredBaseline(l.number, centre)),
                                QString::number(i + 1), colour, shadows, 2);
            }
            const double title_x = left + NumberColumn + TitlePad;
            const double room = right - WhenColumn - title_x - OrbAside - 14;
            if (filled) {
                const QString title = Paint::Fit(rows[i].title, l.row, room);
                if (i == hover_row && i != chosen && pass == 0 && l.cursor_glow.isValid()) {
                    // Under the pointer: the cursor's glow, faint, where the cursor would stand.
                    const double line = Paint::NormalLine(l.row);
                    PaintGlow(p, l.cursor_glow, HoverGlow,
                              QRectF(title_x, centre - line / 2, QFontMetricsF(l.row).horizontalAdvance(title), line));
                }
                if (on && !glide) {
                    label = ListLabel(rows, i); // drawn over the scene with the orbs (paintEvent)
                } else {
                    Paint::PutText(p, l.row, QPointF(title_x, Paint::CentredBaseline(l.row, centre)), title, colour, shadows);
                }
                if (!rows[i].value.isEmpty() && pass == 0) {
                    // The date and play time (or a value) at the right; closer set if it is long.
                    const double x = right - WhenColumn + WhenPad, width = WhenColumn - WhenPad;
                    QFont face = l.when;
                    if (QFontMetricsF(face).horizontalAdvance(rows[i].value) > width) {
                        face.setLetterSpacing(QFont::AbsoluteSpacing, 0);
                    }
                    // The bar ends before this column: it keeps the text's own colour on the chosen row.
                    Paint::PutText(p, face, QPointF(x, Paint::CentredBaseline(face, centre)), Paint::Fit(rows[i].value, face, width),
                                    rows[i].dim ? l.dim : l.txt, rows[i].dim ? l.dim_sh : l.txt_sh);
                }
                if (!glide) {
                    hits.append({QRectF(left, top, right - left, RowHeight), 1, i});
                }
            } else if (games) {
                Paint::PutText(p, l.empty, QPointF(title_x, Paint::CentredBaseline(l.empty, centre)), QStringLiteral("No File"), l.dim, l.dim_sh);
            }
        }
        p.restore();
    }
    // More rows above or below these eight: a small arrow at the right.
    const auto arrow = [&](double y, bool up) {
        QPainterPath path;
        const double x = right - 10;
        path.moveTo(x - 6, up ? y + 4 : y - 4);
        path.lineTo(x + 6, up ? y + 4 : y - 4);
        path.lineTo(x, up ? y - 5 : y + 5);
        path.closeSubpath();
        for (int i = static_cast<int>(l.dim_sh.size()) - 1; i >= 0; --i) {
            p.fillPath(path.translated(l.dim_sh[i].dx, l.dim_sh[i].dy), l.dim_sh[i].colour);
        }
        p.fillPath(path, l.txt);
    };
    if (first > 0) {
        arrow(ListTop + Border + ListPadY + 8, true);
    }
    if (first + RowsShown < total) {
        arrow(ListTop + ListHeight - Border - ListPadY - 8, false);
    }
}

HomeWindow::Label HomeWindow::ListLabel(const QList<Row>& rows, int chosen) const {
    // DrawList's place and look of the chosen row's title.
    const Look& l = *look;
    if (chosen < 0 || chosen >= rows.size()) {
        return {};
    }
    const double left = PanelLeft + l.list_left, right = PanelRight - l.list_right;
    const double centre = ListTop + Border + ListPadY + (chosen % RowsShown) * RowHeight + RowHeight / 2;
    const double title_x = left + NumberColumn + TitlePad;
    const double room = right - WhenColumn - title_x - OrbAside - 14;
    const bool barred = !l.bar.Empty();
    const QString title = Paint::Fit(rows[chosen].title, l.row, room);
    const double line = Paint::NormalLine(l.row);
    return {true, QRectF(title_x, centre - line / 2, QFontMetricsF(l.row).horizontalAdvance(title), line), title, l.row,
            barred ? l.sel : l.txt, barred ? l.sel_sh : l.txt_sh, Paint::CentredBaseline(l.row, centre)};
}

void HomeWindow::DrawTabs(QPainter& p, const QList<Item>& items) {
    const Look& l = *look;
    const double centre = FootTop + FootHeight / 2;
    if (!notice.isEmpty()) {
        Paint::PutText(p, l.tab, QPointF(SheetW / 2, Paint::CentredBaseline(l.tab, centre)),
                        Paint::Fit(notice, l.tab, PanelRight - PanelLeft - 40), l.txt, l.txt_sh, 2);
        return;
    }
    QStringList labels;
    for (const Item& item : items) {
        labels.append(item.kind == Item::System ? SystemLabel(library, item.system) : PageTab(item.kind));
    }
    // The consoles, a little apart from the pages; all smaller if they do not fit.
    QFont face = l.tab;
    QFont lr = l.lr;
    double total = 0;
    QList<double> widths;
    for (int attempt = 0; attempt < 8; ++attempt) {
        widths.clear();
        total = QFontMetricsF(lr).horizontalAdvance(QStringLiteral("L")) + QFontMetricsF(lr).horizontalAdvance(QStringLiteral("R")) +
                FootGap * (items.size() + 1) + FootGap * 1.4;
        for (const QString& text : labels) {
            widths.append(QFontMetricsF(face).horizontalAdvance(text) + 2 * FootPadX);
            total += widths.last();
        }
        if (total <= PanelRight - PanelLeft || face.pixelSize() <= 14) {
            break;
        }
        face.setPixelSize(face.pixelSize() - 1);
        lr.setPixelSize(lr.pixelSize() - 1);
    }
    double x = SheetW / 2 - total / 2;
    x += Paint::PutText(p, lr, QPointF(x, Paint::CentredBaseline(lr, centre)), QStringLiteral("L"), l.dim, l.dim_sh) + FootGap;
    const double height = Paint::NormalLine(face) + 2 * FootPadY;
    for (int i = 0; i < items.size(); ++i) {
        if (i > 0 && items[i].kind != Item::System && items[i - 1].kind == Item::System) {
            x += FootGap * 1.4;
        }
        const QRectF box(x, centre - height / 2, widths[i], height);
        const bool on = i == axis_choice;
        if (on) {
            Paint::PaintShape(p, Rounded(box, 3), l.tab_on, l.tab_on_shadow);
        }
        const bool lit = !on && i == hover_tab;
        Paint::PutText(p, face, QPointF(box.left() + FootPadX, Paint::CentredBaseline(face, centre)), labels[i],
                        on ? l.sel : lit ? l.txt : l.dim, on ? l.sel_sh : lit ? l.txt_sh : l.dim_sh);
        hits.append({box.adjusted(-FootGap / 2, -4, FootGap / 2, 4), 0, i});
        x += widths[i] + FootGap;
    }
    Paint::PutText(p, lr, QPointF(x, Paint::CentredBaseline(lr, centre)), QStringLiteral("R"), l.dim, l.dim_sh);
}

void HomeWindow::DrawOrbs(QPainter& p, bool front, double seconds, const QRectF& box) const {
    if (!label.shown) {
        return;
    }
    const double scale = std::sqrt(std::fabs(p.worldTransform().determinant()));
    const double spread = motion ? OrbSpread(seconds, orb_from) : 1;
    const double spin = motion ? spin_clock.elapsed() / 1000.0 / OrbSpin : 0;
    for (int orb = 0; orb < 2; ++orb) {
        // Orb A (left) comes nearer as they close in and goes over the glyphs, orb B farther
        // and under them; at the sides both are level with the title, A in front.
        const double nearness = (orb == 0 ? 1 : -1) * OrbNear * std::clamp(1 - spread, 0.0, 1.0);
        if ((orb == 0 || nearness > -0.01) != front) {
            continue;
        }
        double bob = 0;
        if (motion) {
            // ±2 px, ease-in-out, there and back in 1.5 s each way; B a half beat behind A.
            const double phase = seconds / OrbBob + orb * 0.5;
            double t = phase - std::floor(phase);
            if (static_cast<long long>(std::floor(phase)) % 2 == 1) {
                t = 1 - t;
            }
            bob = -OrbBobPx + 2 * OrbBobPx * Bezier(0.42, 0, 0.58, 1, t);
        }
        // Over a long title they close in only `OrbReach` px and swing out as much less.
        const double side = orb == 0 ? -1 : 1;
        const double aside = box.width() / 2 + OrbAside;
        const QPointF centre(box.center().x() + side * (aside + std::min(aside, OrbReach) * (spread - 1)), box.center().y() + bob);
        const int frame = static_cast<int>(std::floor((spin + orb * 0.37) * Paint::OrbPhases));
        const QImage sprite = Paint::OrbSprite(look->orb_glow, scale, frame);
        const double size = Paint::OrbBox * (1 + nearness);
        p.save();
        p.setOpacity(nearness < 0 ? 1 + nearness * 2 : 1);
        p.drawImage(QRectF(centre.x() - size / 2, centre.y() - size / 2, size, size), sprite);
        p.restore();
    }
}

void HomeWindow::DrawGlow(QPainter& p, double seconds, const QRectF& box) const {
    if (!label.shown || !look->cursor_glow.isValid()) {
        return;
    }
    PaintGlow(p, look->cursor_glow, motion ? GlowAt(seconds) : GlowRest, box);
}

namespace {

void PaintGlowOval(QPainter& p, const QColor& colour, double strength, const QRectF& box, double wide, double tall) {
    const double rx = box.width() / 2 + wide, ry = box.height() * tall;
    const double alpha = colour.alphaF() * std::clamp(strength, 0.0, 1.0);
    // A soft oval, much the same strength over the title and fading out past its ends.
    QRadialGradient gradient(QPointF(0, 0), 1);
    const auto at = [&](double share) {
        QColor c = colour;
        c.setAlphaF(alpha * share);
        return c;
    };
    gradient.setColorAt(0, at(1));
    gradient.setColorAt(0.6, at(0.95));
    gradient.setColorAt(0.85, at(0.5));
    gradient.setColorAt(1, at(0));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(box.center());
    p.scale(rx, ry);
    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawEllipse(QPointF(0, 0), 1, 1);
    p.restore();
}

} // namespace

void HomeWindow::PaintGlow(QPainter& p, const QColor& colour, double strength, const QRectF& box) {
    PaintGlowOval(p, colour, strength, box, GlowWide, GlowTall);
}

void HomeWindow::paintEvent(QPaintEvent*) {
    QElapsedTimer took;
    took.start();
    if (shelf_on) {
        PaintShelf();
        Pace(took.nsecsElapsed() / 1e6);
        return;
    }
    const char* what = "";
    const double ratio = devicePixelRatioF();
    const double fit = std::min(width() / SheetW, height() / SheetH);
    const double scale = fit * ratio;
    const QSize size(static_cast<int>(std::lround(SheetW * scale)), static_cast<int>(std::lround(SheetH * scale)));
    const bool resized = scene.size() != size || scene_scale != scale;
    const bool smooth = !SystemWantsStillness();
    bool glide_starts = false;
    if (scene_dirty || resized || dirty_parts != 0) {
        // How the page moves on from what it showed: another page, theme or fuzz fades over
        // the whole; another row of the same eight glides the bar there and fades the card;
        // a message at the foot fades in and out there.
        const Shown now = Now();
        QList<int> fade_parts;
        double length = 0;
        bool fade_starts = false;
        bool list_later = false; // the list as it is, under the gliding cursor
        if (scene.isNull()) {
            // The first picture comes up out of the page's colour.
            gliding = false;
            fade_from = QImage(size, QImage::Format_ARGB32_Premultiplied);
            fade_from.fill(look->page);
            fade_area = fade_from.rect();
            fade_length = FadeOpen;
            fade_starts = true;
        } else if (resized) {
            gliding = false;
            fade_from = QImage();
        } else if (now.page != shown.page || now.theme != shown.theme || now.fuzz != shown.fuzz || now.running != shown.running) {
            fade_parts = {PartAll};
            length = FadePage;
            gliding = false;
        } else if (now.row != shown.row || now.layer != shown.layer) {
            const bool same_eight = now.row / RowsShown == shown.row / RowsShown && now.layer == shown.layer;
            if (same_eight && smooth && label.shown) {
                if (!gliding) {
                    bar_at = shown.row % RowsShown;
                    width_at = label.box.width();
                }
                gliding = glide_starts = true;
                fade_parts = {PartCard};
                // While the cursor glides its piece is the whole list, drawn every frame: the
                // scene's list under it is drawn once it stops, not on every row (a held key
                // would draw and fuzz it again many times a second for nothing).
                list_later = !scene_dirty && !resized;
            } else {
                fade_parts = {PartCard, PartList};
                gliding = false;
            }
            length = FadeCard;
        } else if (now.notice != shown.notice) {
            fade_parts = {PartTabs};
            length = FadeFoot;
        }
        if (!fade_parts.isEmpty()) {
            QRect area;
            for (const int part : fade_parts) {
                const QRectF sheet = part == PartAll ? QRectF(0, 0, SheetW, SheetH) : PartArea(part);
                area |= QRectF(sheet.x() * scene_scale, sheet.y() * scene_scale, sheet.width() * scene_scale, sheet.height() * scene_scale)
                            .toAlignedRect();
            }
            // The frame as it was shown (with any fade still going in it), where it is to fade.
            QImage before(scene.size(), QImage::Format_ARGB32_Premultiplied);
            {
                QPainter q(&before);
                q.setClipRect(area);
                Compose(q);
            }
            fade_from = before;
            fade_area = area;
            fade_length = length;
            fade_starts = true;
        }
        if (scene_dirty || resized) {
            DrawScene(scale);
            what = ", the whole page";
        } else {
            int parts = dirty_parts;
            if (list_later && (parts & PartList) != 0) {
                parts &= ~PartList;
                list_waits = true;
                const QList<Item> items = Items();
                const Item& item = items[axis_choice];
                label = ListLabel(Rows(item), RowChoice(item));
            }
            what = (parts & PartCard) != 0 ? ((parts & PartList) != 0 ? ", card and list" : ", card") : ", a part";
            DrawParts(parts);
        }
        shown = now;
        if (fade_starts) {
            fade_clock.restart(); // from the first frame it is seen in, not before the drawing
        }
        const int chosen = shown.row;
        bar_to = chosen % RowsShown;
        width_to = label.box.width();
        if (!gliding) {
            bar_at = bar_to;
            width_at = width_to;
        }
        const int generation = ++prewarm_generation;
        QTimer::singleShot(60, this, [this, generation] { Prewarm(generation, 0); });
    }
    // Time moves on: the bar nears its row (by the time since the last frame; a frame at the
    // start of a glide already moves), and a fade ends.
    double dt = std::clamp(frame_clock.isValid() ? frame_clock.restart() / 1000.0 : 0.0, 0.0, 0.05);
    frame_clock.start();
    if (glide_starts) {
        dt = 1.0 / 60;
    }
    if (gliding) {
        const double k = 1 - std::exp(-dt / GlideTime);
        bar_at += (bar_to - bar_at) * k;
        width_at += (width_to - width_at) * k;
        // Done within a pixel or so: the last of an exponential's way is too slow to see.
        if (std::fabs(bar_to - bar_at) < 0.02 && std::fabs(width_to - width_at) < 0.6) {
            gliding = false;
            bar_at = bar_to;
            width_at = width_to;
            pieces_stale = true; // the cursor at rest again
        }
    }
    if (!fade_from.isNull() && fade_clock.elapsed() / 1000.0 >= fade_length) {
        fade_from = QImage();
    }
    if (list_waits && !gliding) {
        DrawParts(PartList); // the cursor at rest: the list it glided over, as it now is
        what = ", the list at rest";
    }

    // The pieces over the scene: the clock, and the chosen title with its orbs (or, while the
    // bar glides, the list's rows with it).
    const QString time = QDateTime::currentDateTime().toString(look->clock_format);
    if (pieces_stale || time != clock_text) {
        clock_text = time;
        clock_piece = running.isEmpty() ? MakePiece(scene, ClockArea(time), [this, &time](QPainter& q) { DrawClock(q, time); }) : Piece{};
    }
    pieces_stale = false;
    const double seconds = orb_clock.elapsed() / 1000.0;
    if (gliding && label.shown) {
        const QList<Item> items = Items();
        const Item& item = items[axis_choice];
        const QList<Row> rows = Rows(item);
        const int chosen = RowChoice(item);
        const QRectF box(label.box.left(), ListTop + Border + ListPadY + bar_at * RowHeight + RowHeight / 2 - label.box.height() / 2, width_at,
                         label.box.height());
        const Label kept = label;
        const QList<Hit> kept_hits = hits;
        cursor_piece = MakePiece(ground, PartArea(PartList), [&](QPainter& q) {
            DrawGlow(q, seconds, box);
            DrawList(q, item, rows, chosen, bar_at);
            DrawOrbs(q, false, seconds, box);
            DrawOrbs(q, true, seconds, box);
        });
        label = kept;
        hits = kept_hits;
    } else if (label.shown) {
        // A band round the title, wide enough for the orbs' spring past the sides and the glow.
        const double past = std::min(label.box.width() / 2 + OrbAside, OrbReach) * 0.3; // the spring's swing out at most
        const double reach = std::max(OrbAside + past + Paint::OrbBox * (1 + OrbNear) / 2, GlowWide) + 6;
        const double rise = std::max(Paint::OrbBox * (1 + OrbNear) / 2 + OrbBobPx, label.box.height() * (GlowTall - 0.5)) + 4;
        cursor_piece = MakePiece(scene, label.box.adjusted(-reach, -rise, reach, rise), [&](QPainter& q) {
            DrawGlow(q, seconds, label.box);
            DrawOrbs(q, false, seconds, label.box);
            Paint::PutText(q, label.font, QPointF(label.box.left(), label.baseline), label.text, label.colour, label.shadows);
            DrawOrbs(q, true, seconds, label.box);
        });
    } else {
        cursor_piece = {};
    }

    QPainter p(this);
    p.fillRect(rect(), look->page);
    // On whole pixels of the screen: a sheet half a pixel off is drawn soft.
    p.scale(1 / ratio, 1 / ratio);
    p.translate(std::round((width() * ratio - size.width()) / 2), std::round((height() * ratio - size.height()) / 2));
    Compose(p);
    Pace(took.nsecsElapsed() / 1e6);
    if (TraceFrames()) {
        p.end();
        Trace().Frame(took.nsecsElapsed() / 1e6, *what != 0 ? what : gliding ? ", gliding" : "");
    }
}

void HomeWindow::Pace(double frame_ms) {
    // A frame as often as the screen shows one (sixty a second was all there was, on any screen:
    // on the Z13's, which shows more, the orbs and the glide moved in uneven steps), or every
    // second or third of the screen's when a frame takes longer to draw than that, with room
    // left for the keys: never more often than frames can be drawn, never less often than before.
    frame_cost += (std::min(frame_ms, 60.0) - frame_cost) * 0.12;
    // READER_HOME_HZ says the screen's rate instead (60 for the old pace; a test's stand-in screen).
    static const double said = qEnvironmentVariable("READER_HOME_HZ").toDouble();
    const QScreen* on = screen();
    double hertz = said > 0 ? said : on != nullptr ? on->refreshRate() : 60;
    if (hertz < 50 || hertz > 480) {
        hertz = 60;
    }
    const double period = 1000 / hertz;
    // Slower as soon as a frame needs it, faster only once there is room to spare (no flutter).
    const int slower = std::max(1, static_cast<int>(std::ceil(frame_cost * 1.4 / period)));
    const int faster = std::max(1, static_cast<int>(std::ceil(frame_cost * 1.9 / period)));
    if (slower > frame_skip) {
        frame_skip = slower;
    } else if (faster < frame_skip) {
        frame_skip = faster;
    }
    while (frame_skip > 1 && period * frame_skip > 17) {
        --frame_skip;
    }
    const int every = std::max(4, static_cast<int>(std::lround(period * frame_skip)));
    if (every != ticker.interval()) {
        ticker.setInterval(every);
    }
}

void HomeWindow::Prewarm(int generation, int step) {
    // One card or list a turn, while nothing moves, until a newer drawing starts it again.
    if (generation != prewarm_generation || scene.isNull() || !running.isEmpty() || playing_game) {
        return;
    }
    if (gliding || !fade_from.isNull()) {
        QTimer::singleShot(60, this, [this, generation, step] { Prewarm(generation, step); });
        return;
    }
    const QList<Item> items = Items();
    const Item& item = items[axis_choice];
    const int chosen = RowChoice(item);
    // The rows next to the chosen one, then the pages beside this one.
    struct Job {
        int item, row;
        bool list;
    };
    QList<Job> jobs;
    for (const int d : {1, -1, 2, -2}) {
        jobs.append({axis_choice, chosen + d, false});
    }
    for (const int d : {1, -1}) {
        const int other = axis_choice + d;
        if (other >= 0 && other < items.size()) {
            jobs.append({other, row_choice.value(items[other].label), false});
            jobs.append({other, row_choice.value(items[other].label), true});
        }
    }
    if (step >= jobs.size()) {
        return;
    }
    const Job job = jobs[step];
    const QList<Row> rows = Rows(items[job.item]);
    if (job.row >= 0 && job.row < rows.size()) {
        QImage scratch(1, 1, QImage::Format_ARGB32_Premultiplied);
        QPainter q(&scratch);
        q.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        q.scale(scene_scale, scene_scale);
        const Label kept = label;
        const QList<Hit> kept_hits = hits;
        if (job.list) {
            DrawList(q, items[job.item], rows, job.row);
        } else {
            DrawCard(q, items[job.item], rows, job.row);
        }
        label = kept;
        hits = kept_hits;
    }
    QTimer::singleShot(15, this, [this, generation, step] { Prewarm(generation, step + 1); });
}

void HomeWindow::RunScript(const QStringList& lines) {
    script = lines;
    QTimer::singleShot(300, this, &HomeWindow::NextScriptLine);
}

void HomeWindow::NextScriptLine() {
    if (script.isEmpty()) {
        return;
    }
    const QString line = script.takeFirst().trimmed();
    const QString word = line.section(QLatin1Char(' '), 0, 0);
    const QString rest = line.section(QLatin1Char(' '), 1);
    int wait = 120;
    if (word == QStringLiteral("key")) {
        static const QHash<QString, int> keys{{QStringLiteral("Left"), Qt::Key_Left},   {QStringLiteral("Right"), Qt::Key_Right},
                                              {QStringLiteral("Up"), Qt::Key_Up},       {QStringLiteral("Down"), Qt::Key_Down},
                                              {QStringLiteral("Enter"), Qt::Key_Return}, {QStringLiteral("Escape"), Qt::Key_Escape},
                                              {QStringLiteral("Q"), Qt::Key_Q},         {QStringLiteral("E"), Qt::Key_E},
                                              {QStringLiteral("F5"), Qt::Key_F5}, {QStringLiteral("F2"), Qt::Key_F2},
                                              {QStringLiteral("Backspace"), Qt::Key_Backspace},
                                              {QStringLiteral("Space"), Qt::Key_Space}, {QStringLiteral("F4"), Qt::Key_F4},
                                              {QStringLiteral("End"), Qt::Key_End}, {QStringLiteral("Home"), Qt::Key_Home},
                                              {QStringLiteral("S"), Qt::Key_S}, {QStringLiteral("F"), Qt::Key_F}};
        // A name of the list, else "/" or one letter or digit (a setting looked for by name).
        int key = keys.value(rest, 0);
        if (key == 0 && rest == QStringLiteral("/")) {
            key = Qt::Key_Slash;
        } else if (key == 0 && rest.size() == 1 && rest[0].isLetterOrNumber()) {
            key = rest[0].toUpper().unicode();
        }
        Key(key);
    } else if (word == QStringLiteral("press")) {
        // A key held down: "press Down 40 33" sends Down 40 times, 33 ms apart, through the
        // event queue as the system's auto-repeat does (then the frames' times, if traced).
        const QString name = rest.section(QLatin1Char(' '), 0, 0);
        const int count = rest.section(QLatin1Char(' '), 1, 1).toInt();
        const int every = std::max(1, rest.section(QLatin1Char(' '), 2, 2).toInt());
        const int key = name == QStringLiteral("Up") ? Qt::Key_Up : name == QStringLiteral("Down") ? Qt::Key_Down
                        : name == QStringLiteral("Left") ? Qt::Key_Left : Qt::Key_Right;
        Trace().Summary();
        for (int i = 0; i < count; ++i) {
            QTimer::singleShot(i * every, this, [this, key, i] {
                QCoreApplication::postEvent(this, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier, QString(), i > 0));
            });
        }
        wait = count * every + 600;
        QTimer::singleShot(wait - 10, this, [] { Trace().Summary(); });
    } else if (word == QStringLiteral("click")) {
        const double scale = std::min(width() / SheetW, height() / SheetH);
        const QPointF at((width() - SheetW * scale) / 2 + rest.section(QLatin1Char(' '), 0, 0).toDouble() * scale,
                         (height() - SheetH * scale) / 2 + rest.section(QLatin1Char(' '), 1, 1).toDouble() * scale);
        QMouseEvent press(QEvent::MouseButtonPress, at, mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        mousePressEvent(&press);
    } else if (word == QStringLiteral("shape")) {
        SetShelf(rest == QStringLiteral("shelf"), false);
    } else if (word == QStringLiteral("shelf-theme")) {
        SetShelfTheme(rest, false);
    } else if (word == QStringLiteral("hover")) {
        // The pointer over a place on the sheet, for pictures of what it lights.
        const QPointF at(rest.section(QLatin1Char(' '), 0, 0).toDouble(), rest.section(QLatin1Char(' '), 1, 1).toDouble());
        if (shelf_on) {
            ShelfHover(at, true);
        } else {
            Hover(at);
        }
    } else if (word == QStringLiteral("wait")) {
        wait = rest.toInt();
    } else if (word == QStringLiteral("shot")) {
        grab().save(rest); // as it is this moment, moving or not
    } else if (word == QStringLiteral("settings")) {
        // The settings window at a tab ("settings Save sync"), for pictures of it.
        if (open_settings) {
            open_settings(rest);
        }
    } else if (word == QStringLiteral("screen")) {
        // Every window shown (the settings window too), each its own picture where it stands:
        // a stand-in screen gives no picture of itself.
        QImage canvas(this->screen() ? this->screen()->geometry().size() : QSize(1280, 720), QImage::Format_RGB32);
        canvas.fill(Qt::black);
        QPainter painter(&canvas);
        // This page's window under the others.
        QList<QWidget*> tops = QApplication::topLevelWidgets();
        std::stable_partition(tops.begin(), tops.end(), [this](QWidget* top) { return top == window(); });
        for (QWidget* top : tops) {
            if (top->isVisible() && !top->isMinimized()) {
                painter.drawPixmap(top->frameGeometry().topLeft(), top->grab());
            }
        }
        painter.end();
        canvas.save(rest);
    } else if (word == QStringLiteral("grab")) {
        // The page at rest: what glides or fades is finished first.
        repaint();
        shelf_at = shelf_choice;
        stage_at = shelf_stage ? 1 : 0;
        pages_at = shelf_pages ? 1 : 0;
        log_at = shelf_log ? 1 : 0;
        wizard_at = wizard_on ? 1 : 0;
        gliding = false;
        bar_at = bar_to;
        width_at = width_to;
        fade_from = QImage();
        pieces_stale = true;
        repaint();
        grab().save(rest);
    } else if (word == QStringLiteral("theme")) {
        for (int i = 0; i < themes.size(); ++i) {
            if (themes[i].Key() == rest) {
                SetTheme(i, false);
            }
        }
    } else if (word == QStringLiteral("fuzz")) {
        SetFuzz(rest, false);
    } else if (word == QStringLiteral("motion")) {
        SetMotion(rest == QStringLiteral("on"), false);
    } else if (word == QStringLiteral("reload")) {
        ReloadKeepingChoice();
        Changed();
    } else if (word == QStringLiteral("lines-add")) {
        // lines-add websocket|clipboard TITLE: what the Settings page's rows do, without their window.
        AddLinesTitle(rest.section(QLatin1Char(' '), 0, 0), rest.section(QLatin1Char(' '), 1));
    } else if (word == QStringLiteral("lines-address")) {
        SetLinesAddress(rest);
    } else if (word == QStringLiteral("wizard")) {
        // The wizard that adds a title ("wizard helper": the OCR helper's settings alone).
        if (!shelf_on) {
            SetShelf(true, false);
        }
        OpenWizard(rest == QStringLiteral("helper"));
    } else if (word == QStringLiteral("wizard-window")) {
        // The game window whose title has these words, chosen among the wizard's (for its pictures).
        WizardChooseWindow(rest);
    } else if (word == QStringLiteral("type")) {
        // Text typed on the wizard's line in hand.
        WizardText(0, rest);
    } else if (word == QStringLiteral("drag")) {
        // drag X1 Y1 X2 Y2 (on the sheet): the pointer pressed, moved and let go.
        const QStringList at = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (at.size() == 4 && shelf_on) {
            ShelfClick(QPointF(at[0].toDouble(), at[1].toDouble()));
            if (wizard_on) {
                WizardDrag(QPointF(at[2].toDouble(), at[3].toDouble()), true);
            }
        }
    } else if (word == QStringLiteral("hook-add")) {
        // hook-add ENGINE PROGRAM SCRIPT|- [THREAD,THREAD] TITLE: a title read by a hook, without the wizard (a test's).
        const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 5) {
            Lines::Title hook;
            hook.engine = parts[0];
            hook.process = parts[1];
            hook.script = parts[2] == QStringLiteral("-") ? QString() : parts[2];
            hook.threads = parts[3] == QStringLiteral("-") ? QStringList() : parts[3].split(QLatin1Char(','), Qt::SkipEmptyParts);
            AddHookTitle(rest.section(QLatin1Char(' '), 4), hook);
        }
    } else if (word == QStringLiteral("ocr-helper")) {
        // ocr-helper ADDRESS SOURCE TOKEN: the helper's settings, as the wizard writes them.
        SetOcrHelper(rest.section(QLatin1Char(' '), 0, 0), rest.section(QLatin1Char(' '), 1, 1), rest.section(QLatin1Char(' '), 2, 2));
    } else if (word == QStringLiteral("ocr-add")) {
        // ocr-add X Y W H WINDOW|TITLE: a window read by OCR, without the wizard (a test's).
        const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString names = rest.section(QLatin1Char(' '), 4);
        if (parts.size() >= 5) {
            AddWindowTitle(names.section(QLatin1Char('|'), 1), names.section(QLatin1Char('|'), 0, 0),
                           QRectF(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble()));
        }
    } else if (word == QStringLiteral("quit")) {
        window()->close();
        return;
    } else if (script_commands) {
        script_commands(word, rest);
    }
    QTimer::singleShot(wait, this, &HomeWindow::NextScriptLine);
}

#include "shelf_page.inc"
#include "wizard_page.inc"
#include "wizard_hooks.inc"

} // namespace Home
