// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The reader over the emulator's window: what the reader's windows ask of the emulator
// (host.h), answered with DuckStation, and what the emulator's main loop calls (install.h).
//
// The emulator's window is SDL's (the NoGUI frontend) and everything in it is drawn by the
// emulator. The reader's windows are Qt's. So there is a Qt application beside SDL, whose
// events the emulator's main loop handles once a turn (Pump), and a window of Qt's that
// nobody sees or can click (the "shadow"), kept exactly where the emulator's window is: to the
// reader's windows it is the main window and the widget the game is drawn in, which is what
// they were written against. The same as in the Flycast Reader fork, whose emulator has an
// SDL window too.

#include <deque>
#include <map>
#include <mutex>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>
#include <QWindow>

#include "common/log.h"
#include "core/host_display.h"
#include "core/settings.h"
#include "core/system.h"
#include "frontend-common/common_host_interface.h"
#include "frontend-common/fullscreen_ui.h"
#include "frontend-common/game_list.h"
#include "frontend-common/opengl_host_display.h"
#include "glad.h"

#include "reader/core/emulator.h"
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_control.h"
#include "reader/qt/reader_controller.h"
#include "reader/qt/reader_link.h"
#include "reader/qt/reader_session.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/qt/save_sync_dialog.h"
#include "reader/save_sync/host.h"
#endif

// Last: the window system's own headers define plain words (None, Status) as macros.
#include <SDL.h>
#include <SDL_syswm.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

Log_SetChannel(Reader);

// Reader fork: src/duckstation-nogui/sdl_host_interface.cpp says which window is the emulator's.
SDL_Window* sdl_reader_window();

