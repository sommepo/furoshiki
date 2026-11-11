// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The reader over the program's game window: what the shared reader windows ask of their
// surroundings (engine/reader/qt/host.h), answered with the core that runs the game, and what
// the game window calls (engine/reader/qt/install.h). The program's host.cpp, where each
// reader fork has its own for its emulator's window (Station Reader's, reader/qt/host.cpp, is
// the model: its emulator also draws into a window that is not Qt's).
//
// The program's window is Qt's, and so is the main loop: the game is drawn by the core into a
// native widget of that window (MainWindow's screen), from the core's own thread. Everything
// the core tells comes on that thread and reaches the windows through PostToGui.

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QMainWindow>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>

#include "core_session.h"
#include "library.h"
#include "reader/core/console.h"
#include "reader/core/crt.h"
#include "reader/core/emulator.h"
#include "reader/desktop/screen_text.h"
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_control.h"
#include "reader/qt/reader_controller.h"
#include "reader/qt/reader_session.h"

namespace ReaderUi {

void WriteLog(HostLog level, const std::string& message) {
    switch (level) {
    case HostLog::Info:
        qInfo("%s", message.c_str());
        break;
    case HostLog::Warning:
        qWarning("%s", message.c_str());
        break;
    case HostLog::Error:
        qCritical("%s", message.c_str());
        break;
    }
}

namespace {
std::mutex g_gui_tasks_mutex;
std::deque<std::function<void()>> g_gui_tasks;

/// On the main thread: what the core's threads asked for.
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
    // Nothing of Qt on the caller's thread (a core's): the main thread takes the tasks from
    // the list with a timer of its own (Install).
    std::scoped_lock lock{g_gui_tasks_mutex};
    g_gui_tasks.push_back(std::move(task));
}

QString ReaderDirectory() {
    // The program's own, in Furoshiki's folder: statistics, the backlog, profiles of the
    // user's own. (The reader settings and dictionaries are the shared ones beside it.)
    const QString directory = Home::SharedFolder() + QStringLiteral("/program/reader");
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

QMainWindow* g_window = nullptr;
QWidget* g_screen = nullptr;
ReaderController* g_controller = nullptr;
ReaderPointerFilter* g_pointer_filter = nullptr;
bool g_has_pointer = false;
Home::CoreSession* g_session = nullptr;
bool g_screen_shown = false;
/// The reader stopped the game for one of its own windows, and starts it again.
bool g_paused_by_reader = false;
bool g_program_paused = false;
unsigned g_crt_revision = ~0u;
void (*g_end_game)(void*) = nullptr;
void* g_end_game_context = nullptr;
void (*g_game_stopped)(void*) = nullptr;
void* g_game_stopped_context = nullptr;

bool GameLoaded() {
    return g_session != nullptr && g_session->Running();
}

/// The game is going: loaded, in front, not paused.
bool Playing() {
    return GameLoaded() && g_screen_shown && !g_session->Paused();
}

bool GameInFront() {
    return GameLoaded() && g_screen_shown && (!g_session->Paused() || g_paused_by_reader);
}

/// Where the game's picture is in the screen, in its own (Qt's) units: as the core says, or,
/// until it has, the console's shape in the middle.
QRectF PictureRect() {
    if (g_screen == nullptr || g_session == nullptr) {
        return {};
    }
    const qreal scale = g_screen->devicePixelRatioF();
    const Home::CoreSession::Rect rect = g_session->PictureRect();
    if (rect.width > 0 && rect.height > 0) {
        return QRectF(rect.x / scale, rect.y / scale, rect.width / scale, rect.height / scale);
    }
    const QRectF area(g_screen->rect());
    const qreal width = std::min(area.width(), area.height() * 4.0 / 3.0);
    const qreal height = width * 3.0 / 4.0;
    return QRectF(area.center().x() - width / 2, area.center().y() - height / 2, width, height);
}

/// Copies a picture the core handed over (on its thread) and makes it an image on the main
/// thread.
void PostPicture(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t stride,
                 std::function<void(const QImage&)> callback) {
    if (rgba == nullptr || width == 0 || height == 0) {
        PostToGui([callback] { callback(QImage()); });
        return;
    }
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * 4 * height);
    for (uint32_t y = 0; y < height; ++y) {
        std::copy_n(rgba + static_cast<std::size_t>(y) * stride, width * 4, pixels.data() + static_cast<std::size_t>(y) * width * 4);
    }
    PostToGui([pixels = std::move(pixels), width, height, callback] {
        const QImage image(pixels.data(), static_cast<int>(width), static_cast<int>(height),
                           static_cast<qsizetype>(width) * 4, QImage::Format_RGBA8888);
        callback(image.convertToFormat(QImage::Format_RGB32));
    });
}

/// The CRT picture's sliders go to the core that draws it (its own copy of the shader's
/// table), whenever they changed here.
void SendCrt(bool always) {
    if (g_session == nullptr) {
        return;
    }
    const unsigned revision = Reader::Crt::Revision();
    if (!always && revision == g_crt_revision) {
        return;
    }
    g_crt_revision = revision;
    g_session->ApplySettings({{READER_CORE_SETTING_CRT, Reader::Crt::Serialize()}});
}
} // namespace

