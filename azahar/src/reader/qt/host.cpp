// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// The reader in the emulator's Qt frontend: what the reader's windows ask of the emulator
// (host.h), answered with Azahar, and what the main window calls to have the reader at all
// (install.h). The windows are the reader layer every reader fork shares; what is the 3DS's
// own here is its HOME menu over the game list (threeds_home.*), its save sync's ties
// (save_sync_glue.*), its two screens and how their picture is had.

#include <deque>
#include <memory>
#include <mutex>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include "citra_qt/bootmanager.h"
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_control.h"
#include "reader/qt/reader_controller.h"
#include "reader/qt/reader_session.h"
#include "reader/qt/save_sync_glue.h"
#include "reader/qt/threeds_home.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "reader/core/emulator.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace ReaderUi {

void WriteLog(HostLog level, const std::string& message) {
    switch (level) {
    case HostLog::Info:
        LOG_INFO(Frontend, "{}", message);
        break;
    case HostLog::Warning:
        LOG_WARNING(Frontend, "{}", message);
        break;
    case HostLog::Error:
        LOG_ERROR(Frontend, "{}", message);
        break;
    }
}

namespace {
std::mutex g_gui_tasks_mutex;
std::deque<std::function<void()>> g_gui_tasks;

/// On the main thread, from a timer: what the emulator's threads asked for.
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
    // In the emulator's user folder, beside sdmc and nand: reader.
    const QString directory =
        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir)) +
        QStringLiteral("reader");
    QDir().mkpath(directory);
    return QDir::cleanPath(directory);
}

QString ReaderSettingsFile() {
    // Beside the emulator's own settings, where the reader's have been since the first version.
    return QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir)) +
           QStringLiteral("reader.ini");
}

QString CacheDirectory() {
    const QString directory =
        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::CacheDir)) +
        QStringLiteral("/reader");
    QDir().mkpath(directory);
    return directory;
}

namespace {
bool g_pointer_said = false;
QPoint g_pointer_position;
} // namespace

QPoint PointerPosition() {
    return g_pointer_said ? g_pointer_position : QCursor::pos();
}

void SetPointerPositionForTests(const QPoint& global) {
    g_pointer_said = true;
    g_pointer_position = global;
    // The render window asks the system where the pointer is rather than the event.
    QCursor::setPos(global);
}