namespace ReaderUi {

void WriteLog(HostLog level, const std::string& message) {
    switch (level) {
    case HostLog::Info:
        Log_InfoPrintf("%s", message.c_str());
        break;
    case HostLog::Warning:
        Log_WarningPrintf("%s", message.c_str());
        break;
    case HostLog::Error:
        Log_ErrorPrintf("%s", message.c_str());
        break;
    }
}

namespace {
std::mutex g_gui_tasks_mutex;
std::deque<std::function<void()>> g_gui_tasks;

/// On the main thread: what the emulator's threads asked for.
void RunGuiTasks() {
    for (;;) {
        std::function<void()> task;
        {
            std::scoped_lock lock{g_gui_tasks_mutex};
            if (g_gui_tasks.empty()) {
                return;
            }
            task = std::move(g_gui_tasks.front());
            g_gui_tasks.pop_front();
        }
        task();
    }
}

CommonHostInterface* Emulator() {
    return static_cast<CommonHostInterface*>(g_host_interface);
}
} // namespace

void PostToGui(std::function<void()> task) {
    std::scoped_lock lock{g_gui_tasks_mutex};
    g_gui_tasks.push_back(std::move(task));
}

QString ReaderDirectory() {
    // Beside the emulator's own files, in its user folder.
    const QString directory =
        QString::fromStdString(g_host_interface->GetUserDirectoryRelativePath("reader"));
    QDir().mkpath(directory);
    return QDir::cleanPath(directory);
}

QString ReaderSettingsFile() {
    return ReaderDirectory() + QStringLiteral("/reader.ini");
}

QString CacheDirectory() {
    const QString directory = ReaderDirectory() + QStringLiteral("/cache");
    QDir().mkpath(directory);
    return directory;
}

namespace {
bool g_pointer_said = false;
QPoint g_pointer_position;
QString g_surface_grab;
} // namespace

QPoint PointerPosition() {
    return g_pointer_said ? g_pointer_position : QCursor::pos();
}

void SetPointerPositionForTests(const QPoint& global) {
    g_pointer_said = true;
    g_pointer_position = global;
}

std::optional<QByteArray> HostFetch(const QUrl& url, QString* error) {
    // Qt's network access; the caller's window waits, the app goes on drawing.
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    std::optional<QByteArray> out;
    if (reply->error() == QNetworkReply::NoError) {
        out = reply->readAll();
    } else {
        *error = QCoreApplication::translate("ReaderUi", "Could not fetch %1 (%2)")
                     .arg(url.toString(), reply->errorString());
    }
    reply->deleteLater();
    return out;
}

bool HostCommand(const QString& name, const QStringList& parts, const QString& rest,
                 const std::function<void(const QString&)>& reply, QObject* context) {
    const auto ok = [&reply](const QString& text = {}) {
        reply(text.isEmpty() ? QStringLiteral("OK") : QStringLiteral("OK ") + text);
    };
    if (name == QStringLiteral("home-key")) {
        // home-key NAME: a key for the emulator's own interface (its menus): Left Right Up
        // Down Enter Escape, or any key by SDL's name for it ("F1", "Space").
        if (HomeKey(rest)) {
            ok();
        } else {
            reply(QStringLiteral("ERR no such key"));
        }
    } else if (name == QStringLiteral("home-click")) {
        // home-click X Y: a click in the emulator's window, in its own units.
        HomeClick(parts.value(1).toFloat(), parts.value(2).toFloat());
        ok();
    } else if (name == QStringLiteral("dump")) {
        // dump PATH: the console's main memory as it is now, for finding a game's text.
        if (Reader::Emu::DumpMemory(rest.toStdString())) {
            ok(rest);
        } else {
            reply(QStringLiteral("ERR no memory to write (no game?)"));
        }
    } else if (name == QStringLiteral("state")) {
        // state save|load SLOT: one of the emulator's savestates of the running game.
        if (HostState(parts.value(1) == QStringLiteral("save"), parts.value(2).toInt())) {
            ok();
        } else {
            reply(QStringLiteral("ERR no game, or no such slot"));
        }
    } else if (name == QStringLiteral("speed")) {
        // speed N: the emulation's speed in percent (0: as fast as it goes), to get through a
        // game's opening quickly while a profile is written.
        g_settings.emulation_speed = parts.value(1).toFloat() / 100.0f;
        Emulator()->RunLater([] { System::UpdateThrottlePeriod(); });
        ok();
    } else {
        return false;
    }
    return true;
}

namespace {
/// A key goes into SDL's own queue, as if it had been pressed: the emulator's interface reads
/// it the way it reads a real one.
void PushKey(SDL_Keycode key, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    if (SDL_Window* window = sdl_reader_window()) {
        event.key.windowID = SDL_GetWindowID(window);
    }
    SDL_PushEvent(&event);
}
} // namespace

bool HomeKey(const QString& name) {
    static const std::map<QString, SDL_Keycode> keys{
        {QStringLiteral("Left"), SDLK_LEFT},   {QStringLiteral("Right"), SDLK_RIGHT},
        {QStringLiteral("Up"), SDLK_UP},       {QStringLiteral("Down"), SDLK_DOWN},
        {QStringLiteral("Enter"), SDLK_RETURN}, {QStringLiteral("Escape"), SDLK_ESCAPE},
    };
    SDL_Keycode key = SDLK_UNKNOWN;
    if (const auto found = keys.find(name); found != keys.end()) {
        key = found->second;
    } else {
        key = SDL_GetKeyFromName(name.toUtf8().constData());
    }
    if (key == SDLK_UNKNOWN) {
        return false;
    }
    PushKey(key, true);
    // Up a few turns later: the interface sees a press, not a key that came and went at once.
    QTimer::singleShot(80, qApp, [key] { PushKey(key, false); });
    return true;
}

bool HostState(bool save, int slot) {
    if (slot < 1 || slot > 10 || !System::IsValid()) {
        return false;
    }
    Emulator()->RunLater([save, slot] {
        if (save) {
            Emulator()->SaveState(false, slot);
        } else {
            Emulator()->LoadState(false, slot);
        }
    });
    return true;
}

void HomeClick(float x, float y) {
    SDL_Window* window = sdl_reader_window();
    if (window == nullptr) {
        return;
    }
    const Uint32 id = SDL_GetWindowID(window);
    const auto push = [id, x, y](Uint32 type) {
        SDL_Event event{};
        event.type = type;
        if (type == SDL_MOUSEMOTION) {
            event.motion.windowID = id;
            event.motion.x = static_cast<Sint32>(x);
            event.motion.y = static_cast<Sint32>(y);
        } else {
            event.button.windowID = id;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
            event.button.clicks = 1;
            event.button.x = static_cast<Sint32>(x);
            event.button.y = static_cast<Sint32>(y);
        }
        SDL_PushEvent(&event);
    };
    // The interface reads the button once a frame: the press and the release a few frames
    // apart, as a hand makes them.
    push(SDL_MOUSEMOTION);
    QTimer::singleShot(50, qApp, [push] { push(SDL_MOUSEBUTTONDOWN); });
    QTimer::singleShot(130, qApp, [push] { push(SDL_MOUSEBUTTONUP); });
}

void RequestSurfaceGrab(const QString& path) {
    g_surface_grab = path;
}

namespace {

void BeforePresent(int width, int height);

ReaderPointerFilter* g_pointer_filter = nullptr;
bool g_has_pointer = false;
ReaderController* g_controller = nullptr;
QMainWindow* g_shadow = nullptr;
/// The reader stopped the game for one of its own windows, and starts it again.
bool g_paused_by_reader = false;
bool g_in_pump = false;
bool g_unmanaged = false;
void (*g_idle_frame)() = nullptr;
bool g_in_idle_frame = false;
/// When the turn of the main loop that is being answered began.
QElapsedTimer g_pump_clock;
/// A turn that takes longer than this is one of the reader's windows running the events.
constexpr qint64 NestedAfterMs = 60;

bool Nested() {
    return g_in_pump && g_pump_clock.isValid() && g_pump_clock.elapsed() > NestedAfterMs;
}

/// Whether the reader's own windows wear the console's look: Reader settings' own switch,
/// on unless it was turned off (the app's own reader.ini, not the shared one).
bool LookEnabled() {
    QSettings settings(ReaderSettingsFile(), QSettings::IniFormat);
    return settings.value(QStringLiteral("console/look"), true).toBool();
}

/// The console's colours for the reader's own windows: the grey of the console itself and of
/// its system menu's panels, dark ink, and the deep blue of its own menus for what is chosen.
void ApplyLook(QApplication& app) {
    if (!LookEnabled()) {
        return;
    }
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QColor ground(0xD9, 0xD8, 0xD3), panel(0xF2, 0xF1, 0xEC), button(0xC8, 0xC7, 0xC1);
    const QColor ink(0x23, 0x24, 0x2B), soft(0x6B, 0x6C, 0x74), blue(0x2B, 0x3A, 0x8C), edge(0x9C, 0x9B, 0x95);
    QPalette palette;
    palette.setColor(QPalette::Window, ground);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, panel);
    palette.setColor(QPalette::AlternateBase, QColor(0xE6, 0xE5, 0xE0));
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::ToolTipBase, panel);
    palette.setColor(QPalette::ToolTipText, ink);
    palette.setColor(QPalette::PlaceholderText, soft);
    palette.setColor(QPalette::Highlight, blue);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, blue);
    palette.setColor(QPalette::Light, panel);
    palette.setColor(QPalette::Mid, edge);
    palette.setColor(QPalette::Dark, soft);
    palette.setColor(QPalette::Disabled, QPalette::Text, soft);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, soft);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x9A, 0x9A, 0x9E));
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(
        "QPushButton, QToolButton { background: #C8C7C1; color: #23242B; border: 1px solid #9C9B95;"
        " border-radius: 3px; padding: 5px 14px; }"
        "QPushButton:hover, QToolButton:hover { background: #E2E1DB; }"
        "QPushButton:pressed, QPushButton:default:pressed { background: #2B3A8C; color: white; }"
        "QPushButton:default { border: 2px solid #2B3A8C; }"
        "QPushButton:disabled { background: #DDDCD7; color: #9A9A9E; border-color: #C3C2BC; }"
        "QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit { background: #F2F1EC; color: #23242B;"
        " border: 1px solid #9C9B95; border-radius: 2px; padding: 3px 6px;"
        " selection-background-color: #2B3A8C; selection-color: white; }"
        "QTableView, QTreeView, QListView { background: #F2F1EC; alternate-background-color: #E6E5E0;"
        " border: 1px solid #9C9B95; selection-background-color: #2B3A8C; selection-color: white; }"
        "QHeaderView::section { background: #C8C7C1; color: #23242B; border: none; padding: 4px 6px; }"
        "QTabWidget::pane { border: 1px solid #9C9B95; background: #F2F1EC; }"
        "QTabBar::tab { background: #C8C7C1; color: #23242B; padding: 6px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #2B3A8C; color: white; }"
        "QMenu { background: #F2F1EC; color: #23242B; border: 1px solid #9C9B95; }"
        "QMenu::item:selected { background: #2B3A8C; color: white; }"
        "QGroupBox { border: 1px solid #9C9B95; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; color: #23242B; }"
        "QCheckBox, QRadioButton, QLabel { color: #23242B; }"));
}

