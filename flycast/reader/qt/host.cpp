// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The reader over the emulator's window: what the reader's windows ask of the emulator
// (host.h), answered with Flycast, and what the emulator's main loop calls (install.h).
//
// Flycast's window is SDL's and everything in it is drawn by the emulator. The reader's
// windows are Qt's. So there is a Qt application beside SDL, whose events the emulator's main
// loop handles once a turn (Pump), and a window of Qt's that nobody sees or can click (the
// "shadow"), kept exactly where the emulator's window is: to the reader's windows it is the
// main window and the widget the game is drawn in, which is what they were written against.

#include <deque>
#include <map>
#include <mutex>
#include <QCoreApplication>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QEventLoop>
#include <QFileDialog>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>
#include <QWindow>

#include "types.h"
#include "cfg/option.h"
#include "emulator.h"
#include "hw/pvr/Renderer_if.h"
#include "imgui.h"
#include "log/Log.h"
#include "rend/transform_matrix.h"
#include "stdclass.h"
#include "ui/gui.h"
#include "wsi/gl_context.h"

#include "reader/core/emulator.h"
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_link.h"
#include "reader/qt/reader_control.h"
#include "reader/qt/reader_controller.h"
#include "reader/qt/reader_session.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/qt/save_sync_dialog.h"
#endif
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif
#include "reader/ui/dc_home.h"

// Last: the window system's own headers define plain words (None, Status) as macros.
#include <SDL.h>
#include <SDL_syswm.h>
#if defined(_WIN32)
#include <QAbstractNativeEventFilter>
#include <windows.h>
#endif

// Reader fork: core/sdl/sdl.cpp says which window is the emulator's.
SDL_Window* sdl_reader_window();