QPoint PointerPosition() {
    return g_pointer_said ? g_pointer_position : QCursor::pos();
}

void SetPointerPositionForTests(const QPoint& global) {
    g_pointer_said = true;
    g_pointer_position = global;
}

std::optional<QByteArray> HostFetch(const QUrl& url, QString* error) {
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    std::optional<QByteArray> out;
    if (reply->error() == QNetworkReply::NoError) {
        out = reply->readAll();
    } else {
        *error = QCoreApplication::translate("ReaderUi", "Could not fetch %1 (%2)").arg(url.toString(), reply->errorString());
    }
    reply->deleteLater();
    return out;
}

void RequestSurfaceGrab(const QString& path) {
    // The window as the core drew it, saved on the main thread.
    if (g_session == nullptr) {
        return;
    }
    g_session->Grab(READER_CORE_GRAB_WINDOW, [path](const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t stride) {
        PostPicture(rgba, width, height, stride, [path](const QImage& image) {
            if (!image.isNull()) {
                image.save(path);
            }
        });
    });
}

namespace {
std::function<bool(const QString&, const QStringList&, const std::function<void(const QString&)>&)> g_program_commands;
} // namespace

void SetProgramCommands(
    std::function<bool(const QString&, const QStringList&, const std::function<void(const QString&)>&)> commands) {
    g_program_commands = std::move(commands);
}

bool HostCommand(const QString& name, const QStringList& parts, const QString& rest,
                 const std::function<void(const QString&)>& reply, QObject*) {
    const auto ok = [&reply](const QString& text = {}) {
        reply(text.isEmpty() ? QStringLiteral("OK") : QStringLiteral("OK ") + text);
    };
    if (name == QStringLiteral("dump")) {
        // dump PATH: the game's memory, region after region, for finding a game's text.
        if (Reader::Emu::DumpMemory(rest.toStdString())) {
            ok(rest);
        } else {
            reply(QStringLiteral("ERR no memory to write (no game?)"));
        }
    } else if (name == QStringLiteral("state")) {
        // state save|load SLOT: one of the core's savestates of the running game.
        if (HostState(parts.value(1) == QStringLiteral("save"), parts.value(2).toInt())) {
            ok();
        } else {
            reply(QStringLiteral("ERR no game, or no such slot"));
        }
    } else if (name == QStringLiteral("home")) {
        // home: the game ends and Furoshiki's page comes back (the program's menu does it).
        if (g_end_game == nullptr) {
            reply(QStringLiteral("ERR not here"));
            return true;
        }
        g_end_game(g_end_game_context);
        ok();
    } else if (name == QStringLiteral("speed")) {
        // speed N: the game's speed in percent (0: as fast as it goes).
        if (g_session == nullptr) {
            reply(QStringLiteral("ERR no core"));
            return true;
        }
        g_session->ApplySettings({{READER_CORE_SETTING_SPEED, parts.value(1).toStdString()}});
        ok();
    } else {
        return g_program_commands && g_program_commands(name, parts, reply);
    }
    return true;
}