/// The game is going: not paused, not behind the emulator's menus.
bool Playing() {
    return System::IsRunning() && !FullscreenUI::HasActiveWindow();
}

/// The game stands still because the reader stopped it, with its last picture up.
bool PausedByReader() {
    return g_paused_by_reader && System::IsPaused();
}

bool GameLoaded() {
    return System::IsValid();
}

/// The game's picture is what the window shows: going, or stopped by the reader.
bool GameInFront() {
    return Playing() || (PausedByReader() && !FullscreenUI::HasActiveWindow());
}

/// How many of the emulator's window's units one of Qt's is.
qreal Scale() {
    return g_shadow ? g_shadow->devicePixelRatioF() : 1.0;
}

/// Where the game's picture is in the emulator's window, in Qt's units.
QRectF PictureRect() {
    SDL_Window* window = sdl_reader_window();
    HostDisplay* display = g_host_interface->GetDisplay();
    if (window == nullptr || display == nullptr) {
        return {};
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const auto [left, top, draw_width, draw_height] = display->CalculateDrawRect(width, height, 0);
    const qreal scale = Scale();
    return QRectF(left / scale, top / scale, draw_width / scale, draw_height / scale);
}

/// The game's picture as it was last drawn, in its own size. On the main thread, which is the
/// one that draws.
QImage GrabPicture() {
    HostDisplay* display = g_host_interface->GetDisplay();
    if (display == nullptr) {
        return {};
    }
    const int width = static_cast<int>(display->GetDisplayWidth());
    const int height = static_cast<int>(display->GetDisplayHeight());
    std::vector<u32> pixels;
    if (width <= 0 || height <= 0 ||
        !display->WriteDisplayTextureToBuffer(&pixels, static_cast<u32>(width), static_cast<u32>(height)) ||
        pixels.size() < static_cast<std::size_t>(width) * height) {
        return {};
    }
    return QImage(reinterpret_cast<const uchar*>(pixels.data()), width, height, width * 4,
                  QImage::Format_RGBA8888)
        .convertToFormat(QImage::Format_RGB32);
}

/// The reading statistics go along with the saves: every reading day of every game, whole
/// each time, under an ID of this install's own. Nothing is said when it does not arrive.
void SendStats() {
#ifndef READER_NO_SAVE_SYNC
    if (!SaveSync::Host::Get().enabled) {
        return;
    }
    auto& session = ReaderSession::Instance();
    const std::string install = session.Settings().StatsInstall().toStdString();
    const QString device = QString::fromStdString(SaveSync::Host::Get().device);
    ReaderApp::ReaderDatabase* database = &session.Database();
    session.AfterWrites([install, device, database] {
        QJsonObject record = database->StatsRecord();
        record.insert(QStringLiteral("app"), QStringLiteral("station-reader"));
#ifdef _WIN32
        record.insert(QStringLiteral("platform"), QStringLiteral("windows"));
#else
        record.insert(QStringLiteral("platform"), QStringLiteral("linux"));
#endif
        record.insert(QStringLiteral("device"), device);
        record.insert(QStringLiteral("sent"), QDateTime::currentMSecsSinceEpoch());
        SaveSync::Host::SendStats(
            install, QJsonDocument(record).toJson(QJsonDocument::Compact).toStdString());
    });
#endif
}

/// What Reader Home is told of this app (reader_link.h).
const HomeLink::About& LinkAbout() {
    static const HomeLink::About about{QStringLiteral("station-reader"), QStringLiteral("Station Reader"),
                                       QStringLiteral("PlayStation"), {QStringLiteral("%ROM%")}, {},
                                       {QStringLiteral("-fullscreen")}};
    return about;
}

/// The reading statistics, for Reader Home's sums.
void TellHomeStats() {
    auto& session = ReaderSession::Instance();
    ReaderApp::ReaderDatabase* database = &session.Database();
    session.AfterWrites([database] {
        const QJsonObject record = database->StatsRecord();
        HomeLink::Write(LinkAbout(), nullptr, &record);
    });
}

/// The games of the emulator's list whose text can be read, for Reader Home's library. The
/// list is asked again and again while no game runs; the file is written when the answer has
/// changed.
void TellHomeGames() {
    QList<HomeLink::Game> games;
    GameList* list = Emulator()->GetGameList();
    if (list == nullptr) {
        return;
    }
    for (const GameListEntry& entry : list->GetEntries()) {
        if (entry.code.empty() || !Reader::Emu::HasProfile(entry.code)) {
            continue;
        }
        games.append({QString::fromStdString(entry.code), QString::fromStdString(entry.title),
                      QDir::toNativeSeparators(QString::fromStdString(entry.path)),
                      QDir::toNativeSeparators(
                          QString::fromStdString(list->GetCoverImagePathForEntry(&entry)))});
    }
    HomeLink::Write(LinkAbout(), &games, nullptr);
}

#ifdef _WIN32
/// The emulator's window, the owner of the shadow on Windows (BindToWindow).
HWND g_owner = nullptr;

/// Windows keeps an owned window above its owner and minimises it with it. Qt cannot make a
/// window that is not its own the owner (a transient parent that is a foreign window is
/// ignored), and it sets the owner again each time it shows the shadow: so it is said here,
/// and again whenever it is lost.
void KeepOwner() {
    if (g_owner == nullptr || g_shadow == nullptr || g_shadow->windowHandle() == nullptr) {
        return;
    }
    const HWND shadow = reinterpret_cast<HWND>(g_shadow->winId());
    if (GetWindow(shadow, GW_OWNER) != g_owner) {
        SetWindowLongPtrW(shadow, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(g_owner));
    }
}
#endif

/// Keeps the shadow where the emulator's window is, shown while that one is. `now` puts the
/// layer over the game in its place at once too (not only every fifteenth turn).
void FollowWindow(bool now = false) {
    SDL_Window* window = sdl_reader_window();
    if (window == nullptr || g_shadow == nullptr) {
        return;
    }
    const Uint32 flags = SDL_GetWindowFlags(window);
    bool shown = (flags & SDL_WINDOW_SHOWN) != 0 && (flags & SDL_WINDOW_MINIMIZED) == 0;
    // Out of the window manager's hands, the reader's windows lie above every window on the
    // screen: they go when the emulator's window is no longer the one in use. (None of them
    // ever takes the keyboard, so a click on the popup does not count as leaving.) A window
    // that never had the keyboard at all (a screen without one) keeps them.
    static bool had_focus = false;
    static bool has_focus = false;
    const bool focus = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (focus != has_focus) {
        has_focus = focus;
        had_focus = had_focus || focus;
    }
    if (g_unmanaged && had_focus && !has_focus && QApplication::activeWindow() == nullptr) {
        shown = false;
    }
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &width, &height);
    const qreal scale = Scale();
    const QRect place(qRound(x / scale), qRound(y / scale), qRound(width / scale),
                      qRound(height / scale));
    if (g_shadow->geometry() != place) {
        g_shadow->setGeometry(place);
    }
    if (g_shadow->isVisible() != shown) {
        g_shadow->setVisible(shown);
    }
#ifdef _WIN32
    KeepOwner();
#endif
    // The layer over the game covers the shadow exactly. Some window managers put a window
    // where they like when it first appears, whatever it asked for: it is put back.
    static int turn = 0;
    if (++turn % 15 == 0 || now) {
        for (QWidget* child : g_shadow->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            if (child->isWindow() && child->isVisible() && child->inherits("ReaderUi::ReaderOverlay") &&
                child->geometry() != place) {
                child->setGeometry(place);
            }
        }
    }
}