namespace ReaderUi {

void WriteLog(HostLog level, const std::string& message) {
    switch (level) {
    case HostLog::Info:
        NOTICE_LOG(COMMON, "reader: %s", message.c_str());
        break;
    case HostLog::Warning:
        WARN_LOG(COMMON, "reader: %s", message.c_str());
        break;
    case HostLog::Error:
        ERROR_LOG(COMMON, "reader: %s", message.c_str());
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
} // namespace

void PostToGui(std::function<void()> task) {
    std::scoped_lock lock{g_gui_tasks_mutex};
    g_gui_tasks.push_back(std::move(task));
}

QString ReaderDirectory() {
    // Beside the emulator's own files, in its data folder.
    QString directory = QString::fromStdString(get_writable_data_path("reader"));
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
        // home-key NAME: a key for the emulator's own interface (the Dreamcast-style menu):
        // Left Right Up Down Enter Escape.
        if (HomeKey(rest)) {
            ok();
        } else {
            reply(QStringLiteral("ERR no such key"));
        }
    } else if (name == QStringLiteral("home-click")) {
        // home-click X Y: a click in the emulator's window, in its own units.
        HomeClick(parts.value(1).toFloat(), parts.value(2).toFloat());
        ok();
    } else if (name == QStringLiteral("analog")) {
        // analog X Y [MS]: the first controller's stick, each way from -100 to 100, held a
        // moment (a game with a pointer of its own).
        const int hold = parts.size() > 3 ? parts.value(3).toInt() : 100;
        Reader::Emu::MoveStick(parts.value(1).toInt(), parts.value(2).toInt());
        QTimer::singleShot(hold, context, [reply] {
            Reader::Emu::RestSticks();
            reply(QStringLiteral("OK"));
        });
    } else if (name == QStringLiteral("dump")) {
        // dump PATH: the console's main memory as it is now, for finding a game's text.
        if (Reader::Emu::DumpMemory(rest.toStdString())) {
            ok(rest);
        } else {
            reply(QStringLiteral("ERR no memory to write (no game?)"));
        }
    } else if (name == QStringLiteral("state")) {
        // state save|load SLOT: one of the emulator's ten savestates of the running game.
        if (HostState(parts.value(1) == QStringLiteral("save"), parts.value(2).toInt())) {
            ok();
        } else {
            reply(QStringLiteral("ERR no game, or no such slot"));
        }
    } else {
        return false;
    }
    return true;
}

bool HomeKey(const QString& name) {
    static const std::map<QString, ImGuiKey> keys{
        {QStringLiteral("Left"), ImGuiKey_LeftArrow}, {QStringLiteral("Right"), ImGuiKey_RightArrow},
        {QStringLiteral("Up"), ImGuiKey_UpArrow},     {QStringLiteral("Down"), ImGuiKey_DownArrow},
        {QStringLiteral("Enter"), ImGuiKey_Enter},    {QStringLiteral("Escape"), ImGuiKey_Escape},
    };
    const auto key = keys.find(name);
    if (key == keys.end()) {
        return false;
    }
    ImGui::GetIO().AddKeyEvent(key->second, true);
    ImGui::GetIO().AddKeyEvent(key->second, false);
    return true;
}

bool HostState(bool save, int slot) {
    if (slot < 0 || slot > 9 || !emu.running()) {
        return false;
    }
    config::SavestateSlot = slot;
    if (save) {
        gui_saveState(true);
    } else {
        gui_loadState();
    }
    return true;
}

void HomeClick(float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
}

void RequestSurfaceGrab(const QString& path) {
    g_surface_grab = path;
}

namespace {

ReaderPointerFilter* g_pointer_filter = nullptr;
bool g_has_pointer = false;
ReaderController* g_controller = nullptr;
QMainWindow* g_shadow = nullptr;
/// The reader stopped the game for one of its own windows, and starts it again.
bool g_paused_by_reader = false;
bool g_in_pump = false;
bool g_unmanaged = false;
/// Windows told of a change of the screens, the session or the system's sleep: the layer
/// over the game is shown afresh (CheckLayer).
bool g_layer_refresh = false;
void (*g_idle_frame)() = nullptr;
bool g_in_idle_frame = false;
/// When the turn of the main loop that is being answered began.
QElapsedTimer g_pump_clock;
/// A turn that takes longer than this is one of the reader's windows running the events.
constexpr qint64 NestedAfterMs = 60;

bool Nested() {
    return g_in_pump && g_pump_clock.isValid() && g_pump_clock.elapsed() > NestedAfterMs;
}

/// The console's colours for the reader's own windows: the Dreamcast-style menu's pale
/// ground, dark blue type and orange for what is chosen (reader/ui/dc_home.cpp).
void ApplyLook(QApplication& app) {
    if (!ReaderHome::LookEnabled()) {
        return;
    }
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QColor ground(0xEA, 0xF3, 0xFD), panel(0xFF, 0xFF, 0xFF), sky(0xC4, 0xDD, 0xF6);
    const QColor ink(0x1F, 0x3F, 0x8F), soft(0x5A, 0x7A, 0xB8), orange(0xF2, 0x7C, 0x1E), edge(0x7F, 0xA8, 0xE0);
    QPalette palette;
    palette.setColor(QPalette::Window, ground);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, panel);
    palette.setColor(QPalette::AlternateBase, QColor(0xF3, 0xF8, 0xFE));
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Button, sky);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::ToolTipBase, panel);
    palette.setColor(QPalette::ToolTipText, ink);
    palette.setColor(QPalette::PlaceholderText, soft);
    palette.setColor(QPalette::Highlight, orange);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor(0xD8, 0x62, 0x0A));
    palette.setColor(QPalette::Light, panel);
    palette.setColor(QPalette::Mid, edge);
    palette.setColor(QPalette::Dark, soft);
    palette.setColor(QPalette::Disabled, QPalette::Text, soft);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, soft);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x9A, 0xB0, 0xD4));
    app.setPalette(palette);
    // The menu is all rounded corners; what is chosen is orange with white type.
    app.setStyleSheet(QStringLiteral(
        "QPushButton, QToolButton { background: #C4DDF6; color: #1F3F8F; border: 1px solid #7FA8E0;"
        " border-radius: 12px; padding: 5px 14px; }"
        "QPushButton:hover, QToolButton:hover { background: #FFB870; }"
        "QPushButton:pressed, QPushButton:default:pressed { background: #F27C1E; color: white; }"
        "QPushButton:default { border: 2px solid #F27C1E; }"
        "QPushButton:disabled { background: #E3ECF7; color: #9AB0D4; border-color: #C9D8EE; }"
        "QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit { background: white; color: #1F3F8F;"
        " border: 1px solid #7FA8E0; border-radius: 8px; padding: 3px 6px;"
        " selection-background-color: #F27C1E; selection-color: white; }"
        "QTableView, QTreeView, QListView { background: white; alternate-background-color: #F3F8FE;"
        " border: 1px solid #7FA8E0; border-radius: 10px; selection-background-color: #F27C1E;"
        " selection-color: white; }"
        "QHeaderView::section { background: #C4DDF6; color: #1F3F8F; border: none; padding: 4px 6px; }"
        "QTabWidget::pane { border: 1px solid #7FA8E0; border-radius: 10px; background: white; }"
        "QTabBar::tab { background: #C4DDF6; color: #1F3F8F; padding: 6px 16px; margin-right: 3px;"
        " border-top-left-radius: 10px; border-top-right-radius: 10px; }"
        "QTabBar::tab:selected { background: #F27C1E; color: white; }"
        "QMenu { background: white; color: #1F3F8F; border: 1px solid #7FA8E0; }"
        "QMenu::item:selected { background: #F27C1E; color: white; }"
        "QGroupBox { border: 1px solid #7FA8E0; border-radius: 10px; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; color: #1F3F8F; }"
        "QCheckBox, QRadioButton, QLabel { color: #1F3F8F; }"));
}