bool HomeKey(const QString&) {
    return false; // Furoshiki's own page is worked with its own keys (its --script)
}

bool HostState(bool save, int slot) {
    if (!GameLoaded() || slot < 1 || slot > 10) {
        return false;
    }
    const reader_core_api& api = g_session->Api();
    const auto call = save ? api.save_state : api.load_state;
    return call != nullptr && call(slot) == 0;
}

void HomeClick(float, float) {}

void Install(QMainWindow* window, QWidget* screen) {
    if (g_controller != nullptr) {
        return;
    }
    g_window = window;
    g_screen = screen;
    // The core's threads' tasks, a few milliseconds apart at most.
    auto* tasks = new QTimer(window);
    tasks->setInterval(4);
    QObject::connect(tasks, &QTimer::timeout, window, [] {
        RunGuiTasks();
        SendCrt(false);
        // The reader hears when the game goes on or stands still, whoever stopped it.
        static bool was_running = false;
        const bool running = Playing();
        if (running != was_running && g_controller != nullptr) {
            was_running = running;
            g_controller->OnEmulationRunning(running);
        }
    });
    tasks->start();

    ReaderSession::Instance();
    // The text engine reports to the windows from here on.
    ReaderBridge::OnGameLoading();
    ReaderBridge::PrepareFiles();

    EmulatorHost host;
    host.is_powered_on = [] { return GameLoaded(); };
    host.is_running = [] { return Playing(); };
    // Around the reader's own windows, and when the user asks it to read what is on screen,
    // the game stands still with its picture up.
    host.pause = [] {
        if (Playing()) {
            g_session->Pause(true);
            g_paused_by_reader = true;
        }
    };
    host.resume = [] {
        if (g_paused_by_reader) {
            g_paused_by_reader = false;
            if (GameLoaded() && !g_program_paused) {
                g_session->Pause(false);
            }
        }
    };
    host.picture_rect = [] { return PictureRect(); };
    // A console with two screens: each where the core says it is drawn.
    // A console with one: where its 2D is when the core says (the PS2's wide picture keeps its 2D
    // to its own shape in the middle), else the picture.
    host.screen_rect = [](int screen) {
        if (g_screen == nullptr || g_session == nullptr) {
            return QRectF();
        }
        const qreal scale = g_screen->devicePixelRatioF();
        if (Reader::ConsoleTraits::LowerScreenWidth <= 0.0f) {
            const Home::CoreSession::Rect rect = g_session->ScreenRect(0);
            if (screen != 0) {
                return QRectF();
            }
            return rect.width > 0 && rect.height > 0
                       ? QRectF(rect.x / scale, rect.y / scale, rect.width / scale, rect.height / scale)
                       : PictureRect();
        }
        const Home::CoreSession::Rect rect = g_session->ScreenRect(screen);
        return rect.width > 0 && rect.height > 0
                   ? QRectF(rect.x / scale, rect.y / scale, rect.width / scale, rect.height / scale)
                   : QRectF();
    };
    host.grab_picture = [](std::function<void(const QImage&)> callback) {
        if (!GameLoaded() || !g_session->Grab(READER_CORE_GRAB_PICTURE,
                                              [callback](const uint8_t* rgba, uint32_t width, uint32_t height,
                                                         uint32_t stride) {
                                                  PostPicture(rgba, width, height, stride, callback);
                                              })) {
            callback(QImage());
        }
    };
    host.set_pointer_filter = [](ReaderPointerFilter* filter) {
        g_pointer_filter = filter;
        g_has_pointer = false;
    };
    g_controller = new ReaderController(window, screen, std::move(host));
    g_controller->OnApplicationActive(true);
    ReaderControl::StartIfWanted(window, screen, g_controller);
}