/// The shadow belongs to the emulator's window as far as the window manager is concerned:
/// above it, and gone with it.
void BindToWindow() {
    SDL_Window* window = sdl_reader_window();
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window == nullptr || !SDL_GetWindowWMInfo(window, &info)) {
        return;
    }
    WId native = 0;
#if defined(_WIN32)
    if (info.subsystem == SDL_SYSWM_WINDOWS) {
        native = reinterpret_cast<WId>(info.info.win.window);
    }
#elif defined(SDL_VIDEO_DRIVER_X11)
    if (info.subsystem == SDL_SYSWM_X11) {
        native = static_cast<WId>(info.info.x11.window);
    }
#endif
    if (native == 0) {
        return;
    }
    g_shadow->winId();
#ifdef _WIN32
    g_owner = reinterpret_cast<HWND>(native);
    KeepOwner();
    // While the window is dragged or sized, Windows runs a loop of its own and the emulator's
    // turns stop: the window's moves still come through SDL's window procedure, and the
    // reader's windows go along with them there.
    SDL_AddEventWatch(
        [](void*, SDL_Event* event) -> int {
            if (event->type == SDL_WINDOWEVENT &&
                (event->window.event == SDL_WINDOWEVENT_MOVED ||
                 event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                FollowWindow(true);
            }
            return 0;
        },
        nullptr);
#else
    if (QWindow* foreign = QWindow::fromWinId(native)) {
        g_shadow->windowHandle()->setTransientParent(foreign);
    }
#endif
}