/// The game is going: not paused, not behind the emulator's menus.
bool Playing() {
    return emu.running() && !gui_is_open();
}

/// The game stands still because the reader stopped it, with its last picture up (the
/// emulator's own "paused" screen, which keeps drawing that picture).
bool PausedByReader() {
    return g_paused_by_reader && gui_state == GuiState::Pause;
}

bool GameLoaded() {
    return g_paused_by_reader || emu.running();
}

/// The game's picture is what the window shows: going, or stopped by the reader.
bool GameInFront() {
    return Playing() || PausedByReader();
}

/// How many of the emulator's window's units one of Qt's is.
qreal Scale() {
    return g_shadow ? g_shadow->devicePixelRatioF() : 1.0;
}

/// Where the game's picture is in the emulator's window, in Qt's units.
QRectF PictureRect() {
    SDL_Window* window = sdl_reader_window();
    if (window == nullptr) {
        return {};
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    int dx = 0;
    int dy = 0;
    getWindowboxDimensions(width, height, getOutputFramebufferAspectRatio(), dx, dy,
                           config::Rotate90);
    const qreal scale = Scale();
    return QRectF(dx / scale, dy / scale, (width - 2 * dx) / scale, (height - 2 * dy) / scale);
}

/// The game's picture as it was last drawn. On the main thread, which is the one that draws.
QImage GrabPicture() {
    std::vector<u8> data;
    int width = 0;
    int height = 0;
    if (renderer == nullptr || !renderer->GetLastFrame(data, width, height) || width <= 0 ||
        height <= 0 || data.size() < static_cast<std::size_t>(width) * height * 3) {
        return {};
    }
    return QImage(data.data(), width, height, width * 3, QImage::Format_RGB888)
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
        record.insert(QStringLiteral("app"), QStringLiteral("flycast-reader"));
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
    static const HomeLink::About about{QStringLiteral("flycast-reader"), QStringLiteral("Flycast Reader"),
                                       QStringLiteral("Dreamcast"), {QStringLiteral("%ROM%")}, {},
                                       {QStringLiteral("-config"), QStringLiteral("window:fullscreen=yes")}};
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

/// The games whose text can be read, for Reader Home's library. The folders are looked
/// through in the background, so this is asked again and again while no game runs; the file
/// is written when the answer has changed.
void TellHomeGames() {
    QList<HomeLink::Game> games;
    for (const ReaderHome::ReadableGame& game : ReaderHome::ReadableGames()) {
        games.append({QString::fromStdString(game.id), QString::fromStdString(game.title),
                      QDir::toNativeSeparators(QString::fromStdString(game.path)),
                      QDir::toNativeSeparators(QString::fromStdString(game.cover))});
    }
    HomeLink::Write(LinkAbout(), &games, nullptr);
}

void CheckLayer();

/// Keeps the shadow where the emulator's window is, shown while that one is.
void FollowWindow() {
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
        NOTICE_LOG(COMMON, "reader: the emulator's window %s the keyboard", focus ? "has" : "lost");
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
    // The layer over the game covers the shadow exactly. Some window managers put a window
    // where they like when it first appears, whatever it asked for: it is put back.
    static int turn = 0;
    if (turn % 30 == 0) {
        CheckLayer();
    }
    if (++turn % 15 == 0) {
        for (QWidget* child : g_shadow->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            if (child->isWindow() && child->isVisible() && child->inherits("ReaderUi::ReaderOverlay") &&
                child->geometry() != place) {
                child->setGeometry(place);
            }
        }
    }
}

/// The layer over the game and its CRT frame were seen on the maintainer's Windows PC
/// (2026-10-19, July) to go at times and not come back, with nothing in the reader asking for
/// it. Why is not known yet. Each look (twice a second) puts down what it finds when that
/// changes, in flycast.log, and on Windows two cases are put right: the layer hidden by
/// Windows while Qt holds it shown, and the screens, the session or the system's sleep
/// having changed (a layered window was seen elsewhere to keep nothing of its picture after
/// that). Needs proper testing: the failure itself has not been seen here.
void CheckLayer() {
    if (g_shadow == nullptr) {
        return;
    }
    static SDL_Window* bound = sdl_reader_window();
    if (sdl_reader_window() != bound) {
        bound = sdl_reader_window();
        NOTICE_LOG(COMMON, "reader: the emulator's window was made anew");
    }
    const bool refresh = g_layer_refresh;
    g_layer_refresh = false;
    for (QWidget* child : g_shadow->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        auto* layer = qobject_cast<ReaderOverlay*>(child);
        if (layer == nullptr) {
            continue;
        }
        const bool shown = layer->isVisible();
        bool native = shown;
#if defined(_WIN32)
        if (shown && layer->testAttribute(Qt::WA_WState_Created)) {
            native = IsWindowVisible(reinterpret_cast<HWND>(layer->winId())) != 0;
        }
#endif
        const QRect place = layer->geometry();
        const QRect under = g_shadow->geometry();
        static QString said;
        const QString now =
            QStringLiteral("layer %1 (on screen %2), wanted %3, game %4, shadow %5, at %6,%7 %8x%9 over %10,%11 %12x%13")
                .arg(shown ? QStringLiteral("shown") : QStringLiteral("hidden"), native ? QStringLiteral("yes") : QStringLiteral("no"),
                     layer->HasContent() ? QStringLiteral("yes") : QStringLiteral("no"),
                     ReaderSession::Instance().IsActive() ? QStringLiteral("on") : QStringLiteral("off"),
                     g_shadow->isVisible() ? QStringLiteral("shown") : QStringLiteral("hidden"))
                .arg(place.x()).arg(place.y()).arg(place.width()).arg(place.height())
                .arg(under.x()).arg(under.y()).arg(under.width()).arg(under.height());
        if (now != said) {
            said = now;
            NOTICE_LOG(COMMON, "reader: %s", now.toUtf8().constData());
        }
        if (shown && (!native || refresh)) {
            NOTICE_LOG(COMMON, "reader: the layer is shown afresh (%s)",
                       native ? "the screens or the session changed" : "Windows had hidden it");
            layer->hide();
            layer->show();
            layer->update();
        }
    }
}

#if defined(_WIN32)
/// What Windows tells every window of the screens, the session and the system's sleep.
class LayerEvents final : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        const MSG* msg = static_cast<const MSG*>(message);
        const char* what = nullptr;
        switch (msg->message) {
        case WM_DISPLAYCHANGE:
            what = "the screens changed";
            break;
        case WM_DPICHANGED:
            what = "the screen's scale changed";
            break;
        case WM_DWMCOMPOSITIONCHANGED:
            what = "the desktop's drawing changed";
            break;
        case WM_POWERBROADCAST:
            if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
                what = "the computer woke";
            }
            break;
        default:
            break;
        }
        if (what != nullptr) {
            // Every window of the app hears it: once is enough.
            static QElapsedTimer last;
            if (!last.isValid() || last.elapsed() > 1000) {
                last.start();
                NOTICE_LOG(COMMON, "reader: %s", what);
            }
            g_layer_refresh = true;
        }
        return false;
    }
};
#endif

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
    if (QWindow* foreign = QWindow::fromWinId(native)) {
        g_shadow->windowHandle()->setTransientParent(foreign);
    }
}