void UseSession(Home::CoreSession* session) {
    g_session = session;
    Reader::Emu::SetSession(session);
    if (session == nullptr) {
        return;
    }
    // The emulator's threads start and stop games and tell the engine; the engine tells the
    // windows. Nothing of Qt on those threads (PostToGui says why).
    session->SetGameCallbacks(
        [](const std::string& game_id, const std::string& title) {
            ReaderBridge::OnGameStarted(); // the profile goes in before the game's code runs
            PostToGui([game_id, title] {
                g_controller->OnGameBooted(QString::fromStdString(game_id), QString::fromStdString(title));
            });
        },
        [] {
            PostToGui([] {
                g_paused_by_reader = false;
                ReaderBridge::OnGameStopped();
                g_controller->OnGameStopped();
                if (g_game_stopped != nullptr) {
                    g_game_stopped(g_game_stopped_context);
                }
            });
        });
    // The layer follows the picture when the core moves it.
    session->SetPictureCallback([] {
        PostToGui([] {
            if (g_screen != nullptr) {
                // The controller puts its windows in place on the screen's own events.
                QCoreApplication::postEvent(g_screen, new QEvent(QEvent::Move));
            }
        });
    });
    SendCrt(true);
    // The layer follows the glass only where the core draws it: Vita3K's picture stays flat.
    const reader_core_info& info = session->Info();
    const bool glass = std::any_of(info.settings, info.settings + info.setting_count, [](const reader_core_setting& setting) {
        return std::string_view(setting.key) == READER_CORE_SETTING_CRT;
    });
    ReaderApp::PlacedText::SetBend(glass ? std::function<void(float&, float&)>([](float& x, float& y) { Reader::Crt::ToScreen(x, y); }) : nullptr);
}

void UseSource(ReadingSource* source) {
    if (g_controller != nullptr) {
        g_controller->Use(source);
    }
}

void SetScreenShown(bool shown) {
    g_screen_shown = shown;
}

void ProgramPaused(bool paused) {
    g_program_paused = paused;
    if (g_session == nullptr) {
        return;
    }
    if (paused) {
        g_session->Pause(true);
    } else if (!g_paused_by_reader || g_controller == nullptr) {
        g_session->Pause(false);
    } else {
        // The program's menu ended what the reader began, too.
        g_paused_by_reader = false;
        g_session->Pause(false);
        g_controller->GamePauseEnded();
    }
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
    QTimer::singleShot(0, g_controller, [name] { g_controller->TriggerAction(name); });
    return true;
}

void SetEndGame(void (*end_game)(void*), void* context) {
    g_end_game = end_game;
    g_end_game_context = context;
}

void SetGameStopped(void (*stopped)(void*), void* context) {
    g_game_stopped = stopped;
    g_game_stopped_context = context;
}

void Notify(const QString& text) {
    if (g_controller != nullptr) {
        g_controller->Notify(text);
    }
}

QList<int> PointerButtons() {
    QList<int> buttons{-1};
    for (const auto& button : Reader::ConsoleTraits::ReaderButtons) {
        if (!button.menu_only) {
            buttons.append(button.number);
        }
    }
    return buttons;
}

QString PointerButtonName(int button) {
    for (const auto& one : Reader::ConsoleTraits::ReaderButtons) {
        if (one.number == button) {
            return QString::fromLatin1(one.name);
        }
    }
    return QStringLiteral("None");
}

void SetPointerButton(int button) {
    auto& settings = ReaderSession::Instance().Settings();
    settings.SetValue(ReaderApp::ReaderSettings::KeyCursorButton, button);
    settings.Sync();
    if (g_controller != nullptr) {
        g_controller->TriggerAction(QStringLiteral("reader_apply_settings"));
    }
}

QMenu* Menu() {
    return g_controller != nullptr ? g_controller->Menu() : nullptr;
}

bool PointerPress(float x, float y, bool left_button) {
    if (!g_pointer_filter || !GameInFront()) {
        return false;
    }
    if (g_pointer_filter->OnPointerPress(QPointF(x, y), left_button)) {
        g_has_pointer = true;
        return true;
    }
    return false;
}

bool PointerMove(float x, float y) {
    if (!g_pointer_filter || !GameInFront()) {
        return false;
    }
    g_pointer_filter->OnPointerMove(QPointF(x, y));
    return g_has_pointer;
}

bool PointerRelease(float x, float y) {
    if (!g_has_pointer) {
        return false;
    }
    g_has_pointer = false;
    if (g_pointer_filter) {
        g_pointer_filter->OnPointerRelease(QPointF(x, y));
    }
    return true;
}

} // namespace ReaderUi