/// The shadow. No pointer ever reaches it from the system (it lets everything through to
/// the emulator's window, whose own events come to PointerPress and its fellows below); the
/// development channel hands it mouse events as it would hand them to a window that draws
/// the game, and these take the same way from there.
class ShadowWindow final : public QMainWindow {
public:
    ShadowWindow(QWidget* parent, Qt::WindowFlags flags) : QMainWindow(parent, flags) {
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        const qreal scale = devicePixelRatioF();
        PointerPress(static_cast<float>(event->position().x() * scale),
                     static_cast<float>(event->position().y() * scale),
                     event->button() == Qt::LeftButton);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        const qreal scale = devicePixelRatioF();
        PointerMove(static_cast<float>(event->position().x() * scale),
                    static_cast<float>(event->position().y() * scale));
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        const qreal scale = devicePixelRatioF();
        PointerRelease(static_cast<float>(event->position().x() * scale),
                       static_cast<float>(event->position().y() * scale));
    }
};

void Install() {
    // Qt and the emulator count the screen in the same units on X11 this way.
    if (qEnvironmentVariableIsEmpty("QT_ENABLE_HIGHDPI_SCALING")) {
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    }
    static int argc = 1;
    static char name[] = "station-reader";
    static char* argv[] = {name, nullptr};
    auto* app = new QApplication(argc, argv);
    app->setApplicationName(QStringLiteral("station-reader"));
    app->setApplicationDisplayName(QStringLiteral("Station Reader"));
    app->setQuitOnLastWindowClosed(false);
    app->setWindowIcon(QIcon(QStringLiteral(":/reader/icon/app.png")));
    ApplyLook(*app);
    // While a window of the reader's own runs the events, the emulator's window is still
    // drawn and still answers.
    auto* idle = new QTimer(app);
    idle->setInterval(16);
    QObject::connect(idle, &QTimer::timeout, app, [] {
        if (g_idle_frame != nullptr && !g_in_idle_frame && Nested()) {
            g_in_idle_frame = true;
            g_idle_frame();
            g_in_idle_frame = false;
        }
    });
    idle->start();

    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                            Qt::WindowDoesNotAcceptFocus | Qt::Tool;
#if !defined(_WIN32) && !defined(__APPLE__)
    // On X11 the reader's windows over the game are kept out of the window manager's hands
    // unless READER_OVERLAY_UNMANAGED=0 says otherwise: they belong to a window that is not
    // Qt's, and a window manager was seen to put such windows where it liked.
    if (qEnvironmentVariableIsEmpty("READER_OVERLAY_UNMANAGED")) {
        qputenv("READER_OVERLAY_UNMANAGED", "1");
    }
#endif
    g_unmanaged = qEnvironmentVariableIntValue("READER_OVERLAY_UNMANAGED") != 0;
    if (g_unmanaged) {
        flags |= Qt::X11BypassWindowManagerHint;
    }
    g_shadow = new ShadowWindow(nullptr, flags);
    g_shadow->setAttribute(Qt::WA_TranslucentBackground);
    g_shadow->setAttribute(Qt::WA_ShowWithoutActivating);
    g_shadow->setAttribute(Qt::WA_TransparentForMouseEvents);
    g_shadow->setStyleSheet(QStringLiteral("QMainWindow { background: transparent; }"));
    g_shadow->setWindowTitle(QStringLiteral("Station Reader"));
    BindToWindow();
    FollowWindow();

    ReaderSession::Instance();
    // The text engine reports to the app from here on.
    ReaderBridge::OnGameLoading();

    EmulatorHost host;
    host.is_powered_on = [] { return GameLoaded(); };
    host.is_running = [] { return Playing(); };
    // Around the reader's own windows, and when the user asks for it to read what is on
    // screen, the game stands still: the emulator's own pause, which keeps the picture up.
    host.pause = [] {
        if (Playing()) {
            g_host_interface->PauseSystem(true);
            g_paused_by_reader = true;
        }
    };
    host.resume = [] {
        if (g_paused_by_reader) {
            g_paused_by_reader = false;
            if (System::IsPaused() && !FullscreenUI::HasActiveWindow()) {
                g_host_interface->PauseSystem(false);
            }
        }
    };
    host.picture_rect = [] { return PictureRect(); };
    host.grab_picture = [](std::function<void(const QImage&)> callback) {
        callback(GameLoaded() ? GrabPicture() : QImage());
    };
    host.set_pointer_filter = [](ReaderPointerFilter* filter) {
        g_pointer_filter = filter;
        g_has_pointer = false;
    };

    g_controller = new ReaderController(g_shadow, g_shadow, std::move(host));
#ifndef READER_NO_SAVE_SYNC
    // The Save sync window, in the Reader menu as in the other reader apps.
    g_controller->AddWindowEntry(QObject::tr("Save &sync…"), QStringLiteral("reader_save_sync"),
                                 [](QWidget* parent) {
                                     SaveSyncDialog dialog(parent);
                                     dialog.exec();
                                 });
#endif

    // The emulator starts and stops games and tells the engine; the engine tells us, on the
    // emulator's threads. Nothing of Qt in these (PostToGui says why): plain strings go over
    // to the main thread.
    ReaderBridge::PrepareFiles();
    Reader::Emu::SetGameLoadedCallback([](const std::string& game_id) {
        ReaderBridge::OnGameStarted(); // the profile goes in before the game's code runs
        const std::string title = Reader::Emu::GameTitle();
        PostToGui([game_id, title] {
            g_controller->OnGameBooted(QString::fromStdString(game_id),
                                       QString::fromStdString(title));
        });
    });
    Reader::Emu::SetGameStoppedCallback([] {
        PostToGui([] {
            g_paused_by_reader = false;
            ReaderBridge::OnGameStopped();
            g_controller->OnGameStopped();
            SendStats();
            TellHomeStats();
        });
    });
    // A game's stop is not the only moment the statistics go out: soon after the app starts
    // (what the last run counted) and every few minutes of a game.
    QTimer::singleShot(10000, g_controller, [] { SendStats(); });
    auto* stats_timer = new QTimer(g_controller);
    stats_timer->setInterval(5 * 60 * 1000);
    QObject::connect(stats_timer, &QTimer::timeout, g_controller, [] {
        if (ReaderSession::Instance().IsActive()) {
            SendStats();
        }
    });
    stats_timer->start();
    // A game that can be read: a bundled profile, or one of the user's.
    Reader::Emu::SetProfileCheck([](const std::string& game_id) {
        const QString name = QString::fromStdString(game_id) + QStringLiteral(".lua");
        return QFile::exists(QStringLiteral(":/reader/profiles/") + name) ||
               QFile::exists(ReaderSession::DataDirectory() + QStringLiteral("/profiles/") + name);
    });
    // The Reader entries of the emulator's own screens: a page by its name, or the menu.
    Reader::Emu::SetMenuCallback([](const std::string& what) {
        static const std::map<std::string, QString> pages{
            {"stats", QStringLiteral("reader_statistics")},
            {"games", QStringLiteral("reader_games")},
            {"dictionaries", QStringLiteral("reader_dictionaries")},
            {"anki", QStringLiteral("reader_anki")},
            {"settings", QStringLiteral("reader_settings")},
            {"backlog", QStringLiteral("reader_backlog")},
            {"sync", QStringLiteral("reader_save_sync")},
        };
        PostToGui([what] {
            const auto page = pages.find(what);
            if (page == pages.end()) {
                g_controller->Menu()->popup(PointerPosition());
            } else {
                g_controller->TriggerAction(page->second);
            }
        });
    });
    g_controller->OnApplicationActive(true);
    g_reader_before_present = &BeforePresent;

    ReaderControl::StartIfWanted(g_shadow, g_shadow, g_controller);
}

} // namespace