/// The shadow. No pointer ever reaches it from the system (it lets everything through to
/// the emulator's window, whose own events come to PointerPress and its fellows below); the
/// development channel hands it mouse events as it would hand them to a window that draws
/// the game, and these take the same way from there.
class ShadowWindow final : public QMainWindow {
public:
    ShadowWindow(QWidget* parent, Qt::WindowFlags flags) : QMainWindow(parent, flags) {
        // A move with no button held reaches mouseMoveEvent only so (the channel's `move`, which
        // is how the README's demos point at words).
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
    static char name[] = "flycast-reader";
    static char* argv[] = {name, nullptr};
    auto* app = new QApplication(argc, argv);
    app->setApplicationName(QStringLiteral("flycast-reader"));
    app->setApplicationDisplayName(QStringLiteral("Flycast Reader"));
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
    g_shadow->setWindowTitle(QStringLiteral("Flycast Reader"));
    BindToWindow();
#if defined(_WIN32)
    app->installNativeEventFilter(new LayerEvents);
#endif
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
            try {
                emu.stop();
                gui_setState(GuiState::Pause);
                g_paused_by_reader = true;
            } catch (...) {
            }
        }
    };
    host.resume = [] {
        if (g_paused_by_reader) {
            g_paused_by_reader = false;
            if (gui_state == GuiState::Pause) {
                try {
                    gui_setState(GuiState::Closed);
                    emu.start();
                } catch (...) {
                }
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
    // The Save sync window, in the Reader menu as in the other reader apps (the Dreamcast
    // menu's File, Save Sync page is the same settings).
    g_controller->AddWindowEntry(QObject::tr("Save &sync…"), QStringLiteral("reader_save_sync"),
                                 [](QWidget* parent) {
                                     SaveSyncDialog dialog(parent);
                                     dialog.exec();
                                 });
#endif

    // The emulator starts and stops games from its own screens and tells the engine; the
    // engine tells us, on the emulator's threads. Nothing of Qt in these (PostToGui says
    // why): plain strings go over to the main thread.
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
    // A game's stop is not the only moment the statistics go out: a window closed with its game
    // still running stops nothing, and so sent nothing. Also soon after the app starts (what
    // the last run counted) and every few minutes of a game.
    QTimer::singleShot(10000, g_controller, [] { SendStats(); });
    auto* stats_timer = new QTimer(g_controller);
    stats_timer->setInterval(5 * 60 * 1000);
    QObject::connect(stats_timer, &QTimer::timeout, g_controller, [] {
        if (ReaderSession::Instance().IsActive()) {
            SendStats();
        }
    });
    stats_timer->start();
    // The game list's mark for a game that can be read: a bundled profile, or one of the
    // user's.
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

    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, g_controller,
                     [](Qt::ApplicationState) {
                         // The emulator's window is not Qt's: the app counts as active while
                         // that window is on screen.
                     });
    g_controller->OnApplicationActive(true);

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
    if (g_paused_by_reader && gui_state != GuiState::Pause) {
        g_paused_by_reader = false;
        g_controller->GamePauseEnded();
    }
    static bool was_running = false;
    const bool running = Playing();
    // Reader Home hears of this app once at the start, and of its games while its menu is up.
    static QElapsedTimer told;
    if (!told.isValid()) {
        told.start();
        TellHomeStats();
        TellHomeGames();
    } else if (!running && gui_state == GuiState::Main && told.elapsed() > 4000) {
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
    if (g_surface_grab.isEmpty()) {
        return;
    }
    const QString path = g_surface_grab;
    g_surface_grab.clear();
#ifdef USE_OPENGL
    const int width = settings.display.width;
    const int height = settings.display.height;
    if (isOpenGL(config::RendererType) && width > 0 && height > 0) {
        QImage picture(width, height, QImage::Format_RGBA8888);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, picture.bits());
        picture.convertToFormat(QImage::Format_RGB32).mirrored().save(path);
    }
#endif
}

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
