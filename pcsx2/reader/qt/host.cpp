// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// The reader in the emulator's Qt frontend: what the windows ask of the emulator (host.h),
// answered with PCSX2, and the functions the frontend calls to have the reader at all.

#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QTimer>

#include "common/Console.h"
#include "pcsx2/Config.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/VMManager.h"

#include "reader/core/emulator.h"
#include <QJsonObject>
#include <QDateTime>
#include <QJsonDocument>
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/ps2_home.h"
#include "reader/qt/reader_control.h"
#include "reader/qt/reader_controller.h"
#include "reader/qt/reader_games.h"
#include "reader/qt/reader_link.h"
#include "reader/qt/save_sync_dialog.h"
#include "reader/save_sync/host.h"
#include "reader/qt/reader_session.h"

namespace ReaderUi {

void WriteLog(HostLog level, const std::string& message) {
    switch (level) {
    case HostLog::Info:
        Console.WriteLn("%s", message.c_str());
        break;
    case HostLog::Warning:
        Console.Warning("%s", message.c_str());
        break;
    case HostLog::Error:
        Console.Error("%s", message.c_str());
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
    // Beside the emulator's own folders (bios, memcards, sstates): reader.
    const QString directory =
        QString::fromStdString(EmuFolders::DataRoot) + QStringLiteral("/reader");
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
} // namespace

QPoint PointerPosition() {
    return g_pointer_said ? g_pointer_position : QCursor::pos();
}

void SetPointerPositionForTests(const QPoint& global) {
    g_pointer_said = true;
    g_pointer_position = global;
}

void RequestSurfaceGrab(const QString&) {
    // The game's picture is a window of the graphics card's here; the development channel
    // has the reader's own windows and the capture of a frame instead.
}

namespace {

ReaderPointerFilter* g_pointer_filter = nullptr;
bool g_has_pointer = false;
ReaderController* g_controller = nullptr;
Ps2Home* g_home = nullptr;
// Whether the game list's reading that ends next was cut short (GameListCut): a game started
// (PCSX2 stops reading the list then), the app closing, another reading begun. What it found is
// then not every game, and Reader Home keeps the games it was given last instead: a game started
// from Reader Home starts at once, before a network share's games are read, and the list it was
// told was empty until the app was next started on its own.
bool g_list_cut = false;
MainWindowHooks g_hooks;

bool GameRunning() {
    return VMManager::GetState() == VMState::Running;
}

/// The game's picture is up: going, or paused (the reader pauses a game so that what it
/// shows can be read, and its words are still to be clicked then).
bool GameShown() {
    const VMState state = VMManager::GetState();
    return state == VMState::Running || state == VMState::Paused;
}

/**
 * Where the game's picture is in the widget it is shown in, in the widget's own units: the
 * widget's whole area for "Stretch", otherwise the largest area of the chosen shape in its
 * middle. (needs proper testing: the emulator's own zoom, offsets and integer scaling are
 * not followed here)
 */
QRectF PictureRect(QWidget* widget) {
    if (!widget || !VMManager::HasValidVM() || widget->width() <= 0 || widget->height() <= 0) {
        return {};
    }
    const QRectF whole(0, 0, widget->width(), widget->height());
    qreal shape = 4.0 / 3.0;
    switch (EmuConfig.CurrentAspectRatio) {
    case AspectRatioType::Stretch:
        return whole;
    case AspectRatioType::R16_9:
        shape = 16.0 / 9.0;
        break;
    case AspectRatioType::R10_7:
        shape = 10.0 / 7.0;
        break;
    default:
        break;
    }
    qreal width = whole.width();
    qreal height = width / shape;
    if (height > whole.height()) {
        height = whole.height();
        width = height * shape;
    }
    return QRectF((whole.width() - width) / 2, (whole.height() - height) / 2, width, height);
}

/// What Reader Home is told of this app (reader_link.h).
const HomeLink::About& LinkAbout() {
    static const HomeLink::About about{QStringLiteral("pcsx2-reader"), QStringLiteral("PCSX2 Reader"),
                                       QStringLiteral("PlayStation 2"),
                                       {QStringLiteral("-batch"), QStringLiteral("-fullscreen"), QStringLiteral("--"),
                                        QStringLiteral("%ROM%")},
                                       {}};
    return about;
}

/// The reading statistics, for Reader Home's sums; and, with save sync on, to the service
/// beside the saves: every reading day of every game, whole each time, under an ID of this
/// install's own. Nothing is said when it does not arrive.
void TellHomeStats() {
    auto& session = ReaderSession::Instance();
    ReaderApp::ReaderDatabase* database = &session.Database();
    const bool send = SaveSync::Host::Started() && SaveSync::Host::Get().enabled;
    const std::string install = send ? session.Settings().StatsInstall().toStdString() : std::string{};
    const QString device = send ? QString::fromStdString(SaveSync::Host::Get().device) : QString{};
    session.AfterWrites([database, send, install, device] {
        QJsonObject record = database->StatsRecord();
        HomeLink::Write(LinkAbout(), nullptr, &record);
        if (!send) {
            return;
        }
        record.insert(QStringLiteral("app"), QStringLiteral("pcsx2-reader"));
#ifdef _WIN32
        record.insert(QStringLiteral("platform"), QStringLiteral("windows"));
#else
        record.insert(QStringLiteral("platform"), QStringLiteral("linux"));
#endif
        record.insert(QStringLiteral("device"), device);
        record.insert(QStringLiteral("sent"), QDateTime::currentMSecsSinceEpoch());
        SaveSync::Host::SendStats(install, QJsonDocument(record).toJson(QJsonDocument::Compact).toStdString());
    });
}

/// `whole`: the game list was read to its end, and its games are all there are.
void RefreshHome(bool whole) {
    if (!g_hooks.games) {
        return;
    }
    // The games whose text can be read, for Reader Home's library; Reader Home keeps the ones it
    // was told last while the list is not known whole.
    QList<HomeLink::Game> readable;
    for (const HomeGame& entry : g_hooks.games()) {
        if (Reader::Emu::HasProfile(entry.serial.toStdString())) {
            readable.append({entry.serial, entry.name, QDir::toNativeSeparators(entry.path),
                             QDir::toNativeSeparators(entry.cover)});
        }
    }
    HomeLink::Write(LinkAbout(), whole ? &readable : nullptr, nullptr);
    if (!g_home) {
        return;
    }
    QList<Ps2Home::Game> games;
    for (const HomeGame& entry : g_hooks.games()) {
        Ps2Home::Game game;
        game.path = entry.path;
        game.name = entry.name;
        game.serial = entry.serial;
        if (!entry.cover.isEmpty()) {
            game.picture.load(entry.cover);
        }
        game.readable = Reader::Emu::HasProfile(entry.serial.toStdString());
        games.append(game);
    }
    g_home->SetGames(games);
}

/// What the Reader menu says of an entry, without the mark of its shortcut letter.
QString MenuText(const QString& action_name) {
    if (const QAction* action = g_controller->Menu()->findChild<QAction*>(action_name)) {
        return action->text().remove(u'&').remove(QChar(0x2026));
    }
    return {};
}

void FillSettings() {
    using Setting = Ps2Home::Setting;
    const auto page = [](const QString& key, const QString& name, const QString& action,
                         std::function<QString()> value = {}) {
        return Setting{key, name, std::move(value),
                       [action] { g_controller->TriggerAction(action); }};
    };
    QList<Setting> settings;
    settings.append(page(QStringLiteral("statistics"), QObject::tr("Reading Statistics"),
                         QStringLiteral("reader_statistics"), [] {
                             // The Reader menu's entry says what was read today.
                             g_controller->TriggerAction(QStringLiteral("reader_refresh_menu"));
                             const QString text = MenuText(QStringLiteral("reader_statistics"));
                             const int open = text.indexOf(u'(');
                             return open < 0 ? QString() : text.mid(open + 1).remove(u')').trimmed();
                         }));
    settings.append(page(QStringLiteral("games"), QObject::tr("Game Scripts"),
                         QStringLiteral("reader_games"),
                         [] { return QString::number(Games::Supported().size()); }));
    settings.append(page(QStringLiteral("dictionaries"), QObject::tr("Dictionaries"),
                         QStringLiteral("reader_dictionaries")));
    settings.append(page(QStringLiteral("anki"), QObject::tr("Anki"), QStringLiteral("reader_anki")));
    settings.append(page(QStringLiteral("reader"), QObject::tr("Reader Settings"),
                         QStringLiteral("reader_settings")));
    settings.append(Setting{QStringLiteral("emulator"), QObject::tr("Emulator Settings"), {}, [] {
                                if (g_hooks.open) {
                                    g_hooks.open(QStringLiteral("settings"));
                                }
                            }});
    settings.append(Setting{QStringLiteral("controllers"), QObject::tr("Controllers"), {}, [] {
                                if (g_hooks.open) {
                                    g_hooks.open(QStringLiteral("controllers"));
                                }
                            }});
    settings.append(Setting{QStringLiteral("folders"), QObject::tr("Game Folders"), {}, [] {
                                if (g_hooks.add_game_folder) {
                                    g_hooks.add_game_folder();
                                }
                            }});
    settings.append(Setting{QStringLiteral("list"), QObject::tr("Game List"),
                            [] { return QObject::tr("This menu"); },
                            [] { g_home->SetWanted(false); }});
    g_home->SetSettings(settings);
}

} // namespace

bool PointerPress(const QPointF& position, bool left_button) {
    if (!g_pointer_filter || !GameShown()) {
        return false;
    }
    if (g_pointer_filter->OnPointerPress(position, left_button)) {
        g_has_pointer = true;
        return true;
    }
    return false;
}

bool PointerMove(const QPointF& position) {
    if (!g_pointer_filter || !GameShown()) {
        return false;
    }
    g_pointer_filter->OnPointerMove(position);
    return g_has_pointer;
}

bool PointerRelease(const QPointF& position) {
    if (!g_has_pointer) {
        return false;
    }
    g_has_pointer = false;
    if (g_pointer_filter) {
        g_pointer_filter->OnPointerRelease(position);
    }
    return true;
}

bool ApplyLook(QMainWindow* main_window) {
    return Ps2Look::Apply(main_window);
}

void GamesChanged() {
    if (g_controller) {
        Games::Forget();
        RefreshHome(!g_list_cut);
    }
}

void GameListCut(bool cut) {
    g_list_cut = cut;
}

void Install(QMainWindow* main_window, MainWindowHooks hooks) {
    if (g_controller) {
        return;
    }
    g_hooks = std::move(hooks);
    ReaderSession::Instance();
    // The text engine reports to the app from here on.
    ReaderBridge::OnGameLoading();

    EmulatorHost host;
    host.is_powered_on = [] { return VMManager::HasValidVM(); };
    host.is_running = [] { return GameRunning(); };
    host.pause = [] {
        if (g_hooks.set_paused && VMManager::GetState() == VMState::Running) {
            g_hooks.set_paused(true);
        }
    };
    host.resume = [] {
        if (g_hooks.set_paused && VMManager::GetState() == VMState::Paused) {
            g_hooks.set_paused(false);
        }
    };
    QWidget* picture_area = g_hooks.picture_area;
    host.picture_rect = [picture_area] { return PictureRect(picture_area); };
    host.grab_picture = [](std::function<void(const QImage&)> callback) {
        if (!VMManager::HasValidVM()) {
            callback(QImage());
            return;
        }
        // The graphics thread is asked for what the game shows and waited for on the
        // emulator's own thread, the only one that may wait for it: a second thread waiting
        // beside it can miss the one signal there is and wait for ever (seen with Persona 4,
        // where every later picture then hung too). The picture comes back to the main thread
        // through the reader's own queue.
        Reader::Emu::RunOnEmulator([callback = std::move(callback)] {
            u32 width = 0;
            u32 height = 0;
            std::vector<u32> pixels;
            QImage picture;
            if (MTGS::SaveMemorySnapshot(0, 0, true, false, &width, &height, &pixels) && width > 0 &&
                height > 0 && pixels.size() >= static_cast<std::size_t>(width) * height) {
                picture = QImage(reinterpret_cast<const uchar*>(pixels.data()), static_cast<int>(width),
                                 static_cast<int>(height), static_cast<qsizetype>(width) * 4,
                                 QImage::Format_RGBX8888)
                              .convertToFormat(QImage::Format_RGB32);
            }
            PostToGui([callback, picture] { callback(picture); });
        });
    };
    host.set_pointer_filter = [](ReaderPointerFilter* filter) {
        g_pointer_filter = filter;
        g_has_pointer = false;
    };

    g_controller = new ReaderController(main_window, picture_area, std::move(host));
    main_window->menuBar()->addMenu(g_controller->Menu());

    // The emulator starts and stops games on its own thread and tells the engine; the engine
    // tells us there. Nothing of Qt in these: plain strings go over to the main thread.
    auto* gui_tasks = new QTimer(g_controller);
    gui_tasks->setInterval(4);
    QObject::connect(gui_tasks, &QTimer::timeout, g_controller, [] { RunGuiTasks(); });
    gui_tasks->start();
    ReaderBridge::PrepareFiles();
    Reader::Emu::SetGameLoadedCallback([](const std::string& game_id, const std::string& title) {
        ReaderBridge::OnGameStarted(); // the profile goes in before the next frame is read
        PostToGui([game_id, title] {
            g_controller->OnGameBooted(QString::fromStdString(game_id), QString::fromStdString(title));
        });
    });
    Reader::Emu::SetGameStoppedCallback([] {
        PostToGui([] {
            ReaderBridge::OnGameStopped();
            g_controller->OnGameStopped();
            TellHomeStats();
        });
    });
    // A game that can be read: a bundled profile, or one of the user's.
    Reader::Emu::SetProfileCheck([](const std::string& game_id) {
        const QString name = QString::fromStdString(game_id) + QStringLiteral(".lua");
        return QFile::exists(QStringLiteral(":/reader/profiles/") + name) ||
               QFile::exists(ReaderSession::DataDirectory() + QStringLiteral("/profiles/") + name);
    });

    // Whether the game is running (not paused) is asked.
    auto* watch = new QTimer(g_controller);
    watch->setInterval(150);
    QObject::connect(watch, &QTimer::timeout, g_controller, [] {
        static bool was = false;
        const bool now = GameRunning();
        if (now != was) {
            was = now;
            g_controller->OnEmulationRunning(now);
        }
    });
    watch->start();
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, g_controller,
                     [](Qt::ApplicationState state) {
                         g_controller->OnApplicationActive(state == Qt::ApplicationActive);
                     });

    // The console's look: its system menu in place of the game list, its colours in every
    // window. Both from the Reader menu too.
    g_home = new Ps2Home(main_window, g_hooks.game_list);
    g_home->start_game = [](const QString& path) {
        if (g_hooks.start_game) {
            g_hooks.start_game(path);
        }
    };
    g_home->add_games = [] {
        if (g_hooks.add_game_folder) {
            g_hooks.add_game_folder();
        }
    };
    FillSettings();
#ifndef READER_NO_SAVE_SYNC
    // Save sync's page, in the Reader menu (not in a published build).
    Reader::Emu::EnsureSaveSync();
    g_controller->AddWindowEntry(QObject::tr("Save &sync…"), QStringLiteral("reader_save_sync"),
                                 [](QWidget* parent) {
                                     SaveSyncDialog dialog(parent);
                                     dialog.exec();
                                 });
#endif
    // The game list is not read yet: Reader Home keeps what it was told last.
    RefreshHome(false);
    TellHomeStats();
    // A game's stop is not the only moment the statistics go out: a window closed with its game
    // still running stops nothing, and so sent nothing; and right now save sync may not be up yet.
    // Also soon after the app starts (what the last run counted) and every few minutes of a game.
    QTimer::singleShot(10000, g_controller, [] { TellHomeStats(); });
    auto* stats_timer = new QTimer(g_controller);
    stats_timer->setInterval(5 * 60 * 1000);
    QObject::connect(stats_timer, &QTimer::timeout, g_controller, [] {
        if (ReaderSession::Instance().IsActive()) {
            TellHomeStats();
        }
    });
    stats_timer->start();
    QMenu* menu = g_controller->Menu();
    menu->addSeparator();
    QAction* home_action = menu->addAction(QObject::tr("PS2 system &menu as the game list"));
    home_action->setObjectName(QStringLiteral("reader_ps2_home"));
    home_action->setCheckable(true);
    home_action->setChecked(Ps2Home::Wanted());
    QObject::connect(home_action, &QAction::triggered, g_home, [](bool wanted) { g_home->SetWanted(wanted); });
    QObject::connect(g_home, &Ps2Home::WantedChanged, home_action, &QAction::setChecked);
    QAction* look_action = menu->addAction(QObject::tr("PS2 &colours in every window"));
    look_action->setObjectName(QStringLiteral("reader_ps2_look"));
    look_action->setCheckable(true);
    look_action->setChecked(Ps2Look::Wanted());
    QObject::connect(look_action, &QAction::triggered, g_controller, [](bool wanted) {
        Ps2Look::SetWanted(wanted);
        if (g_hooks.update_theme) {
            g_hooks.update_theme();
        }
        g_home->update();
    });

    ReaderControl::StartIfWanted(main_window, picture_area, g_controller);
}

Ps2Home* Home() {
    return g_home;
}

} // namespace ReaderUi