void Pump() {
    if (g_in_pump) {
        return; // a window of the reader's own is open and runs the events itself
    }
    g_in_pump = true;
    if (g_controller == nullptr) {
        Install();
    }
    g_pump_clock.start();
    FollowWindow();
    RunGuiTasks();
    // The emulator's own pause key or its menu ended what the reader began.
    if (g_paused_by_reader && !System::IsPaused()) {
        g_paused_by_reader = false;
        g_controller->GamePauseEnded();
    }
    static bool was_running = false;
    const bool running = Playing();
    // Reader Home hears of this app once at the start, and of its games while no game runs.
    static QElapsedTimer told;
    if (!told.isValid()) {
        told.start();
        TellHomeStats();
        TellHomeGames();
    } else if (!GameLoaded() && told.elapsed() > 4000) {
        told.restart();
        TellHomeGames();
    }
    if (running != was_running) {
        was_running = running;
        g_controller->OnEmulationRunning(running);
    }
    g_pump_clock.start();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    g_in_pump = false;
}

void SetIdleFrame(void (*frame)()) {
    g_idle_frame = frame;
}

bool WindowOpen() {
    return Nested();
}

bool PickFolder(const char* title, std::string& folder) {
    folder.clear();
#ifdef _WIN32
    if (qApp == nullptr) {
        return false;
    }
    const QString chosen = QFileDialog::getExistingDirectory(nullptr, QString::fromUtf8(title));
    folder = QDir::toNativeSeparators(chosen).toStdString();
    return true;
#else
    (void)title;
    return false;
#endif
}