void RequestSurfaceGrab(const QString&) {
    // grab-window grabs the main window; the reader's own pictures are composed from the
    // render window (host.window_picture).
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

namespace {

constexpr int CaptureRetryMs = 60;
constexpr int CaptureTimeoutMs = 2500;
constexpr int CaptureTries = 20;

QMainWindow* g_main_window = nullptr;
Core::System* g_system = nullptr;
MainWindowHooks g_hooks;
ReaderController* g_controller = nullptr;
ThreeDsHome* g_home = nullptr;

bool Powered() {
    return g_system && g_system->IsPoweredOn();
}

bool GameRunning() {
    return g_hooks.is_running && g_hooks.is_running();
}

/// Where one of the console's screens (0 the upper, 1 the lower) is drawn in the render window.
QRectF ScreenRect(int screen) {
    GRenderWindow* window = g_hooks.render_window;
    const auto& layout = window->GetFramebufferLayout();
    const qreal ratio = window->windowPixelRatio();
    // A screen drawn on its side (the upright layouts) is not one whose text can be pointed
    // at: the places a profile reports are those of the screen lying as the console holds it.
    if (ratio <= 0 || !layout.is_rotated) {
        return {};
    }
    const bool lower = screen == 1;
    if (lower ? !layout.bottom_screen_enabled : !layout.top_screen_enabled) {
        return {};
    }
    const auto& rect = lower ? layout.bottom_screen : layout.top_screen;
    if (rect.GetWidth() == 0 || rect.GetHeight() == 0) {
        return {};
    }
    return QRectF(rect.left / ratio, rect.top / ratio, rect.GetWidth() / ratio,
                  rect.GetHeight() / ratio);
}

/// The upper screen where it is shown; the lower one when it is shown alone.
QRectF PictureRect() {
    const QRectF upper = ScreenRect(0);
    return upper.isValid() ? upper : ScreenRect(1);
}

bool SoftwareRenderer() {
    return Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::Software;
}

/// The game's two screens, one above the other as the console has them.
void GrabPicture(std::function<void(const QImage&)> callback, int tries_left) {
    // A paused game presents no frame, and a screenshot is taken when one is presented.
    if (!Powered() || !GameRunning()) {
        callback(QImage());
        return;
    }
    // The software renderer draws into the window itself and takes no requests for a
    // picture: the window is copied as it is.
    if (SoftwareRenderer()) {
        callback(g_hooks.render_window->grab().toImage());
        return;
    }
    auto& renderer = g_system->GPU().Renderer();
    if (renderer.IsScreenshotPending()) {
        if (tries_left <= 0) {
            callback(QImage());
            return;
        }
        QTimer::singleShot(CaptureRetryMs, g_controller,
                           [callback = std::move(callback), tries_left] {
                               GrabPicture(callback, tries_left - 1);
                           });
        return;
    }
    // At twice the screens' own size (400 wide): the reader scales a card's picture down.
    const Layout::FramebufferLayout layout = Layout::FrameLayoutFromResolutionScale(2, false);

    // The picture is written by the renderer's thread, and the answer may come after this
    // request was given up: both are kept alive by what is shared here.
    struct Pending {
        QImage image;
        std::function<void(const QImage&)> callback;
        bool done{};
    };
    auto pending = std::make_shared<Pending>();
    pending->image = QImage(QSize(static_cast<int>(layout.width), static_cast<int>(layout.height)),
                            QImage::Format_RGB32);
    pending->image.fill(Qt::black);
    pending->callback = std::move(callback);
    const auto finish = [pending](const QImage& image) {
        if (pending->done) {
            return;
        }
        pending->done = true;
        pending->callback(image);
    };
    renderer.RequestScreenshot(
        pending->image.bits(),
        [pending, finish](bool invert_y) {
            // On a thread of the renderer's making: the answer goes to the main thread.
            PostToGui([pending, finish, invert_y] {
                finish(invert_y ? pending->image.mirrored(false, true) : pending->image);
            });
        },
        layout);
    QTimer::singleShot(CaptureTimeoutMs, g_controller, [finish] { finish(QImage()); });
}

} // namespace

bool HostCommand(const QString& name, const QStringList& parts, const QString&,
                 const std::function<void(const QString&)>& reply, QObject*) {
    const auto ok = [&reply](const QString& text = {}) {
        reply(text.isEmpty() ? QStringLiteral("OK") : QStringLiteral("OK ") + text);
    };
    const auto number = [&parts](int index) { return parts.value(index).toDouble(); };
    if (name == QStringLiteral("grab-window")) {
        // The main window as it is drawn.
        const QString file = parts.mid(1).join(u' ');
        ok(g_main_window->grab().save(file) ? file : QStringLiteral("(not written)"));
    } else if (name == QStringLiteral("home")) {
        // The HOME menu: what its two screens show and what is chosen.
        ok(QString::fromUtf8(QJsonDocument(g_home->Describe()).toJson(QJsonDocument::Compact)));
    } else if (name == QStringLiteral("home-key")) {
        // A key to whatever has the focus, as the keyboard would send it: Left, Return, a.
        const QKeySequence sequence(parts.value(1));
        QWidget* target = QApplication::focusWidget();
        if (!target && !QApplication::activeModalWidget()) {
            // Without a display no window is given back the focus when another closes.
            target = g_home;
        }
        if (sequence.isEmpty() || !target) {
            reply(QStringLiteral("ERR unknown key, or nothing has the focus"));
            return true;
        }
        const int key = sequence[0].key();
        const QString text = parts.value(1).size() == 1 ? parts.value(1).toLower() : QString();
        QTimer::singleShot(0, target, [target, key, text] {
            QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(target, &down);
        });
        ok(QString::fromUtf8(target->metaObject()->className()));
    } else if (name == QStringLiteral("home-click")) {
        // A click in the home: X Y in its own coordinates.
        ThreeDsHome* home = g_home;
        const QPointF at(number(1), number(2));
        QTimer::singleShot(0, home, [home, at] {
            for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                QMouseEvent event(type, at, home->mapToGlobal(at), Qt::LeftButton,
                                  type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                                  Qt::NoModifier);
                QCoreApplication::sendEvent(home, &event);
            }
        });
        ok();
    } else {
        return false;
    }
    return true;
}