void FrameDrawn() {
    // The OpenGL display hands its finished picture over before showing it (BeforePresent,
    // below); this is for the others: the game's picture as the emulator draws it, without
    // the emulator's menus.
    if (g_surface_grab.isEmpty()) {
        return;
    }
    HostDisplay* display = g_host_interface->GetDisplay();
    if (display == nullptr || display->GetRenderAPI() == HostDisplay::RenderAPI::OpenGL ||
        display->GetRenderAPI() == HostDisplay::RenderAPI::OpenGLES) {
        return;
    }
    const QString path = g_surface_grab;
    g_surface_grab.clear();
    const u32 width = display->GetWindowWidth();
    const u32 height = display->GetWindowHeight();
    std::vector<u32> pixels;
    u32 stride = 0;
    HostDisplayPixelFormat format = HostDisplayPixelFormat::Unknown;
    if (width == 0 || height == 0 || !display->RenderScreenshot(width, height, &pixels, &stride, &format)) {
        return;
    }
    QImage picture(reinterpret_cast<const uchar*>(pixels.data()), static_cast<int>(width),
                   static_cast<int>(height), static_cast<qsizetype>(stride),
                   format == HostDisplayPixelFormat::BGRA8 ? QImage::Format_ARGB32 : QImage::Format_RGBA8888);
    picture.convertToFormat(QImage::Format_RGB32).save(path);
}

namespace {
/// The OpenGL display's finished picture, menus and all, in the back buffer before it is shown.
void BeforePresent(int width, int height) {
    if (g_surface_grab.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    const QString path = g_surface_grab;
    g_surface_grab.clear();
    QImage picture(width, height, QImage::Format_RGBA8888);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, picture.bits());
    // OpenGL's rows go upwards.
    picture.convertToFormat(QImage::Format_RGB32).mirrored().save(path);
}
} // namespace

bool Shortcut(int key) {
    static const std::map<int, QString> actions{
        {'g', QStringLiteral("reader_pause_game")},
        {'t', QStringLiteral("reader_pause_stats")},
        {'d', QStringLiteral("reader_sentence_card")},
        {'h', QStringLiteral("reader_backlog")},
    };
    const auto action = actions.find(key);
    if (action == actions.end() || g_controller == nullptr || !GameInFront()) {
        return false;
    }
    const QString name = action->second;
    // Not from inside the emulator's reading of its events: a window may open.
    PostToGui([name] { g_controller->TriggerAction(name); });
    return true;
}

bool PointerPress(float x, float y, bool left_button) {
    if (!g_pointer_filter || !GameInFront()) {
        return false;
    }
    const qreal scale = Scale();
    if (g_pointer_filter->OnPointerPress(QPointF(x / scale, y / scale), left_button)) {
        g_has_pointer = true;
        return true;
    }
    return false;
}

bool PointerMove(float x, float y) {
    if (!g_pointer_filter || !GameInFront()) {
        return false;
    }
    const qreal scale = Scale();
    g_pointer_filter->OnPointerMove(QPointF(x / scale, y / scale));
    return g_has_pointer;
}

bool PointerRelease(float x, float y) {
    if (!g_has_pointer) {
        return false;
    }
    g_has_pointer = false;
    if (g_pointer_filter) {
        const qreal scale = Scale();
        g_pointer_filter->OnPointerRelease(QPointF(x / scale, y / scale));
    }
    return true;
}

} // namespace ReaderUi