void Install(QMainWindow* main_window, Core::System& system, MainWindowHooks hooks) {
    if (g_controller) {
        return;
    }
    g_main_window = main_window;
    g_system = &system;
    g_hooks = std::move(hooks);
    ReaderSession::Instance();

    EmulatorHost host;
    host.is_powered_on = [] { return Powered(); };
    host.is_running = [] { return GameRunning(); };
    host.pause = [] {
        if (g_hooks.pause) {
            g_hooks.pause();
        }
    };
    host.resume = [] {
        if (g_hooks.resume) {
            g_hooks.resume();
        }
    };
    host.picture_rect = [] { return PictureRect(); };
    host.screen_rect = [](int screen) { return ScreenRect(screen); };
    host.grab_picture = [](std::function<void(const QImage&)> callback) {
        GrabPicture(std::move(callback), CaptureTries);
    };
    GRenderWindow* render_window = g_hooks.render_window;
    host.window_picture = [render_window] {
        // Only the software renderer draws into the window's own picture (as the app is run
        // without a display); a graphics card draws past it.
        return SoftwareRenderer() ? render_window->grab().toImage() : QImage();
    };
    host.set_pointer_filter = [render_window](ReaderPointerFilter* filter) {
        render_window->SetReaderPointerFilter(filter);
    };

    g_controller = new ReaderController(main_window, render_window, std::move(host));
    // The emulator's threads hand what they have to the main thread through this queue.
    auto* gui_tasks = new QTimer(g_controller);
    gui_tasks->setInterval(4);
    QObject::connect(gui_tasks, &QTimer::timeout, g_controller, [] { RunGuiTasks(); });
    gui_tasks->start();
    ReaderBridge::PrepareFiles();

    // Save sync, as in the other reader apps (its window in the Reader menu).
    InstallSaveSync(g_controller, main_window);

    // The console's look: its HOME menu in place of the game list, its colours in every window.
    g_home = new ThreeDsHome(main_window, g_hooks.central, g_hooks.game_list,
                             g_hooks.game_list_placeholder, g_controller);
    QMenu* menu = g_controller->Menu();
    menu->addSeparator();
    QAction* home_action = menu->addAction(QObject::tr("3DS &HOME menu as the game list"));
    home_action->setObjectName(QStringLiteral("reader_3ds_home"));
    home_action->setCheckable(true);
    home_action->setChecked(ThreeDsHome::Wanted());
    QObject::connect(home_action, &QAction::triggered, g_home,
                     [](bool wanted) { g_home->SetWanted(wanted); });
    QObject::connect(g_home, &ThreeDsHome::WantedChanged, home_action, &QAction::setChecked);
    QAction* look_action = menu->addAction(QObject::tr("3DS &colours in every window"));
    look_action->setObjectName(QStringLiteral("reader_3ds_look"));
    look_action->setCheckable(true);
    look_action->setChecked(ThreeDsLook::Wanted());
    QObject::connect(look_action, &QAction::triggered, g_controller, [](bool wanted) {
        ThreeDsLook::SetWanted(wanted);
        if (g_hooks.update_theme) {
            g_hooks.update_theme();
        }
        g_home->update();
    });

    ReaderControl::StartIfWanted(main_window, render_window, g_controller);
}

QMenu* Menu() {
    return g_controller->Menu();
}

QWidget* Overlay() {
    QWidget* overlay = g_controller ? g_controller->Overlay() : nullptr;
    return overlay && overlay->isVisible() ? overlay : nullptr;
}

void OnGameBooted(u64 program_id, const QString& title) {
    g_controller->OnGameBooted(QString::fromStdString(Reader::Emu::GameId(program_id)), title);
}

void OnGameStopped() {
    g_controller->OnGameStopped();
    TellHomeStats(); // after the session's last writes
}

void OnEmulationRunning(bool running) {
    g_controller->OnEmulationRunning(running);
}

void OnApplicationActive(bool active) {
    g_controller->OnApplicationActive(active);
}

bool ApplyLook(QMainWindow* main_window) {
    return ThreeDsLook::Apply(main_window);
}

} // namespace ReaderUi
