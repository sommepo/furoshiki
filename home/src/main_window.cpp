// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdlib>
#include <QApplication>
#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QPainter>
#include <QProxyStyle>
#include <QUrl>
#include <QIODevice>
#include <QMediaDevices>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>
#include <QSettings>
#include <QSet>
#include <QThreadPool>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QStackedWidget>
#include <SDL.h>
#include "home_window.h"
#include "library.h"
#include "app_folders.h"
#include "main_window.h"
#include "old_settings.h"
#include "reader/core/input_filter.h"
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_link.h"
#include "reader/qt/reader_session.h"
#include "reader/qt/lookup_popup.h"
#include "popup_style.h"
#include "settings_window.h"
#ifndef HOME_PUBLIC
#include "feedback_dialog.h"
#endif
#include "reader/qt/reader_look.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/qt/save_sync_dialog.h"
#include "reader/save_sync/host.h"
#endif

namespace Home {

namespace {

/// The program's names for a controller's buttons and the console's names they stand for, by
/// place on the pad (the lower of the four face buttons is "south"): the first name a core
/// lists is the one. A console's own names come with its core's description.
struct Role {
    const char* names[3];
    int key;        ///< On the keyboard
    int sdl;        ///< SDL_GameControllerButton, or -1 (a trigger: below)
    int filter;     ///< The reader's number for it (console.h's Pad* and ReaderButtons), or -1
};
constexpr Role Roles[] = {
    {{"Up"}, Qt::Key_Up, SDL_CONTROLLER_BUTTON_DPAD_UP, 19},
    {{"Down"}, Qt::Key_Down, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 20},
    {{"Left"}, Qt::Key_Left, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 21},
    {{"Right"}, Qt::Key_Right, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 22},
    {{"Cross", "A"}, Qt::Key_Z, SDL_CONTROLLER_BUTTON_A, 96},
    {{"Circle", "B"}, Qt::Key_X, SDL_CONTROLLER_BUTTON_B, 97},
    {{"Square", "X"}, Qt::Key_A, SDL_CONTROLLER_BUTTON_X, 99},
    {{"Triangle", "Y"}, Qt::Key_S, SDL_CONTROLLER_BUTTON_Y, 100},
    {{"L1", "L"}, Qt::Key_Q, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 102},
    {{"R1", "R"}, Qt::Key_W, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 103},
    {{"L2", "ZL"}, Qt::Key_1, -1, 104},
    {{"R2", "ZR"}, Qt::Key_2, -1, 105},
    {{"L3", "LStick"}, Qt::Key_3, SDL_CONTROLLER_BUTTON_LEFTSTICK, 106},
    {{"R3", "RStick"}, Qt::Key_4, SDL_CONTROLLER_BUTTON_RIGHTSTICK, 107},
    {{"Start", "Plus"}, Qt::Key_Return, SDL_CONTROLLER_BUTTON_START, 108},
    {{"Select", "Minus"}, Qt::Key_Backspace, SDL_CONTROLLER_BUTTON_BACK, 109},
};

/// The console's bit for a role, by the core's names for its buttons; -1 where it has none.
int BitOf(const reader_core_info& info, const Role& role) {
    for (const char* name : role.names) {
        if (name == nullptr) {
            break;
        }
        for (uint32_t i = 0; i < info.button_count; ++i) {
            if (info.buttons[i].name != nullptr && QString::fromUtf8(info.buttons[i].name).compare(
                                                       QLatin1String(name), Qt::CaseInsensitive) == 0) {
                return info.buttons[i].bit;
            }
        }
    }
    return -1;
}

/// The program's own data, in Furoshiki's folder: each core's folders below it.
QString ProgramFolder() {
    return SharedFolder() + QStringLiteral("/program");
}

/// The title a game's profile gives it ("-- reader: title 弟切草 蘇生篇"), or empty.
QString ProfileTitle(const QString& game_id) {
    const auto source = ReaderUi::ReaderBridge::ProfileSource(game_id);
    if (!source) {
        return {};
    }
    for (const QByteArray& line : source->split('\n')) {
        const QByteArray text = line.trimmed();
        if (!text.startsWith("--")) {
            break; // only the comments the file begins with
        }
        if (text.startsWith("-- reader: title ")) {
            return QString::fromUtf8(text.mid(17)).trimmed();
        }
    }
    return {};
}

/// A core's description of its console, for the settings' pages and layers.
PlayConsole ConsoleOf(const reader_core_info& info) {
    PlayConsole console{QString::fromUtf8(info.key), QString::fromUtf8(info.short_name ? info.short_name : info.key),
                        QString::fromUtf8(info.app_key), {}};
    for (uint32_t i = 0; info.settings != nullptr && i < info.setting_count; ++i) {
        const reader_core_setting& given = info.settings[i];
        PlaySetting setting;
        setting.key = QString::fromUtf8(given.key);
        setting.label = given.label != nullptr ? QString::fromUtf8(given.label) : QString();
        setting.kind = static_cast<int>(given.kind);
        setting.default_value = given.default_value != nullptr ? QString::fromUtf8(given.default_value) : QString();
        setting.low = given.low;
        setting.high = given.high;
        setting.step = given.step;
        for (uint32_t c = 0; given.choices != nullptr && c < given.choice_count; ++c) {
            setting.choices.append(QString::fromUtf8(given.choices[c]));
        }
        console.settings.append(setting);
    }
    return console;
}

} // namespace

// ---- Sound ----

/// What a core hands over to be played, kept until the sound output asks for it: written on
/// the core's thread, read on the sound thread (where the output lives). A fifth of a second at
/// most: what comes beyond that pushes the oldest out (the output fell behind).
class SoundOut final : public QIODevice {
public:
    // A core makes its sound by its own clock, the output plays it by the sound card's: the two
    // never quite agree, so what waits grows or shrinks. Cutting what grew too much was a click,
    // and what waited too long made the sound late. Instead each piece is played a hair faster or
    // slower (at most MaxStretch, which nobody hears) so that what waits stays near TargetMs.
    void Push(const int16_t* samples, size_t frames, uint32_t rate) {
        std::scoped_lock lock{mutex};
        if (rate != sound_rate) {
            sound_rate = rate;
            buffer.clear();
            primed = false;
            phase = 0;
        }
        const double target = rate * TargetMs / 1000.0;
        const double waiting = static_cast<double>(buffer.size() / 4);
        // Above the target: more input per output frame, fewer frames out.
        const double step = 1.0 + std::clamp((waiting - target) / target, -1.0, 1.0) * MaxStretch;
        const auto at = [&](qint64 i, int channel) -> double {
            return i == 0 ? last[channel] : samples[(i - 1) * 2 + channel];
        };
        QByteArray out;
        out.reserve(static_cast<qsizetype>(frames / step + 2) * 4);
        double position = phase; // 0 is the last frame of the piece before
        const auto count = static_cast<qint64>(frames);
        while (position < count) {
            const auto i = static_cast<qint64>(position);
            const double fraction = position - i;
            for (int channel = 0; channel < 2; ++channel) {
                const double value = at(i, channel) * (1 - fraction) + at(i + 1, channel) * fraction;
                const auto sample = static_cast<int16_t>(std::clamp(value, -32768.0, 32767.0));
                out.append(reinterpret_cast<const char*>(&sample), 2);
            }
            position += step;
        }
        phase = position - count;
        if (count > 0) {
            last[0] = samples[(count - 1) * 2];
            last[1] = samples[(count - 1) * 2 + 1];
        }
        buffer.append(out);
        given += count;
        // Only if the two clocks are far apart (a core paused, a hitch) is anything cut.
        const qsizetype most = static_cast<qsizetype>(rate) * 4 * MostMs / 1000 / 4 * 4;
        if (buffer.size() > most) {
            buffer.remove(0, (buffer.size() - most) / 4 * 4);
            ++cuts;
        }
        Account();
    }
    /// Once a minute, in the log: how often the sound ran dry or had to be cut, and how much
    /// waited; what a click on someone's computer is to be read from.
    void Account() {
        if (!counted.isValid()) {
            counted.start();
            return;
        }
        if (counted.elapsed() < 60000) {
            return;
        }
        if (dry > 0 || cuts > 0) {
            // What the core gave, against its rate: below it, the core is slower than the console.
            qInfo("sound: ran dry %d times, cut %d times in the last minute (%lld ms waiting; the core gave "
                  "%lld frames a second at %u)",
                  dry, cuts, static_cast<long long>(buffer.size() / 4 * 1000 / std::max<uint32_t>(sound_rate, 1)),
                  static_cast<long long>(given * 1000 / std::max<qint64>(counted.elapsed(), 1)), sound_rate);
        }
        dry = 0;
        given = 0;
        cuts = 0;
        counted.restart();
    }
    void Clear() {
        std::scoped_lock lock{mutex};
        buffer.clear();
        primed = false;
    }
    /// The output played all it had (its state went idle): it waits for the cushion again.
    void RanDry() {
        std::scoped_lock lock{mutex};
        if (primed) {
            ++dry;
            primed = false;
        }
    }

protected:
    // A core's sound comes in pieces, a frame's at a time, and not evenly (on Windows a core's
    // frames often come two at once, a thirtieth of a second apart). The output's own buffer
    // (a tenth of a second) is the cushion for that: it is handed only the sound there is, never
    // silence in its place, which was a gap whenever a piece came late (the Z13 heard it some
    // eight times a second, 2026-10-30). Nothing is handed over until CushionMs waits, at the
    // start and again after the output ran dry (RanDry), and then it fades in (FadeFrames).
    qint64 readData(char* data, qint64 size) override {
        std::scoped_lock lock{mutex};
        if (!primed) {
            return 0;
        }
        const qint64 have = std::min<qint64>(size / 4 * 4, buffer.size());
        std::memcpy(data, buffer.constData(), static_cast<size_t>(have));
        buffer.remove(0, have);
        if (starting) {
            auto* frames = reinterpret_cast<int16_t*>(data);
            const qint64 fade = std::min<qint64>(FadeFrames, have / 4);
            for (qint64 i = 0; i < fade; ++i) {
                frames[i * 2] = static_cast<int16_t>(frames[i * 2] * i / fade);
                frames[i * 2 + 1] = static_cast<int16_t>(frames[i * 2 + 1] * i / fade);
            }
            starting = have == 0;
        }
        return have;
    }
    qint64 writeData(const char*, qint64) override {
        return -1;
    }
    [[nodiscard]] qint64 bytesAvailable() const override {
        std::scoped_lock lock{mutex};
        if (!primed) {
            const qint64 cushion = static_cast<qint64>(sound_rate) * 4 * CushionMs / 1000 / 4 * 4;
            if (buffer.size() < cushion) {
                return QIODevice::bytesAvailable();
            }
            primed = true;
            starting = true;
        }
        return buffer.size() / 4 * 4 + QIODevice::bytesAvailable();
    }

private:
    static constexpr qint64 CushionMs = 80;
    static constexpr qint64 FadeFrames = 64;
    // What waits here beyond the output's own buffer, which it keeps full.
    static constexpr double TargetMs = 20;
    static constexpr qint64 MostMs = 250;
    static constexpr double MaxStretch = 0.005;
    double phase{};
    double last[2]{};
    int dry{};
    int cuts{};
    qint64 given{}; ///< Frames the core gave since the last account
    QElapsedTimer counted;
    mutable std::mutex mutex;
    mutable bool starting{};
    QByteArray buffer;
    uint32_t sound_rate{44100};
    mutable bool primed{};
};

void MainWindow::PlaySound(const int16_t* samples, size_t frames, uint32_t rate) {
    if (sound == nullptr) {
        return;
    }
    sound->Push(samples, frames, rate);
    if (rate != sink_rate.exchange(rate)) {
        // The output is made (again) for the console's rate, on its own thread.
        QMetaObject::invokeMethod(sound, [this, rate] {
            if (sink != nullptr) {
                sink->stop();
                delete sink;
                sink = nullptr;
            }
            QAudioFormat format;
            format.setSampleRate(static_cast<int>(rate));
            format.setChannelCount(2);
            format.setSampleFormat(QAudioFormat::Int16);
            const QAudioDevice device = QMediaDevices::defaultAudioOutput();
            if (device.isNull() || !device.isFormatSupported(format)) {
                return; // no sound to be had here
            }
            sink = new QAudioSink(device, format);
            // A tenth of a second at the sound card, kept full; SoundOut keeps the rest.
            sink->setBufferSize(static_cast<qsizetype>(rate) * 4 / 10 / 4 * 4);
            // Idle: it played everything it was given and had nothing more.
            QObject::connect(sink, &QAudioSink::stateChanged, sound, [this](QAudio::State state) {
                if (state == QAudio::IdleState) {
                    sound->RanDry();
                }
            });
            sink->start(sound);
        }, Qt::QueuedConnection);
    }
}

// ---- The screen ----

GameScreen::GameScreen(QWidget* parent) : QWidget(parent) {
    // A window of its own for the core's renderer, which nothing of Qt's paints over.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void GameScreen::SetButtons(const reader_core_info& info) {
    key_bits.clear();
    for (const Role& role : Roles) {
        const int bit = BitOf(info, role);
        if (bit >= 0) {
            key_bits[role.key] = bit;
        }
    }
    // Space for going on, as the reader apps have it on a keyboard.
    if (const auto yes = info.button_yes; yes >= 0) {
        key_bits[Qt::Key_Space] = yes;
    }
    key_buttons = 0;
}

void GameScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    emit Resized();
}

void GameScreen::keyPressEvent(QKeyEvent* event) {
    if ((event->modifiers() & Qt::ControlModifier) != 0 && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z &&
        ReaderUi::Shortcut('a' + (event->key() - Qt::Key_A))) {
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        emit MenuWanted();
        return;
    }
    if (event->key() == Qt::Key_F1) {
        emit ReaderMenuWanted();
        return;
    }
    if (event->key() == Qt::Key_F11) {
        emit FullScreenWanted();
        return;
    }
    if ((event->key() == Qt::Key_F5 || event->key() == Qt::Key_F8) && !event->isAutoRepeat()) {
        emit QuickStateWanted(event->key() == Qt::Key_F8);
        return;
    }
    const auto found = key_bits.find(event->key());
    if (found == key_bits.end()) {
        QWidget::keyPressEvent(event);
        return;
    }
    key_buttons |= uint64_t{1} << found->second;
}

void GameScreen::keyReleaseEvent(QKeyEvent* event) {
    const auto found = key_bits.find(event->key());
    if (found == key_bits.end() || event->isAutoRepeat()) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    key_buttons &= ~(uint64_t{1} << found->second);
}

void GameScreen::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() == Qt::RightButton) {
        emit ReaderMenuWanted();
        return;
    }
    const auto x = static_cast<float>(event->position().x()), y = static_cast<float>(event->position().y());
    // The reader first (a word clicked); what it leaves is the game's, where it takes a pointer
    // there: the middle button is a light gun's second (its reload).
    if (event->button() == Qt::LeftButton && ReaderUi::PointerPress(x, y, true)) {
        return;
    }
    const uint32_t flags = READER_CORE_POINTER_PRESSED |
                           (event->button() == Qt::MiddleButton ? READER_CORE_POINTER_SECONDARY : 0u);
    if (pointer_sink && pointer_sink(event->position(), flags)) {
        game_pointer = flags;
    }
}

void GameScreen::mouseMoveEvent(QMouseEvent* event) {
    const bool kept = ReaderUi::PointerMove(static_cast<float>(event->position().x()), static_cast<float>(event->position().y()));
    if (!kept && pointer_sink) {
        pointer_sink(event->position(), game_pointer);
    }
}

void GameScreen::mouseReleaseEvent(QMouseEvent* event) {
    const bool kept =
        ReaderUi::PointerRelease(static_cast<float>(event->position().x()), static_cast<float>(event->position().y()));
    if (!kept && game_pointer != 0) {
        game_pointer = 0;
        if (pointer_sink) {
            pointer_sink(event->position(), 0);
        }
    }
}

// ---- The window ----

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Furoshiki"));
    pages = new QStackedWidget(this);
    home = new HomeWindow(pages);
    screen = new GameScreen(pages);
    pages->addWidget(home);
    pages->addWidget(screen);
    setCentralWidget(pages);
    resize(1280, 720);
    setStyleSheet(QStringLiteral("QMainWindow { background: black; }"));

    sound = new SoundOut;
    sound->open(QIODevice::ReadOnly);
    sound_thread.setObjectName(QStringLiteral("sound"));
    sound->moveToThread(&sound_thread);
    sound_thread.start(QThread::TimeCriticalPriority);

    // The cores beside the program, in its "cores" folder, or where READER_HOME_CORES says.
    QStringList folders{QCoreApplication::applicationDirPath(), QCoreApplication::applicationDirPath() + QStringLiteral("/cores")};
    if (const QString given = qEnvironmentVariable("READER_HOME_CORES"); !given.isEmpty()) {
        folders.prepend(given);
    }
    for (const QString& folder : folders) {
        core_files += FindCores(folder);
    }
    play_settings = PlaySettings(ProgramFolder() + QStringLiteral("/settings.json"));
    // The old reader apps' own settings, the first time only: what was changed there becomes
    // that console's setting here.
    const QList<OldSettings::Value> old = OldSettings::ReadOnce(play_settings, QCoreApplication::applicationDirPath());
    for (const OldSettings::Value& value : old) {
        qInfo("from an old app's settings: %s %s = %s", qPrintable(value.console), qPrintable(value.key),
              qPrintable(value.value));
    }
    LoadAllCores();
    popup_folder = PopupStyle::Folder(ProgramFolder());
    AddPopupSettings();
    home->SetConsoles(consoles, &play_settings);
    // The popup's settings in the layers come before the reader's own: for the game playing
    // and its console, or all consoles' on the page.
    ReaderUi::ReaderSession::Instance().Settings().SetLayered([this](const QString& key) -> std::optional<QString> {
        if (!key.startsWith(QStringLiteral("reader_popup_"))) {
            return std::nullopt;
        }
        const bool running = current != nullptr;
        return PopupStyle::Layered(play_settings, popup_folder, key,
                                   running ? QString::fromUtf8(current->core.info->key) : QString(),
                                   running ? playing_id : QString());
    });
    home->SetSettingsWindow([this](const QString& tab) { OpenSettings(tab); });
    // L2 was the pointer's by default, and the Reader settings window wrote that default down
    // on saving, so a stored L2 says nothing of a choice. Once, it goes back to the games.
    if (QSettings program; !program.contains(QStringLiteral("pointer_button_left_to_games"))) {
        program.setValue(QStringLiteral("pointer_button_left_to_games"), true);
        if (ReaderUi::ReaderSession::Instance().Settings().CursorButton() == 104) {
            ReaderUi::SetPointerButton(-1);
        }
    }
    home->SetSettingStep([](const QString& row, int step) {
        if (row == QStringLiteral("Pointer button")) {
            const QList<int> buttons = ReaderUi::PointerButtons();
            const int now = buttons.indexOf(ReaderUi::ReaderSession::Instance().Settings().CursorButton());
            ReaderUi::SetPointerButton(buttons[(std::max(now, 0) + step + buttons.size()) % buttons.size()]);
        }
    });
    home->SetSettingValue([this](const QString& row) -> QString {
        if (row == QStringLiteral("RetroAchievements")) {
            const QString user = play_settings.Raw(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_USER));
            if (!user.isEmpty()) {
                return user.toUpper();
            }
            return qEnvironmentVariable("READER_RA_USER").isEmpty() ? QStringLiteral("OFF") : QStringLiteral("FRONT END'S");
        }
        if (row == QStringLiteral("Text server")) {
            auto& settings = ReaderUi::ReaderSession::Instance().Settings();
            return settings.TextHookEnabled() ? QStringLiteral(":%1").arg(settings.TextHookPort()) : QStringLiteral("OFF");
        }
        if (row == QStringLiteral("Pointer button")) {
            return ReaderUi::PointerButtonName(ReaderUi::ReaderSession::Instance().Settings().CursorButton()).toUpper();
        }
        return {};
    });
    home->SetPopupStyle(
        [this](const QString& console, const QString& game) {
            auto& settings = ReaderUi::ReaderSession::Instance().Settings();
            const QJsonObject style = PopupStyle::Export(play_settings, popup_folder, console, game, [&settings](const QString& key) {
                return settings.Value(key).toString();
            });
            const QString name = (console.isEmpty() ? QStringLiteral("all-consoles") : console) +
                                 (game.isEmpty() ? QString() : QLatin1Char('-') + game) + QStringLiteral("-popup-style.json");
            const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Export the popup style"),
                                                              popup_folder + QLatin1Char('/') + name,
                                                              QStringLiteral("Popup style (*.json)"));
            if (file.isEmpty()) {
                return QString();
            }
            QSaveFile out(file);
            if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(style).toJson()) < 0 || !out.commit()) {
                return QStringLiteral("The style could not be written to %1").arg(QDir::toNativeSeparators(file));
            }
            return QStringLiteral("Popup style written to %1").arg(QDir::toNativeSeparators(file));
        },
        [this](const QString& console, const QString& game) {
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import a popup style"), popup_folder,
                                                              QStringLiteral("Popup style (*.json)"));
            if (file.isEmpty()) {
                return QString();
            }
            QFile in(file);
            const QJsonObject style = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
            const QString failed = PopupStyle::Import(style, play_settings, popup_folder,
                                                      QFileInfo(file).completeBaseName().remove(QStringLiteral("-popup-style")),
                                                      console, game);
            if (!failed.isEmpty()) {
                return QStringLiteral("Not imported: %1").arg(failed);
            }
            AddPopupSettings(); // its CSS is a new choice
            home->SetConsoles(consoles, &play_settings);
            return QStringLiteral("Popup style imported for %1").arg(console.isEmpty() ? QStringLiteral("all consoles") : console);
        });
#ifndef READER_NO_SAVE_SYNC
    home->SetSaveSync(
        [] {
            if (!SaveSync::Host::Started()) {
                return QStringLiteral("NONE");
            }
            return SaveSync::Host::Get().enabled ? QStringLiteral("ON") : QStringLiteral("OFF");
        },
        [this] { OpenSaveSync(); });
#endif
    home->SetConsoleSetup([this](const QString& key) { return SetupOf(key); },
                          [this](const QString&) { FindGames(); });
    // The games of the consoles played here, listed by Furoshiki's page as an app's are.
    QTimer::singleShot(0, this, [this] { FindGames(); });
    home->SetCorePlayer([this](const QString& app, const QString& path) {
        QString error;
        if (!Plays(app)) {
            return false;
        }
        if (!Play(path, app, error)) {
            home->Notify(error);
        }
        return true;
    });

    connect(screen, &GameScreen::Resized, this, [this] { SendWindow(); });
    connect(screen, &GameScreen::MenuWanted, this, [this] { OpenMenu(); });
    connect(screen, &GameScreen::ReaderMenuWanted, this, [this] {
        if (QMenu* menu = ReaderUi::Menu()) {
            menu->popup(screen->mapToGlobal(screen->rect().center()));
        }
    });
    connect(screen, &GameScreen::FullScreenWanted, this, [this] { ToggleFullScreen(); });
    connect(screen, &GameScreen::QuickStateWanted, this, [this](bool load) {
        load ? LoadStateFrom(state_slot) : SaveStateTo(state_slot);
    });
    screen->SetPointerSink([this](const QPointF& at, uint32_t flags) { return PointerToGame(at, flags); });

    // The controllers, read here once for the console's first one (SDL's game controllers:
    // the Steam Controller's own gamepad too, which the reader's bridge makes on Windows).
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Qt has the program's events: SDL's own handling of SIGINT and SIGTERM (a quit event
    // nobody reads) would keep the program from ending when it is asked to.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    connect(&home->Steam(), &SteamPad::ButtonChanged, this, [this](int button, bool down) {
        if (button >= 0 && button < 32) {
            steam_buttons = down ? steam_buttons | (1U << button) : steam_buttons & ~(1U << button);
            steam_seen = true;
        }
    });
    connect(&home->Steam(), &SteamPad::AxisChanged, this, [this](int axis, int value) {
        if (axis >= 0 && axis < static_cast<int>(std::size(steam_axes))) {
            steam_axes[axis] = value;
            steam_seen = true;
        }
    });
    pads_timer.setInterval(8);
    connect(&pads_timer, &QTimer::timeout, this, [this] { PollPads(); });
    pads_timer.start();
#ifndef READER_NO_SAVE_SYNC
    // The statistics every few minutes of a game, as every reader app sends them (and soon
    // after a game starts, and when it stops: StartGame, SetGameStopped).
    stats_timer.setInterval(5 * 60 * 1000);
    connect(&stats_timer, &QTimer::timeout, this, [this] {
        if (current != nullptr && ReaderUi::ReaderSession::Instance().IsActive()) {
            SendStats();
        }
    });
    stats_timer.start();
#endif
}

MainWindow::~MainWindow() {
#ifndef READER_NO_SAVE_SYNC
    SaveSync::Host::Shutdown(); // what is still to be sent, a moment at most
#endif
    for (SDL_GameController* pad : sdl_pads) {
        SDL_GameControllerClose(pad);
    }
    for (auto& [file, core] : loaded) {
        if (core->session) { // a core loaded for its description only has none
            core->session->Stop();
        }
    }
    // The sound output and its thread, once no core plays into it any more.
    QMetaObject::invokeMethod(
        sound,
        [this] {
            if (sink != nullptr) {
                sink->stop();
                delete sink;
                sink = nullptr;
            }
            delete sound;
        },
        Qt::BlockingQueuedConnection);
    sound = nullptr;
    sound_thread.quit();
    sound_thread.wait();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (current != nullptr && current->session && current->session->Running()) {
        current->session->Stop();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::LoadAllCores() {
    for (const QString& file : std::as_const(core_files)) {
        if (loaded.find(file) != loaded.end()) {
            continue;
        }
        auto entry = std::make_unique<Loaded>();
        QString why;
        if (!LoadCore(file, entry->core, why)) {
            qWarning("%s", qPrintable(why));
            continue;
        }
        consoles.append(ConsoleOf(*entry->core.info));
        Loaded* started = loaded.emplace(file, std::move(entry)).first->second.get();
        if (!StartSession(started, why)) {
            qWarning("%s", qPrintable(why));
        }
    }
}

bool MainWindow::StartSession(Loaded* entry, QString& error) {
    // The core is started (its init) with its folders in the program's: once, whether a game of
    // it is played or only its files are looked at (a libretro-made core tells a disc only then).
    const reader_core_info& info = *entry->core.info;
    entry->session = std::make_unique<CoreSession>(entry->core);
    // Each console's folders together: program/consoles/<key>/data, saves, states, system (the
    // BIOS or firmware the user puts there). (A folder named after the console would be read
    // by some emulators as their own: PPSSPP takes a memory stick folder named "psp" for its
    // PSP folder.)
    const QString folder = ProgramFolder() + QStringLiteral("/consoles/") + QString::fromUtf8(info.key);
    const QByteArray data = (folder + QStringLiteral("/data")).toUtf8();
    QByteArray saves = (folder + QStringLiteral("/saves")).toUtf8();
    const QByteArray states = (folder + QStringLiteral("/states")).toUtf8();
    QByteArray system = (folder + QStringLiteral("/system")).toUtf8();
    for (const QByteArray& made : {data, saves, states, system}) {
        QDir().mkpath(QString::fromUtf8(made));
    }
    // The console's app's own saves, where it has them on this computer: the same files in the
    // app and here (app_folders.h); its BIOS, firmware or keys too while the program's system
    // folder is empty.
    QString program;
    for (const App& app : Load(SharedFolder()).apps) {
        program = app.app == QString::fromUtf8(info.app_key) ? app.program : program;
    }
    const AppFolders::Folders shared = AppFolders::Of(QString::fromUtf8(info.key), program);
    if (!shared.saves.isEmpty()) {
        saves = QDir::toNativeSeparators(shared.saves).toUtf8();
        qInfo("%s: the saves of %s, %s", info.key, info.app_name, qPrintable(shared.saves));
    }
    if (!shared.system.isEmpty() && !AppFolders::HasSystemFiles(QString::fromUtf8(info.key), QString::fromUtf8(system))) {
        system = QDir::toNativeSeparators(shared.system).toUtf8();
        qInfo("%s: the system files of %s, %s", info.key, info.app_name, qPrintable(shared.system));
    }
    system_folders[QString::fromUtf8(info.key)] = QString::fromUtf8(system);
    reader_core_paths paths{sizeof(reader_core_paths), data.constData(), saves.constData(), states.constData(),
                            system.constData()};
    std::string why;
    if (!entry->session->Start(paths, why)) {
        error = QString::fromStdString(why);
        entry->session.reset();
        return false;
    }
#ifndef READER_NO_SAVE_SYNC
    StartSaveSync(entry, QString::fromUtf8(saves), shared.sync, program);
#endif
    entry->session->SetPlaySink([this](const int16_t* samples, size_t frames, uint32_t rate) {
        PlaySound(samples, frames, rate);
    });
    entry->session->SetPadSource([this](uint32_t port, reader_core_pad& state) {
        if (port >= MaxPorts) {
            return;
        }
        std::scoped_lock lock{pads_mutex};
        state = gamepads[port];
        if (port == 0) { // the keyboard and the Steam Controller are the first controller's
            state.buttons |= screen->KeyButtons();
            state.connected = 1;
        }
    });
    entry->session->SetRumbleCallback([this](uint32_t port, uint16_t strong, uint16_t weak) {
        ReaderUi::PostToGui([this, port, strong, weak] { Rumble(port, strong, weak); });
    });
    entry->session->SetDiscCallback([](int32_t index, int32_t result) {
        ReaderUi::PostToGui([index, result] {
            ReaderUi::Notify(result != 0    ? QStringLiteral("The disc could not be changed")
                             : index < 0    ? QStringLiteral("The tray is open")
                                            : QStringLiteral("Disc %1 is in").arg(index + 1));
        });
    });
    // A core's message for the player (RetroAchievements' unlocks), where the game is shown.
    entry->session->SetNotifyCallback([this](const std::string& title, const std::string& text) {
        ReaderUi::PostToGui([this, line = QString::fromStdString(title.empty() ? text : text.empty() ? title : title + " — " + text)] {
            // A game that has ended (or never started) is on its way back to the page.
            const bool game_shown = pages->currentWidget() == screen && current != nullptr && !current->session->Ended();
            game_shown ? ReaderUi::Notify(line) : home->Notify(line);
        });
    });
    entry->session->SetStateCallback([](uint32_t what, int32_t slot, int32_t result, const std::string& message) {
        ReaderUi::PostToGui([what, slot, result, why = QString::fromStdString(message)] {
            if (result == 0) {
                ReaderUi::Notify(what == 0 ? QStringLiteral("Saved in slot %1").arg(slot)
                                           : QStringLiteral("Slot %1 loaded").arg(slot));
            } else {
                ReaderUi::Notify((what == 0 ? QStringLiteral("Slot %1 could not be written") : QStringLiteral("Slot %1 could not be loaded"))
                                     .arg(slot) +
                                 (why.isEmpty() ? QString() : QStringLiteral(": ") + why));
            }
        });
    });
    return true;
}

#ifndef READER_NO_SAVE_SYNC
void MainWindow::StartSaveSync(Loaded* entry, const QString& saves, const QString& app_sync, const QString& program) {
    const reader_core_info& info = *entry->core.info;
    const std::string key = info.key;
    if ((Features(info) & READER_CORE_HAS_SAVE_SYNC) == 0 || !SaveSync::Host::HasNaming(key)) {
        return;
    }
    // Save sync's own folder for the console: the app's, where the core keeps the app's saves
    // (both go on from the same notes on which version each save is), else the program's.
    const QString own = ProgramFolder() + QStringLiteral("/save-sync/") + QString::fromUtf8(info.key);
    const QString data = !app_sync.isEmpty() ? app_sync : own;
    QDir().mkpath(data);
    if (!sync_folders.contains(data)) {
        sync_folders.append(data);
    }
    // A console with no settings yet takes another's: the program's other consoles', then the
    // reader apps' (the first set up gives them).
    std::vector<std::string> settings_from;
    for (const char* other : {"ps1", "ps2", "psp", "dc", "3ds", "psvita"}) {
        const QString folder = ProgramFolder() + QStringLiteral("/save-sync/") + QLatin1String(other);
        if (folder != data) {
            settings_from.push_back(QDir::toNativeSeparators(folder).toStdString());
        }
    }
    for (const App& app : Load(SharedFolder()).apps) {
        for (const char* other : {"ps1", "ps2", "psp"}) {
            const QString folder = AppFolders::Of(QLatin1String(other), app.program).sync;
            if (!folder.isEmpty() && folder != data && QFileInfo(folder).isDir()) {
                settings_from.push_back(QDir::toNativeSeparators(folder).toStdString());
            }
        }
    }
    Q_UNUSED(program);
    // Where save sync looks for a game's saves in the core's saves folder: the PSP's are its
    // memory stick's PSP/SAVEDATA; the 3DS's its SD card's title folder (Azahar's libretro
    // build keeps its folders in Azahar/ of the saves folder); the Vita's are on its memory
    // card, in the core's data folder; a PlayStation's, a PS2's and a
    // Dreamcast's a folder per game in it.
    QString savedata = saves;
    if (key == "psp") {
        savedata = saves + QStringLiteral("/PSP/SAVEDATA");
    } else if (key == "3ds") {
        const QString id = QStringLiteral("00000000000000000000000000000000");
        savedata = saves + QStringLiteral("/Azahar/sdmc/Nintendo 3DS/%1/%1/title").arg(id);
    } else if (key == "psvita") {
        // Vita3K's own memory card, beside the saves folder: a game's folder in it holds its files.
        savedata = QFileInfo(saves).path() + QStringLiteral("/data/fs/ux0/user/00/savedata");
    }
    QDir().mkpath(savedata);
    SaveSync::Host::StartConsole(
        key, QDir::toNativeSeparators(savedata).toStdString(), QDir::toNativeSeparators(data).toStdString(), settings_from,
        [this](const std::string& text) {
            QMetaObject::invokeMethod(this, [this, line = QString::fromStdString(text)] {
                pages->currentWidget() == screen ? ReaderUi::Notify(line) : home->Notify(line);
            }, Qt::QueuedConnection);
        },
        QSysInfo::machineHostName().toStdString());
    qInfo("%s: save sync's notes in %s", info.key, qPrintable(data));
    // The core's moments: before the game opens its saves (waited for: the newest is fetched
    // meanwhile), each change of one, the game's end (a last send).
    entry->session->SetSavesOpeningCallback([key](const CoreSession::SavesOpening& opening) {
        if (SaveSync::ConsoleHost* host = SaveSync::Host::Of(key)) {
            host->GameStarting(opening.game_id, opening.title);
        }
        return 0;
    });
    entry->session->SetSavesWrittenCallback([key](const std::string&, const std::string&, uint32_t) {
        if (SaveSync::ConsoleHost* host = SaveSync::Host::Of(key)) {
            host->NoteSaved(); // the emulator's own writes too: every byte that changed is sent
        }
    });
    entry->session->SetStopListener([key] {
        if (SaveSync::ConsoleHost* host = SaveSync::Host::Of(key)) {
            host->GameStopped();
        }
    });
}

void MainWindow::OpenSaveSync() {
    const bool playing_now = current != nullptr && pages->currentWidget() == screen;
    if (playing_now) {
        ReaderUi::ProgramPaused(true);
    }
    ReaderUi::SaveSyncDialog dialog(this);
    dialog.exec();
    if (playing_now) {
        ReaderUi::ProgramPaused(false);
        screen->setFocus();
    }
}

bool MainWindow::UsePairing(const QString& link, QString& error) {
    OpenSettings(QStringLiteral("Save sync"));
    return settings_window && settings_window->UsePairing(link, &error);
}
#endif

void MainWindow::OpenSettings(const QString& tab) {
    if (!settings_window) {
        SettingsWindow::Hooks hooks;
        hooks.playing = [this](QString& console, QString& game, QString& title) {
            if (current != nullptr) {
                console = QString::fromUtf8(current->core.info->key);
                game = playing_id;
                title = ProfileTitle(playing_id);
            }
        };
        hooks.applied = [this] { ApplyPlaySettings(playing_id); };
        hooks.popup_files_changed = [this] {
            AddPopupSettings();
            home->SetConsoles(consoles, &play_settings);
        };
#ifndef READER_NO_SAVE_SYNC
        hooks.sync_folders = [this] { return sync_folders; };
        hooks.open_sync_window = [this] { OpenSaveSync(); };
#endif
        hooks.folders_changed = [this] { FindGames(); };
        settings_window = new SettingsWindow(this, play_settings, consoles, popup_folder, home, std::move(hooks));
        settings_window->setAttribute(Qt::WA_DeleteOnClose);
    } else {
        settings_window->Refresh();
    }
    if (!tab.isEmpty()) {
        settings_window->ShowTab(tab);
    }
    settings_window->show();
    settings_window->raise();
    settings_window->activateWindow();
}

void MainWindow::AddPopupSettings() {
    const QList<PlaySetting> popup = PopupStyle::Settings(popup_folder);
    for (PlayConsole& console : consoles) {
        console.settings.erase(std::remove_if(console.settings.begin(), console.settings.end(),
                                              [](const PlaySetting& setting) {
                                                  return setting.key.startsWith(QStringLiteral("popup."));
                                              }),
                               console.settings.end());
        console.settings += popup;
    }
}

void MainWindow::ApplyPlaySettings(const QString& game) {
    if (current == nullptr || !current->session) {
        return;
    }
    const QString key = QString::fromUtf8(current->core.info->key);
    for (const PlayConsole& console : std::as_const(consoles)) {
        if (console.key == key) {
            std::vector<std::pair<std::string, std::string>> values;
            for (const auto& [name, value] : play_settings.ValuesFor(console, game)) {
                values.emplace_back(name.toStdString(), value.toStdString());
            }
            // The game's cheats and patches are its own (READER_CORE_SETTING_CODES, _PATCHES),
            // listed by the core or not; empty for another game. A port's controller is its
            // console's.
            const uint32_t features = current->session->Features();
            for (const auto& [flag, name] : {std::pair{READER_CORE_HAS_CODES, READER_CORE_SETTING_CODES},
                                             std::pair{READER_CORE_HAS_PATCHES, READER_CORE_SETTING_PATCHES}}) {
                const QString setting = QString::fromLatin1(name);
                if ((features & flag) != 0 &&
                    std::none_of(console.settings.cbegin(), console.settings.cend(),
                                 [&](const PlaySetting& listed) { return listed.key == setting; })) {
                    values.emplace_back(name, play_settings.Raw(setting, key, game).toStdString());
                }
            }
            // RetroAchievements: the user's name and token from the settings' layers
            // (`achievements.*` in program/settings.json), else from the environment, where a
            // front end that keeps its own login hands it over (the Batocera box's launcher
            // reads batocera.conf's): READER_RA_USER, READER_RA_TOKEN, READER_RA_HARDCORE.
            if ((features & READER_CORE_HAS_ACHIEVEMENTS) != 0) {
                for (const auto& [name, variable] :
                     {std::pair{READER_CORE_SETTING_ACHIEVEMENTS_USER, "READER_RA_USER"},
                      std::pair{READER_CORE_SETTING_ACHIEVEMENTS_TOKEN, "READER_RA_TOKEN"},
                      std::pair{READER_CORE_SETTING_ACHIEVEMENTS_HARDCORE, "READER_RA_HARDCORE"}}) {
                    // The game's layer, then its console's, then all consoles'.
                    const QString setting = QString::fromLatin1(name);
                    QString value = game.isEmpty() ? QString() : play_settings.Raw(setting, key, game);
                    for (const QString& console : {key, QString()}) {
                        if (value.isEmpty()) {
                            value = play_settings.Raw(setting, console);
                        }
                    }
                    if (value.isEmpty()) {
                        value = qEnvironmentVariable(variable);
                    }
                    values.emplace_back(name, value.toStdString());
                }
            }
            current->session->ApplySettings(values);
            const QList<reader_core_device> devices = Devices(*current->core.info);
            for (uint32_t port = 0; port < PortCount(*current->core.info) && !devices.isEmpty(); ++port) {
                bool ok = false;
                const uint32_t id = play_settings.Raw(QStringLiteral("ports.%1").arg(port + 1), key).toUInt(&ok);
                if (ok) {
                    current->session->SetPortDevice(port, id);
                }
            }
        }
    }
}

MainWindow::Loaded* MainWindow::CoreFor(const QString& app, const QString& path, QString& error) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    // A file whose type several cores take (".iso": a PSP's disc or a Dreamcast's) goes to the
    // one that says it is a game of its console.
    QList<Loaded*> by_type;
    for (const QString& file : std::as_const(core_files)) {
        auto found = loaded.find(file);
        if (found == loaded.end()) {
            auto entry = std::make_unique<Loaded>();
            QString why;
            if (!LoadCore(file, entry->core, why)) {
                qWarning("%s", qPrintable(why));
                continue;
            }
            found = loaded.emplace(file, std::move(entry)).first;
        }
        const reader_core_info& info = *found->second->core.info;
        bool fits = !app.isEmpty() && (QString::fromUtf8(info.app_key) == app ||
                                       app == QStringLiteral("reader-home-") + QString::fromUtf8(info.key));
        if (fits) {
            return found->second.get();
        }
        for (uint32_t i = 0; app.isEmpty() && i < info.file_type_count; ++i) {
            if (QString::fromUtf8(info.file_types[i]) == suffix) {
                by_type.append(found->second.get());
                break;
            }
        }
    }
    if (by_type.size() == 1) {
        return by_type.first();
    }
    const QByteArray utf8 = path.toUtf8();
    for (Loaded* candidate : std::as_const(by_type)) {
        const reader_core_api& api = *candidate->core.api;
        char id[128] = {}, title[256] = {};
        if (api.struct_size >= offsetof(reader_core_api, identify) + sizeof(api.identify) && api.identify != nullptr &&
            api.identify(utf8.constData(), id, sizeof(id), title, sizeof(title)) == 0) {
            return candidate;
        }
    }
    if (!by_type.isEmpty()) {
        return by_type.first();
    }
    error = app.isEmpty() ? QStringLiteral("No core here plays .%1 files").arg(suffix)
                          : QStringLiteral("No core here plays the games of %1").arg(app);
    return nullptr;
}

bool MainWindow::Plays(const QString& app) {
    // The program's own file of a console's games names it "reader-home-<console>".
    for (const PlayConsole& console : std::as_const(consoles)) {
        if (app == QStringLiteral("reader-home-") + console.key) {
            QString error;
            return CoreFor(console.app, QString(), error) != nullptr;
        }
    }
    QString error;
    return CoreFor(app, QString(), error) != nullptr;
}

bool MainWindow::Play(const QString& path, const QString& app, QString& error) {
    Loaded* entry = CoreFor(app, path, error);
    if (entry == nullptr) {
        return false;
    }
    if (current != nullptr && current != entry && current->session->Running()) {
        current->session->Stop();
    }
    const reader_core_info& info = *entry->core.info;
    if (!entry->session && !StartSession(entry, error)) {
        return false;
    }
    if (!reader_installed) {
        reader_installed = true;
        ReaderUi::Install(this, screen);
        ReaderUi::SetEndGame([](void* window) { static_cast<MainWindow*>(window)->EndGame(); }, this);
        ReaderUi::SetProgramCommands([this](const QString& name, const QStringList& parts,
                                            const std::function<void(const QString&)>& reply) {
            return ProgramCommand(name, parts, reply);
        });
        // What was read goes into Furoshiki's sums once the reader has written it.
        ReaderUi::SetGameStopped(
            [](void* context) {
                auto* window = static_cast<MainWindow*>(context);
                if (window->current != nullptr) {
                    window->WriteOwnFile(QString::fromUtf8(window->current->core.info->key), true);
                }
                window->SendStats();
            },
            this);
    }
    current = entry;
    ApplyConsole(info); // the console's values are the reader's now
    ReaderUi::Style::SetConsoleLook(info.key);
    ReaderUi::LookupPopup::SetConsoleKey(QString::fromUtf8(info.key));
    screen->SetButtons(info);
    ReaderUi::UseSession(current->session.get());
    // The settings for the game go in before it starts: the core says which game the file is
    // (identify) so that its own settings, and those that count only at start, are there from
    // the first frame; again once the core has said which game it is, where that differs.
    playing = path;
    playing_id.clear();
    console_folder = ProgramFolder() + QStringLiteral("/consoles/") + QString::fromUtf8(info.key);
    if (READER_CORE_HAS_FIELD(current->session->Api(), identify)) {
        char id[128] = {}, title[256] = {};
        if (current->session->Api().identify(path.toUtf8().constData(), id, sizeof(id), title, sizeof(title)) == 0) {
            playing_id = QString::fromUtf8(id);
        }
    }
    ApplyPlaySettings(playing_id);
    current->session->SetGameListener([this](const std::string& game_id) {
        ReaderUi::PostToGui([this, id = QString::fromStdString(game_id)] {
            if (current == nullptr) {
                return;
            }
            const QString key = QString::fromUtf8(current->core.info->key);
            if (id != playing_id) {
                playing_id = id;
                ApplyPlaySettings(id);
            }
            RememberGame(key, id, playing);
        });
    });
    ShowGame(true);
    SendWindow();
    std::string why;
    if (!current->session->LoadGame(path.toStdString(), why)) {
        error = QString::fromStdString(why);
        ShowGame(false);
        return false;
    }
    QTimer::singleShot(10000, this, [this] { SendStats(); }); // what earlier runs counted
    return true;
}

void MainWindow::EndGame() {
    if (current != nullptr) {
        current->session->Stop();
    }
    for (uint32_t port = 0; port < MaxPorts; ++port) {
        Rumble(port, 0, 0);
    }
    ShowGame(false);
}

void MainWindow::RememberGame(const QString& key, const QString& id, const QString& path) {
    if (id.isEmpty() || path.isEmpty()) {
        return;
    }
    const QString file = ProgramFolder() + QStringLiteral("/games.json");
    QJsonObject root;
    if (QFile in(file); in.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(in.readAll()).object();
    }
    QJsonArray list = root.value(key).toArray();
    for (const QJsonValue& value : std::as_const(list)) {
        if (value.toObject().value(QStringLiteral("path")).toString() == path) {
            return;
        }
    }
    list.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}});
    root.insert(key, list);
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson());
        out.commit();
    }
    QList<OwnGame>& games = own_games[key];
    // The same game by its ID, or the same file however it was spelt (\\host\share and
    // //host/share on Windows).
    const auto same_file = [](const QString& a, const QString& b) {
        return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)),
                                                                      Qt::CaseInsensitive) == 0;
    };
    bool known = false;
    for (const OwnGame& game : std::as_const(games)) {
        known = known || game.id == id || same_file(game.path, path);
    }
    if (!known && (ReaderUi::ReaderBridge::HasBundledProfile(id) || ReaderUi::ReaderBridge::HasUserProfile(id))) {
        const QString title = ProfileTitle(id);
        games.append({id, title.isEmpty() ? QFileInfo(path).completeBaseName() : title, path});
        WriteOwnFile(key, false);
        ShowConsoleGames(key);
    }
}

void MainWindow::FindGames() {
    struct Task {
        QString key;
        const reader_core_api* api;
        QStringList folders, types;
        QList<OwnGame> played;
    };
    QList<Task> tasks;
    const Library library = Load(SharedFolder());
    QJsonObject remembered;
    if (QFile in(ProgramFolder() + QStringLiteral("/games.json")); in.open(QIODevice::ReadOnly)) {
        remembered = QJsonDocument::fromJson(in.readAll()).object();
    }
    for (auto& [file, entry] : loaded) {
        const reader_core_info& info = *entry->core.info;
        const reader_core_api& api = *entry->core.api;
        if (api.struct_size < offsetof(reader_core_api, identify) + sizeof(api.identify) || api.identify == nullptr) {
            continue;
        }
        Task task{QString::fromUtf8(info.key), entry->core.api, play_settings.Folders(QString::fromUtf8(info.key)), {}, {}};
        for (uint32_t i = 0; i < info.file_type_count; ++i) {
            task.types.append(QString::fromUtf8(info.file_types[i]));
        }
        // Where the user keeps them: where the apps' files and the games played say.
        for (const Game& game : library.games) {
            const App* app = nullptr;
            for (const App& candidate : library.apps) {
                app = candidate.app == game.app ? &candidate : app;
            }
            if (app != nullptr && app->app == QString::fromUtf8(info.app_key) && !game.path.isEmpty()) {
                task.folders.append(QFileInfo(game.path).absolutePath());
            }
        }
        for (const QJsonValue& value : remembered.value(task.key).toArray()) {
            const QJsonObject game = value.toObject();
            const QString path = game.value(QStringLiteral("path")).toString();
            task.folders.append(QFileInfo(path).absolutePath());
            task.played.append({game.value(QStringLiteral("id")).toString(), QFileInfo(path).completeBaseName(), path});
        }
        task.folders.removeDuplicates();
        // A Vita's memory card copied as it is keeps its games in app/<ID>, one folder deeper
        // than the search goes.
        if (task.key == QStringLiteral("psvita")) {
            for (const QString& folder : QStringList(task.folders)) {
                task.folders.append(folder + QStringLiteral("/app"));
            }
        }
        tasks.append(task);
    }
    if (tasks.isEmpty()) {
        return;
    }
    // In a thread of Qt's pool: reading discs takes a moment; the window goes on.
    QThreadPool::globalInstance()->start([this, tasks] {
        std::map<QString, QList<OwnGame>> found;
        for (const Task& task : tasks) {
            QList<OwnGame>& games = found[task.key];
            QSet<QString> seen;
            const auto add = [&](const QString& path) {
                const QString canonical = QFileInfo(path).canonicalFilePath();
                if (canonical.isEmpty() || seen.contains(canonical)) {
                    return;
                }
                seen.insert(canonical);
                char id[128] = {}, title[256] = {};
                const QByteArray utf8 = path.toUtf8();
                if (task.api->identify(utf8.constData(), id, sizeof(id), title, sizeof(title)) == 0 && id[0] != '\0') {
                    games.append({QString::fromUtf8(id),
                                  title[0] != '\0' ? QString::fromUtf8(title) : QFileInfo(path).completeBaseName(), path});
                }
            };
            for (const OwnGame& game : task.played) {
                if (QFileInfo::exists(game.path)) {
                    add(game.path);
                }
            }
            for (const QString& folder : task.folders) {
                // The folder and the folders in it (a game in a folder of its own), not deeper.
                QDirIterator files(folder, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                QStringList paths;
                while (files.hasNext()) {
                    const QFileInfo info(files.next());
                    if (info.isDir()) {
                        for (const QFileInfo& inner : QDir(info.filePath()).entryInfoList(QDir::Files)) {
                            paths.append(inner.filePath());
                        }
                    } else {
                        paths.append(info.filePath());
                    }
                }
                paths.sort();
                for (const QString& path : std::as_const(paths)) {
                    const QFileInfo info(path);
                    const QString suffix = info.suffix().toLower();
                    if (!task.types.contains(suffix)) {
                        continue;
                    }
                    // A track of a disc whose sheet is beside it is the sheet's.
                    if (suffix == QStringLiteral("bin") &&
                        (QFileInfo::exists(info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".cue")) ||
                         QFileInfo::exists(info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".CUE")))) {
                        continue;
                    }
                    add(path);
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, found = std::move(found)] {
            for (const auto& [key, games] : found) {
                QList<OwnGame> readable;
                for (OwnGame game : games) {
                    if (ReaderUi::ReaderBridge::HasBundledProfile(game.id) || ReaderUi::ReaderBridge::HasUserProfile(game.id)) {
                        // The game's own name, as its profile writes it, where the disc has none.
                        if (const QString title = ProfileTitle(game.id); !title.isEmpty()) {
                            game.title = title;
                        }
                        readable.append(game);
                    }
                }
                // One row a game: a game found under two spellings of its folder, or found and
                // played, is listed once.
                QList<OwnGame> once;
                for (const OwnGame& game : std::as_const(readable)) {
                    if (std::none_of(once.cbegin(), once.cend(), [&](const OwnGame& other) { return other.id == game.id; })) {
                        once.append(game);
                    }
                }
                own_games[key] = once;
                WriteOwnFile(key, false);
                ShowConsoleGames(key);
            }
            home->Refresh();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::SendStats() {
#ifndef READER_NO_SAVE_SYNC
    // The reading statistics go along with the saves, as the apps send theirs: every reading
    // day of every game read here, whole each time, under this install's own ID.
    if (!SaveSync::Host::Started() || !SaveSync::Host::Get().enabled) {
        return;
    }
    auto& session = ReaderUi::ReaderSession::Instance();
    const std::string install = session.Settings().StatsInstall().toStdString();
    const QString device = QString::fromStdString(SaveSync::Host::Get().device);
    ReaderApp::ReaderDatabase* database = &session.Database();
    session.AfterWrites([install, device, database] {
        QJsonObject record = database->StatsRecord();
        record.insert(QStringLiteral("app"), QStringLiteral("reader-home"));
#ifdef _WIN32
        record.insert(QStringLiteral("platform"), QStringLiteral("windows"));
#else
        record.insert(QStringLiteral("platform"), QStringLiteral("linux"));
#endif
        record.insert(QStringLiteral("device"), device);
        record.insert(QStringLiteral("sent"), QDateTime::currentMSecsSinceEpoch());
        SaveSync::Host::SendStats(install, QJsonDocument(record).toJson(QJsonDocument::Compact).toStdString());
    });
#endif
}

void MainWindow::ShowConsoleGames(const QString& key) {
    QList<HomeWindow::ConsoleGame> list;
    for (const OwnGame& game : own_games[key]) {
        list.append({game.id, game.title});
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.title < b.title; });
    home->SetConsoleGames(key, list);
}

HomeWindow::ConsoleSetup MainWindow::SetupOf(const QString& key) const {
    HomeWindow::ConsoleSetup setup;
    const auto found = system_folders.find(key);
    setup.system_folder = found != system_folders.end()
                              ? QDir::fromNativeSeparators(found->second)
                              : ProgramFolder() + QStringLiteral("/consoles/") + key + QStringLiteral("/system");
    const QDir folder(setup.system_folder);
    // What each console's core looks for there (the cores' own lookups: a BIOS of the
    // console's by its contents, Eden's keys by name).
    const auto files = [&folder](const QStringList& patterns, qint64 least) {
        QStringList out;
        for (const QFileInfo& file : folder.entryInfoList(patterns, QDir::Files)) {
            if (file.size() >= least) {
                out.append(file.fileName());
            }
        }
        return out;
    };
    if (key == QStringLiteral("ps1") || key == QStringLiteral("ps2")) {
        // A PlayStation's BIOS is 512 KB, a PS2's 4 MB.
        const QStringList bios = files({QStringLiteral("*.bin"), QStringLiteral("*.BIN")}, key == QStringLiteral("ps1") ? 512 * 1024 : 4 * 1024 * 1024);
        setup.ready = !bios.isEmpty();
        setup.status = setup.ready ? QStringLiteral("BIOS %1").arg(bios.join(QStringLiteral(", ")))
                                   : QStringLiteral("THE CONSOLE'S BIOS IS NEEDED (A .BIN OF YOUR OWN CONSOLE)");
    } else if (key == QStringLiteral("switch")) {
        setup.ready = folder.exists(QStringLiteral("prod.keys"));
        const bool firmware = QDir(setup.system_folder + QStringLiteral("/nand/system/Contents/registered")).isEmpty() == false;
        setup.status = setup.ready ? QStringLiteral("KEYS FOUND%1").arg(firmware ? QStringLiteral(", FIRMWARE IN THE NAND") : QStringLiteral(", NO FIRMWARE"))
                                   : QStringLiteral("PROD.KEYS FROM YOUR OWN SWITCH IS NEEDED");
    } else if (key == QStringLiteral("dc")) {
        const QStringList bios = files({QStringLiteral("dc_boot.bin"), QStringLiteral("dc/dc_boot.bin")}, 0);
        setup.status = bios.isEmpty() && !folder.exists(QStringLiteral("dc/dc_boot.bin"))
                           ? QStringLiteral("NOTHING NEEDED (FLYCAST'S OWN BIOS; DC_BOOT.BIN OF YOURS IF YOU LIKE)")
                           : QStringLiteral("YOUR DREAMCAST'S BIOS");
    } else if (key == QStringLiteral("3ds")) {
        setup.status = folder.exists(QStringLiteral("sysdata/aes_keys.txt")) || folder.exists(QStringLiteral("aes_keys.txt"))
                           ? QStringLiteral("AES KEYS FOUND")
                           : QStringLiteral("NOTHING NEEDED FOR DECRYPTED GAMES");
    } else {
        setup.status = QStringLiteral("NOTHING NEEDED");
    }
    return setup;
}

void MainWindow::WriteOwnFile(const QString& key, bool stats) {
    const PlayConsole* console = nullptr;
    const reader_core_info* info = nullptr;
    for (const PlayConsole& candidate : std::as_const(consoles)) {
        console = candidate.key == key ? &candidate : console;
    }
    for (const auto& [file, entry] : loaded) {
        info = QString::fromUtf8(entry->core.info->key) == key ? entry->core.info : info;
    }
    if (console == nullptr || info == nullptr) {
        return;
    }
    const QList<OwnGame>& games = own_games[key];
    // The console under the name its own app gives it, where that app has written its file:
    // the page groups games by that name ("PlayStation Portable" and "PSP" were two consoles).
    QString system = QString::fromUtf8(info->system);
    for (const App& app : Load(SharedFolder()).apps) {
        if (app.app == console->app && !app.system.isEmpty()) {
            system = app.system;
        }
    }
    const ReaderUi::HomeLink::About about{QStringLiteral("reader-home-") + key, QStringLiteral("Furoshiki"),
                                          system, {QStringLiteral("--play"), QStringLiteral("%ROM%")},
                                          {}, {}, console->name, -1};
    QList<ReaderUi::HomeLink::Game> list;
    QSet<QString> ids;
    // Each game's cover: the user's own in program/consoles/<key>/covers/<ID>.png (or .jpg),
    // else a picture beside the game's file by its name (or in a "covers" folder there), else
    // the one a reader app listed for the same game.
    const Library library = Load(SharedFolder());
    const auto cover_of = [&](const OwnGame& game) {
        const QFileInfo file(game.path);
        for (const QString& type : {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg")}) {
            for (const QString& candidate : {ProgramFolder() + QStringLiteral("/consoles/%1/covers/%2.%3").arg(key, game.id, type),
                                             file.path() + QStringLiteral("/%1.%2").arg(file.completeBaseName(), type),
                                             file.path() + QStringLiteral("/covers/%1.%2").arg(file.completeBaseName(), type),
                                             file.path() + QStringLiteral("/covers/%1.%2").arg(game.id, type)}) {
                if (QFileInfo::exists(candidate)) {
                    return QDir::toNativeSeparators(candidate);
                }
            }
        }
        for (const Game& listed : library.games) {
            if (listed.id == game.id && !listed.cover.isEmpty() && !listed.app.startsWith(QStringLiteral("reader-home-")) &&
                QFileInfo::exists(listed.cover)) {
                return listed.cover;
            }
        }
        return QString();
    };
    for (const OwnGame& game : games) {
        list.append({game.id, game.title, QDir::toNativeSeparators(game.path), cover_of(game)});
        ids.insert(game.id);
    }
    if (!stats || !reader_installed) {
        ReaderUi::HomeLink::Write(about, &list, nullptr);
        return;
    }
    // What was read in the program, of this console's games (the program keeps every
    // console's in one record).
    auto& session = ReaderUi::ReaderSession::Instance();
    ReaderApp::ReaderDatabase* database = &session.Database();
    session.AfterWrites([this, about, list, ids, database] {
        const QJsonObject all = database->StatsRecord();
        QJsonObject record = all;
        QJsonObject games;
        const QJsonObject all_games = all.value(QStringLiteral("games")).toObject();
        for (auto it = all_games.begin(); it != all_games.end(); ++it) {
            if (ids.contains(it.key())) {
                games.insert(it.key(), it.value());
            }
        }
        QJsonArray days;
        for (const QJsonValue& day : all.value(QStringLiteral("days")).toArray()) {
            if (ids.contains(day.toObject().value(QStringLiteral("game")).toString())) {
                days.append(day);
            }
        }
        record.insert(QStringLiteral("games"), games);
        record.insert(QStringLiteral("days"), days);
        // The file on the reader's worker (as the apps write theirs), the page on the main thread.
        ReaderUi::HomeLink::Write(about, &list, &record);
        QMetaObject::invokeMethod(this, [this] { home->Refresh(); }, Qt::QueuedConnection);
    });
}

void MainWindow::ShowGame(bool shown) {
    pages->setCurrentWidget(shown ? static_cast<QWidget*>(screen) : static_cast<QWidget*>(home));
    // The page under the game takes no keys while it plays; the Steam Controller is the game's.
    home->SetPlaying(shown);
    steam_buttons = 0;
    std::fill(std::begin(steam_axes), std::end(steam_axes), 0);
    ReaderUi::SetScreenShown(shown);
    (shown ? static_cast<QWidget*>(screen) : static_cast<QWidget*>(home))->setFocus();
    if (!shown) {
        ReaderUi::ProgramPaused(false);
    }
}

void MainWindow::SendWindow() {
    if (current == nullptr || !current->session) {
        return;
    }
    reader_core_window window{};
    window.struct_size = sizeof(window);
    const qreal scale = screen->devicePixelRatioF();
    window.width = static_cast<uint32_t>(screen->width() * scale);
    window.height = static_cast<uint32_t>(screen->height() * scale);
    window.scale = static_cast<float>(scale);
    window.handle = reinterpret_cast<void*>(screen->winId());
#if defined(_WIN32)
    window.kind = READER_CORE_WINDOW_WIN32;
#elif defined(__APPLE__)
    window.kind = READER_CORE_WINDOW_COCOA;
#else
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        window.kind = READER_CORE_WINDOW_X11;
#if QT_CONFIG(xcb)
        if (auto* x11 = qApp->nativeInterface<QNativeInterface::QX11Application>()) {
            window.display = x11->display();
        }
#endif
    } else {
        window.kind = READER_CORE_WINDOW_NONE; // Wayland's own surfaces: not yet
    }
#endif
    current->session->SetWindow(window);
}

void MainWindow::PollPads() {
    // The game ended (by itself, or it never started): Furoshiki's page again.
    if (current != nullptr && current->session && current->session->Ended() && pages->currentWidget() == screen) {
        ShowGame(false);
    }

    SDL_GameControllerUpdate();
    // Controllers gone are let go; one that comes takes the next port, in the order they came.
    std::erase_if(sdl_pads, [](SDL_GameController* pad) {
        if (SDL_GameControllerGetAttached(pad)) {
            return false;
        }
        SDL_GameControllerClose(pad);
        return true;
    });
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        const bool open = std::any_of(sdl_pads.cbegin(), sdl_pads.cend(), [id](SDL_GameController* pad) {
            return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == id;
        });
        if (!open) {
            if (SDL_GameController* pad = SDL_GameControllerOpen(i)) {
                sdl_pads.push_back(pad);
            }
        }
    }
    SDL_GameController* pad = sdl_pads.empty() ? nullptr : sdl_pads.front();
    // A controller's button by SDL's number: SDL's controller or the Steam Controller.
    const auto button = [this, pad](int sdl) {
        return (pad != nullptr && SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(sdl)) != 0) ||
               (steam_buttons & (1U << sdl)) != 0;
    };
    // The program's menu from the controller while a game plays (Resume, the Reader menu, a
    // savestate, back to Furoshiki): its Guide button (the Steam button), or Select and Start
    // held together a moment where the system keeps the Guide button; again, and it closes.
    const bool game_shown = current != nullptr && pages->currentWidget() == screen;
    static bool guide_held = false;
    static qint64 both_since = 0; // Select and Start held since (ms), 0: not both
    static bool both_used = false;
    const bool guide = game_shown && button(SDL_CONTROLLER_BUTTON_GUIDE);
    if (guide && !guide_held) {
        OpenMenu();
    }
    guide_held = guide;
    const bool both = game_shown && button(SDL_CONTROLLER_BUTTON_BACK) && button(SDL_CONTROLLER_BUTTON_START);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!both) {
        both_since = 0;
        both_used = false;
    } else if (both_since == 0) {
        both_since = now;
    } else if (!both_used && now - both_since >= 700) {
        both_used = true;
        OpenMenu();
    }
    // While the menu is open the controller works it, and the game gets nothing.
    if (menu != nullptr && menu->isVisible()) {
        static uint32_t menu_held = 0;
        const std::pair<int, int> keys[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, Qt::Key_Up},       {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Qt::Key_Down},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Qt::Key_Left},   {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key_Right},
            {SDL_CONTROLLER_BUTTON_A, Qt::Key_Return},         {SDL_CONTROLLER_BUTTON_B, Qt::Key_Escape},
        };
        for (const auto& [sdl, key] : keys) {
            const bool down = button(sdl);
            const uint32_t mask = 1U << sdl;
            if (down && (menu_held & mask) == 0) {
                if (QWidget* target = QApplication::activePopupWidget() ? QApplication::activePopupWidget() : menu) {
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
                }
            }
            menu_held = down ? menu_held | mask : menu_held & ~mask;
        }
        std::scoped_lock lock{pads_mutex};
        std::fill(std::begin(gamepads), std::end(gamepads), reader_core_pad{});
        return;
    }
    // Furoshiki's page (and a window over it, such as Save sync) from SDL's controller, as the
    // Steam Controller works it on Windows (SteamPad): the d-pad or the left stick go along, A or
    // Start take, B goes back, the bumpers change page, Y reads the apps again. A press counts
    // when it comes, so what was held as a game ended does nothing here.
    {
        static uint32_t page_held = 0;
        static int stick_x = 0;
        static int stick_y = 0;
        const std::pair<int, int> page_keys[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, Qt::Key_Up},       {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Qt::Key_Down},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Qt::Key_Left},   {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key_Right},
            {SDL_CONTROLLER_BUTTON_A, Qt::Key_Return},         {SDL_CONTROLLER_BUTTON_START, Qt::Key_Return},
            {SDL_CONTROLLER_BUTTON_B, Qt::Key_Escape},         {SDL_CONTROLLER_BUTTON_Y, Qt::Key_F5},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Qt::Key_Q},   {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Qt::Key_E},
        };
        // Where a key goes: an open popup or dialog's focused widget, else the page.
        const auto send = [this](int key) {
            QWidget* target = QApplication::activePopupWidget();
            if (target == nullptr && QApplication::activeModalWidget() != nullptr) {
                target = QApplication::focusWidget() != nullptr ? QApplication::focusWidget() : QApplication::activeModalWidget();
            }
            if (target == nullptr) {
                target = pages->currentWidget() == home ? static_cast<QWidget*>(home) : QApplication::focusWidget();
            }
            if (target != nullptr) {
                QApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
                QApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
            }
        };
        // On Windows the Steam Controller works the page itself; SDL may see it as well.
        const bool page_shown = !game_shown && pad != nullptr && !steam_seen;
        uint32_t now_held = 0;
        for (const auto& [sdl, key] : page_keys) {
            const uint32_t mask = 1U << sdl;
            if (button(sdl)) {
                now_held |= mask;
                if (page_shown && (page_held & mask) == 0) {
                    send(key);
                }
            }
        }
        page_held = now_held;
        // The left stick as the d-pad: over halfway is a press, once.
        const auto stick = [&](SDL_GameControllerAxis axis, int& held, int less, int more) {
            const int value = pad != nullptr ? SDL_GameControllerGetAxis(pad, axis) : 0;
            const int now = value > 18000 ? 1 : value < -18000 ? -1 : (std::abs(value) < 9000 ? 0 : held);
            if (now != held && now != 0 && page_shown) {
                send(now > 0 ? more : less);
            }
            held = now;
        };
        stick(SDL_CONTROLLER_AXIS_LEFTX, stick_x, Qt::Key_Left, Qt::Key_Right);
        stick(SDL_CONTROLLER_AXIS_LEFTY, stick_y, Qt::Key_Up, Qt::Key_Down);
    }
    reader_core_pad state{};
    if ((pad != nullptr || steam_seen) && current != nullptr) {
        const void* device = pad != nullptr ? static_cast<const void*>(pad) : static_cast<const void*>(&steam_buttons);
        const reader_core_info& info = *current->core.info;
        static uint64_t held = 0;  // by role, for the reader's input filter
        static uint64_t taken = 0; // those of them the filter kept from the game
        bool trigger_taken[2]{};
        Reader::InputFilter& filter = Reader::InputFilter::Instance();
        for (std::size_t i = 0; i < std::size(Roles); ++i) {
            const Role& role = Roles[i];
            bool down = false;
            if (role.sdl >= 0) {
                // Select and Start held together are the menu's, not the game's.
                down = button(role.sdl) && !(both && (role.sdl == SDL_CONTROLLER_BUTTON_BACK || role.sdl == SDL_CONTROLLER_BUTTON_START));
            } else {
                const bool left = role.filter == 104;
                const auto axis = left ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                down = (pad != nullptr && SDL_GameControllerGetAxis(pad, axis) > 16000) ||
                       steam_axes[left ? ReaderApp::SteamBridge::LeftTrigger : ReaderApp::SteamBridge::RightTrigger] > 16000;
            }
            const uint64_t mask = uint64_t{1} << i;
            if (down != ((held & mask) != 0)) {
                held = down ? held | mask : held & ~mask;
                // The reader's own buttons (its cursor's, its menu's) never reach the game,
                // for as long as they are held.
                const bool kept = role.filter >= 0 && filter.OnButton(device, static_cast<std::size_t>(role.filter), down);
                taken = kept ? taken | mask : taken & ~mask;
            }
            const int bit = BitOf(info, role);
            if (down && (taken & mask) == 0 && bit >= 0) {
                state.buttons |= uint64_t{1} << bit;
            }
            if (role.sdl < 0 && down && (taken & mask) != 0) {
                trigger_taken[role.filter == 104 ? 0 : 1] = true;
            }
        }
        // SDL's controller and the Steam Controller together: the one pushed further counts.
        const auto axis = [this, pad](SDL_GameControllerAxis sdl_axis, int steam_axis) {
            const int from_sdl = pad != nullptr ? SDL_GameControllerGetAxis(pad, sdl_axis) : 0;
            const int from_steam = steam_axes[steam_axis];
            return std::abs(from_steam) > std::abs(from_sdl) ? from_steam : from_sdl;
        };
        namespace Steam = ReaderApp::SteamBridge;
        if (!filter.SticksHeld()) {
            state.sticks[0] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_LEFTX, Steam::LeftX));
            state.sticks[1] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_LEFTY, Steam::LeftY));
            state.sticks[2] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_RIGHTX, Steam::RightX));
            state.sticks[3] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_RIGHTY, Steam::RightY));
        }
        if (!trigger_taken[0]) {
            state.triggers[0] = static_cast<uint8_t>(std::max(0, axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, Steam::LeftTrigger) >> 7));
        }
        if (!trigger_taken[1]) {
            state.triggers[1] = static_cast<uint8_t>(std::max(0, axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, Steam::RightTrigger) >> 7));
        }
    }
    // The other ports: SDL's next controllers, each as it is (the reader's buttons are the
    // first controller's only). The Steam Controller's own gamepad, where SDL sees it too, is
    // the first controller already.
    reader_core_pad others[MaxPorts]{};
    std::fill(std::begin(port_pads), std::end(port_pads), nullptr);
    port_pads[0] = pad;
    const uint32_t ports = current != nullptr ? std::min<uint32_t>(PortCount(*current->core.info), MaxPorts) : 1;
    for (uint32_t port = 1, next = 1; port < ports && next < sdl_pads.size(); ++next) {
        SDL_GameController* other = sdl_pads[next];
        const char* name = SDL_GameControllerName(other);
        if (steam_seen && name != nullptr && QString::fromUtf8(name).contains(QStringLiteral("Steam"), Qt::CaseInsensitive)) {
            continue;
        }
        port_pads[port] = other;
        reader_core_pad& to = others[port++];
        to.connected = 1;
        for (const Role& role : Roles) {
            const int bit = BitOf(*current->core.info, role);
            bool down = false;
            if (role.sdl >= 0) {
                down = SDL_GameControllerGetButton(other, static_cast<SDL_GameControllerButton>(role.sdl)) != 0;
            } else {
                down = SDL_GameControllerGetAxis(other, role.filter == 104 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                                                         : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
            }
            if (down && bit >= 0) {
                to.buttons |= uint64_t{1} << bit;
            }
        }
        to.sticks[0] = SDL_GameControllerGetAxis(other, SDL_CONTROLLER_AXIS_LEFTX);
        to.sticks[1] = SDL_GameControllerGetAxis(other, SDL_CONTROLLER_AXIS_LEFTY);
        to.sticks[2] = SDL_GameControllerGetAxis(other, SDL_CONTROLLER_AXIS_RIGHTX);
        to.sticks[3] = SDL_GameControllerGetAxis(other, SDL_CONTROLLER_AXIS_RIGHTY);
        to.triggers[0] = static_cast<uint8_t>(std::max(0, SDL_GameControllerGetAxis(other, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7));
        to.triggers[1] = static_cast<uint8_t>(std::max(0, SDL_GameControllerGetAxis(other, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7));
    }
    std::scoped_lock lock{pads_mutex};
    gamepads[0] = state;
    std::copy(std::begin(others) + 1, std::end(others), std::begin(gamepads) + 1);
}

void MainWindow::OpenMenu() {
    if (menu != nullptr && menu->isVisible()) {
        menu->hide(); // Escape again: back to the game
        return;
    }
    if (current == nullptr) {
        return;
    }
    if (menu == nullptr) {
        // The program's own menu over the game; the game stands still while it is open. Not
        // run in a loop of its own (popup): the core goes on drawing, and a key handed in by
        // the reader's development channel comes back at once. Its submenus are filled each
        // time they open, from what the core says then.
        menu = new QMenu(this);
        menu->addAction(QStringLiteral("Resume"));
        reader_entry = menu->addSeparator();
        QMenu* save = menu->addMenu(QStringLiteral("Save state"));
        QMenu* load = menu->addMenu(QStringLiteral("Load state"));
        connect(save, &QMenu::aboutToShow, this, [this, save] { FillStateMenu(save, false); });
        connect(load, &QMenu::aboutToShow, this, [this, load] { FillStateMenu(load, true); });
        QMenu* discs = menu->addMenu(QStringLiteral("Change disc"));
        connect(discs, &QMenu::aboutToShow, this, [this, discs] { FillDiscMenu(discs); });
        QMenu* ports = menu->addMenu(QStringLiteral("Controllers"));
        connect(ports, &QMenu::aboutToShow, this, [this, ports] { FillPortMenu(ports); });
        QMenu* patches = menu->addMenu(QStringLiteral("Patches and cheats"));
        connect(patches, &QMenu::aboutToShow, this, [this, patches] { FillPatchMenu(patches); });
        menu->addSeparator();
        QAction* window_settings = menu->addAction(QStringLiteral("Settings…"));
        window_settings->setShortcut(QKeySequence(Qt::Key_F2));
        connect(window_settings, &QAction::triggered, this, [this] { OpenSettings(); });
        QAction* full = menu->addAction(QStringLiteral("Full screen"));
        full->setShortcut(QKeySequence(Qt::Key_F11));
        connect(full, &QAction::triggered, this, [this] { ToggleFullScreen(); });
#ifndef READER_NO_SAVE_SYNC
        QAction* sync = menu->addAction(QStringLiteral("Save sync…"));
        connect(sync, &QAction::triggered, this, [this] { OpenSaveSync(); });
#endif
#ifndef HOME_PUBLIC
        QAction* feedback = menu->addAction(QStringLiteral("Send feedback…"));
        connect(feedback, &QAction::triggered, this, [this] {
            auto* box = new FeedbackDialog(this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        });
#endif
        QAction* folder = menu->addAction(QStringLiteral("Open the save folder"));
        connect(folder, &QAction::triggered, this, [this] { OpenSaveFolder(); });
        menu->addSeparator();
        QAction* quit = menu->addAction(QStringLiteral("Back to Furoshiki"));
        connect(quit, &QAction::triggered, this, [this] { EndGame(); });
        // Out of the program altogether, the game stopped as on closing the window (its last
        // save sent): where a front end such as EmulationStation started the game, the way
        // back to it from the controller.
        QAction* exit = menu->addAction(QStringLiteral("Quit Furoshiki"));
        connect(exit, &QAction::triggered, this, [this] { close(); });
        connect(menu, &QMenu::aboutToShow, this, [this, discs, ports, patches] {
            // What the running core cannot do is not offered.
            const uint32_t features = current != nullptr ? current->session->Features() : 0;
            discs->menuAction()->setVisible((features & READER_CORE_HAS_DISCS) != 0);
            ports->menuAction()->setVisible(current != nullptr && (PortCount(*current->core.info) > 1 ||
                                                                   !Devices(*current->core.info).isEmpty()));
            patches->menuAction()->setVisible((features & (READER_CORE_HAS_PATCHES | READER_CORE_HAS_CODES)) != 0);
        });
        connect(menu, &QMenu::aboutToHide, this, [this] {
            ReaderUi::ProgramPaused(false);
            screen->setFocus();
        });
    }
    if (QMenu* reader = ReaderUi::Menu(); reader != nullptr && !menu->actions().contains(reader->menuAction())) {
        menu->insertMenu(reader_entry, reader);
    }
    ReaderUi::ProgramPaused(true);
    menu->popup(screen->mapToGlobal(screen->rect().center()));
}

namespace {
/// The savestate menus' pictures larger than a menu's icons are.
class SlotStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override {
        if (metric == PM_SmallIconSize) {
            return 96;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};
} // namespace

QString MainWindow::StatePicture(int slot) const {
    const QString game = playing_id.isEmpty() ? QFileInfo(playing).completeBaseName() : playing_id;
    return console_folder + QStringLiteral("/states/pictures/%1/%2.png").arg(game).arg(slot);
}

void MainWindow::FillStateMenu(QMenu* into, bool load) {
    into->clear();
    if (into->style()->objectName() != QStringLiteral("slots")) {
        auto* style = new SlotStyle(QApplication::style()->name());
        style->setObjectName(QStringLiteral("slots"));
        style->setParent(into);
        into->setStyle(style);
    }
    if (current == nullptr) {
        return;
    }
    const int count = StateSlots(*current->core.info);
    for (int slot = 1; slot <= count; ++slot) {
        const std::optional<CoreSession::StateSlot> info = current->session->StateInfo(slot);
        QString text = QStringLiteral("Slot %1").arg(slot);
        if (info && info->exists) {
            text += QStringLiteral("    ") +
                    QDateTime::fromMSecsSinceEpoch(info->saved_ms).toString(QStringLiteral("yyyy-MM-dd  HH:mm"));
        } else if (info) {
            text += QStringLiteral("    empty");
        }
        if (slot == state_slot) {
            text += QStringLiteral("    (F5 / F8)");
        }
        QAction* action = into->addAction(text);
        const QString picture = StatePicture(slot);
        if (info && info->exists && QFileInfo::exists(picture)) {
            action->setIcon(QIcon(picture));
        }
        // A slot a core says is empty cannot be loaded; one it says nothing of is tried.
        action->setEnabled(!load || !info || info->exists);
        connect(action, &QAction::triggered, this, [this, slot, load] {
            menu->hide();
            load ? LoadStateFrom(slot) : SaveStateTo(slot);
        });
    }
}

void MainWindow::SaveStateTo(int slot) {
    if (current == nullptr || !current->session->Running() || slot < 1 || slot > StateSlots(*current->core.info)) {
        return;
    }
    state_slot = slot;
    // Its picture first, from the frame the state will hold: the game's own size, made small.
    // The slot's old one goes, so that a picture the core could not give leaves none.
    const QString picture = StatePicture(slot);
    QFile::remove(picture);
    current->session->Grab(READER_CORE_GRAB_PICTURE, [picture](const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t stride) {
        if (rgba == nullptr || width == 0 || height == 0) {
            return;
        }
        const QImage image = QImage(rgba, static_cast<int>(width), static_cast<int>(height), static_cast<int>(stride),
                                    QImage::Format_RGBA8888)
                                 .scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QDir().mkpath(QFileInfo(picture).path());
        image.save(picture);
    });
    if (!current->session->SaveState(slot) && (current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Slot %1 could not be written").arg(slot));
    } else if ((current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Saved in slot %1").arg(slot)); // a core that does not answer
    }
}

void MainWindow::LoadStateFrom(int slot) {
    if (current == nullptr || !current->session->Running() || slot < 1 || slot > StateSlots(*current->core.info)) {
        return;
    }
    state_slot = slot;
    if (!current->session->LoadState(slot) && (current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Slot %1 could not be loaded").arg(slot));
    } else if ((current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Slot %1 loaded").arg(slot));
    }
}

void MainWindow::FillDiscMenu(QMenu* into) {
    into->clear();
    if (current == nullptr) {
        return;
    }
    CoreSession& session = *current->session;
    const int32_t now = session.DiscCurrent();
    for (uint32_t i = 0; i < session.DiscCount(); ++i) {
        const std::string label = session.DiscLabel(i);
        QAction* action = into->addAction(label.empty() ? QStringLiteral("Disc %1").arg(i + 1)
                                                        : QStringLiteral("%1. %2").arg(i + 1).arg(QString::fromStdString(label)));
        action->setCheckable(true);
        action->setChecked(static_cast<int32_t>(i) == now);
        connect(action, &QAction::triggered, this, [this, i] {
            if (current != nullptr && !current->session->InsertDisc(static_cast<int32_t>(i))) {
                ReaderUi::Notify(QStringLiteral("The disc could not be changed"));
            }
        });
    }
    into->addSeparator();
    QAction* open = into->addAction(QStringLiteral("Open the tray"));
    open->setEnabled(now >= 0);
    connect(open, &QAction::triggered, this, [this] {
        if (current != nullptr) {
            current->session->InsertDisc(-1);
        }
    });
    QAction* add = into->addAction(QStringLiteral("Another disc's file…"));
    connect(add, &QAction::triggered, this, [this] {
        if (current == nullptr) {
            return;
        }
        QStringList types;
        for (uint32_t i = 0; i < current->core.info->file_type_count; ++i) {
            types.append(QStringLiteral("*.") + QString::fromUtf8(current->core.info->file_types[i]));
        }
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Another disc"), QFileInfo(playing).path(),
                                                          QStringLiteral("Discs (%1)").arg(types.join(QLatin1Char(' '))));
        if (file.isEmpty() || current == nullptr) {
            return;
        }
        const int32_t index = current->session->AddDisc(QDir::toNativeSeparators(file).toStdString());
        if (index < 0 || !current->session->InsertDisc(index)) {
            ReaderUi::Notify(QStringLiteral("That disc could not be put in"));
        }
    });
}

void MainWindow::FillPortMenu(QMenu* into) {
    into->clear();
    if (current == nullptr) {
        return;
    }
    const reader_core_info& info = *current->core.info;
    const QString key = QString::fromUtf8(info.key);
    const QList<reader_core_device> devices = Devices(info);
    const uint32_t ports = std::min<uint32_t>(PortCount(info), MaxPorts);
    for (uint32_t port = 0; port < ports; ++port) {
        // Who plays it: the keyboard (and the Steam Controller) with the first controller.
        QString who;
        if (port_pads[port] != nullptr) {
            who = QString::fromUtf8(SDL_GameControllerName(port_pads[port]));
        }
        if (port == 0) {
            who = who.isEmpty() ? QStringLiteral("keyboard") : who + QStringLiteral(" and the keyboard");
        }
        const QString title = QStringLiteral("Port %1: %2").arg(port + 1).arg(who.isEmpty() ? QStringLiteral("nobody") : who);
        QList<reader_core_device> fitting;
        for (const reader_core_device& device : devices) {
            if (((device.ports >> port) & 1) != 0) {
                fitting.append(device);
            }
        }
        if (fitting.size() < 2) {
            into->addAction(title)->setEnabled(false);
            continue;
        }
        // The kind of controller in it, kept for its console (handed over at each start).
        QMenu* kinds = into->addMenu(title);
        const QString setting = QStringLiteral("ports.%1").arg(port + 1);
        const QString chosen = play_settings.Raw(setting, key);
        for (const reader_core_device& device : fitting) {
            QAction* action = kinds->addAction(QString::fromUtf8(device.name));
            action->setCheckable(true);
            action->setChecked(chosen.isEmpty() ? &device == &fitting.first() : chosen.toUInt() == device.id);
            connect(action, &QAction::triggered, this, [this, port, setting, key, id = device.id] {
                play_settings.Set(setting, QString::number(id), key);
                play_settings.Save();
                if (current != nullptr && !current->session->SetPortDevice(port, id)) {
                    ReaderUi::Notify(QStringLiteral("Port %1 takes it at the next start").arg(port + 1));
                }
            });
        }
    }
}

void MainWindow::FillPatchMenu(QMenu* into) {
    into->clear();
    if (current == nullptr) {
        return;
    }
    const QString key = QString::fromUtf8(current->core.info->key);
    const QString game = playing_id;
    const uint32_t features = current->session->Features();
    if (game.isEmpty()) {
        into->addAction(QStringLiteral("The game is not known yet"))->setEnabled(false);
        return;
    }
    if ((features & READER_CORE_HAS_PATCHES) != 0) {
        const std::vector<CoreSession::Patch> patches = current->session->Patches(game.toStdString());
        // Turned on by name, one to a line; a game never set has the core's defaults.
        const QString setting = QString::fromLatin1(READER_CORE_SETTING_PATCHES);
        const bool set = play_settings.IsSet(setting, key, game);
        const QStringList on = play_settings.Raw(setting, key, game).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const CoreSession::Patch& patch : patches) {
            const QString name = QString::fromStdString(patch.name);
            QAction* action = into->addAction(name);
            action->setToolTip(QString::fromStdString(patch.about));
            action->setStatusTip(QString::fromStdString(patch.about));
            action->setCheckable(true);
            action->setChecked(set ? on.contains(name) : patch.on_by_default);
            connect(action, &QAction::toggled, this, [this, key, game, name, patches, setting](bool checked) {
                QStringList now;
                const bool was_set = play_settings.IsSet(setting, key, game);
                const QStringList before = play_settings.Raw(setting, key, game).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const CoreSession::Patch& patch : patches) {
                    const QString other = QString::fromStdString(patch.name);
                    const bool other_on = other == name ? checked : was_set ? before.contains(other) : patch.on_by_default;
                    if (other_on) {
                        now.append(other);
                    }
                }
                // Every patch off is still the game's own choice: a line nobody names.
                play_settings.Set(setting, now.isEmpty() ? QStringLiteral("-") : now.join(QLatin1Char('\n')), key, game);
                play_settings.Save();
                ApplyPlaySettings(game);
                ReaderUi::Notify(QStringLiteral("%1 %2 (for this game)").arg(name, checked ? QStringLiteral("on") : QStringLiteral("off")));
            });
        }
        if (patches.empty()) {
            into->addAction(QStringLiteral("No patches for this game"))->setEnabled(false);
        }
    }
    if ((features & READER_CORE_HAS_CODES) != 0) {
        into->addSeparator();
        const int count = static_cast<int>(play_settings.Raw(QString::fromLatin1(READER_CORE_SETTING_CODES), key, game)
                                               .split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                               .size());
        QAction* codes = into->addAction(count > 0 ? QStringLiteral("Cheat codes (%1 lines)…").arg(count)
                                                   : QStringLiteral("Cheat codes…"));
        connect(codes, &QAction::triggered, this, [this] { EditCodes(); });
    }
}

void MainWindow::EditCodes() {
    if (current == nullptr || playing_id.isEmpty()) {
        return;
    }
    const QString key = QString::fromUtf8(current->core.info->key);
    const QString setting = QString::fromLatin1(READER_CORE_SETTING_CODES);
    const QString game = playing_id;
    ReaderUi::ProgramPaused(true);
    bool ok = false;
    const QString codes = QInputDialog::getMultiLineText(
        this, QStringLiteral("Cheat codes"),
        QStringLiteral("Codes for %1, one to a line, as the console's players write them:").arg(game),
        play_settings.Raw(setting, key, game), &ok);
    ReaderUi::ProgramPaused(false);
    screen->setFocus();
    if (!ok || current == nullptr) {
        return;
    }
    play_settings.Set(setting, codes.trimmed(), key, game);
    play_settings.Save();
    ApplyPlaySettings(game);
    ReaderUi::Notify(codes.trimmed().isEmpty() ? QStringLiteral("No cheat codes") : QStringLiteral("Cheat codes in"));
}

void MainWindow::OpenSaveFolder() {
    if (current == nullptr) {
        return;
    }
    // The game's own folder where the core says it; else the console's saves folder.
    QString folder;
    if (!playing_id.isEmpty()) {
        for (const CoreSession::SaveLocation& location : current->session->SaveLocations(playing_id.toStdString())) {
            const QFileInfo info(QString::fromStdString(location.path));
            folder = info.isDir() ? info.filePath() : info.path();
            if (QFileInfo::exists(folder)) {
                break;
            }
        }
    }
    if (folder.isEmpty() || !QFileInfo::exists(folder)) {
        folder = console_folder + QStringLiteral("/saves");
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        ReaderUi::Notify(QStringLiteral("The saves are in %1").arg(QDir::toNativeSeparators(folder)));
    }
}

void MainWindow::ToggleFullScreen() {
    isFullScreen() ? showNormal() : showFullScreen();
    QSettings().setValue(QStringLiteral("full_screen"), isFullScreen());
}

bool MainWindow::PointerToGame(const QPointF& at, uint32_t flags) {
    if (current == nullptr || !current->session->Running() ||
        (current->session->Features() & READER_CORE_HAS_POINTER) == 0) {
        return false;
    }
    const uint32_t screens = PointerScreens(*current->core.info);
    const QPointF pixel = at * screen->devicePixelRatioF(); // the core's rectangles are in the window's pixels
    // Two screens: each where the core says; one: the picture.
    const auto rect_of = [this](uint32_t which) {
        CoreSession::Rect rect = current->session->ScreenRect(static_cast<int>(which));
        return rect.width <= 0 && which == 0 ? current->session->PictureRect() : rect;
    };
    for (uint32_t which = 0; which < 2; ++which) {
        const CoreSession::Rect rect = rect_of(which);
        if (((screens >> which) & 1) == 0 || rect.width <= 0 || rect.height <= 0) {
            continue;
        }
        const double x = (pixel.x() - rect.x) / rect.width, y = (pixel.y() - rect.y) / rect.height;
        if (x >= 0 && x <= 1 && y >= 0 && y <= 1) {
            pointer_screen = static_cast<int>(which);
            current->session->Pointer(which, 0, flags, static_cast<float>(x), static_cast<float>(y));
            return true;
        }
    }
    // Off the screens: a press there is not the game's; a finger let go (or moved without a
    // press) is told at the edge of the screen it was on, so that the game lets go too.
    if (flags == 0 && pointer_screen >= 0) {
        const CoreSession::Rect rect = rect_of(static_cast<uint32_t>(pointer_screen));
        if (rect.width > 0 && rect.height > 0) {
            current->session->Pointer(static_cast<uint32_t>(pointer_screen), 0, 0,
                                      static_cast<float>(std::clamp((pixel.x() - rect.x) / rect.width, 0.0, 1.0)),
                                      static_cast<float>(std::clamp((pixel.y() - rect.y) / rect.height, 0.0, 1.0)));
        }
    }
    return false;
}

bool MainWindow::ProgramCommand(const QString& name, const QStringList& parts,
                                const std::function<void(const QString&)>& reply) {
#ifndef READER_NO_SAVE_SYNC
    if (name == QStringLiteral("sync")) {
        // sync: save sync's status and conflicts; sync check | all; sync resolve TITLE mine|theirs.
        const QString what = parts.value(1);
        if (what == QStringLiteral("check")) {
            SaveSync::Host::Check();
        } else if (what == QStringLiteral("all")) {
            SaveSync::Host::SyncAll();
        } else if (what == QStringLiteral("resolve")) {
            SaveSync::Host::Resolve(parts.value(2).toStdString(), parts.value(3) == QStringLiteral("mine"));
        }
        QStringList conflicts;
        for (const SaveSync::Host::Conflict& conflict : SaveSync::Host::Conflicts()) {
            conflicts.append(QString::fromStdString(conflict.title + " " + conflict.disc + ": " + conflict.message));
        }
        reply(QStringLiteral("OK %1 | %2 | conflicts: %3")
                  .arg(SaveSync::Host::Get().enabled ? QStringLiteral("on") : QStringLiteral("off"),
                       QString::fromStdString(SaveSync::Host::Status()).replace(QLatin1Char('\n'), QStringLiteral(" / ")),
                       conflicts.isEmpty() ? QStringLiteral("none") : conflicts.join(QStringLiteral(" / "))));
        return true;
    }
#endif
    if (name == QStringLiteral("steam-axis")) {
        // steam-axis N VALUE: as if the Steam Controller had moved axis N (SteamBridge::Axis;
        // 4 is LT, 32767 pulled all the way), through what plays the game.
        steam_axes[std::clamp(parts.value(1).toInt(), 0, static_cast<int>(std::size(steam_axes)) - 1)] = parts.value(2).toInt();
        steam_seen = true;
        reply(QStringLiteral("OK"));
        return true;
    }
    if (name == QStringLiteral("settings-window")) {
        // settings-window [TAB]: the settings window, at a tab by its name.
        OpenSettings(parts.mid(1).join(QLatin1Char(' ')));
        reply(QStringLiteral("OK"));
        return true;
    }
    if (name == QStringLiteral("pad-sent")) {
        // pad-sent: the first controller as the core is handed it, and which bit L2 is there.
        std::scoped_lock lock{pads_mutex};
        reply(QStringLiteral("OK buttons=0x%1 triggers=%2,%3 l2_bit=%4")
                  .arg(gamepads[0].buttons, 0, 16)
                  .arg(gamepads[0].triggers[0])
                  .arg(gamepads[0].triggers[1])
                  .arg(current != nullptr ? BitOf(*current->core.info, Roles[10]) : -1));
        return true;
    }
    if (current == nullptr) {
        return false;
    }
    CoreSession& session = *current->session;
    if (name == QStringLiteral("slot")) {
        // slot save|load N: as the menu does it (the save with its picture); slot list.
        if (parts.value(1) == QStringLiteral("list")) {
            QStringList out;
            for (int slot = 1; slot <= StateSlots(*current->core.info); ++slot) {
                const auto info = session.StateInfo(slot);
                out.append(QStringLiteral("%1:%2%3").arg(slot)
                               .arg(!info ? QStringLiteral("?") : info->exists ? QString::number(info->saved_ms) : QStringLiteral("-"))
                               .arg(QFileInfo::exists(StatePicture(slot)) ? QStringLiteral("+picture") : QString()));
            }
            reply(QStringLiteral("OK ") + out.join(QLatin1Char(' ')));
            return true;
        }
        const int slot = parts.value(2).toInt();
        parts.value(1) == QStringLiteral("load") ? LoadStateFrom(slot) : SaveStateTo(slot);
        reply(QStringLiteral("OK"));
    } else if (name == QStringLiteral("disc")) {
        // disc: the discs and the one in; disc N (from 1; 0 opens the tray); disc add FILE.
        if (parts.size() < 2) {
            QStringList out;
            for (uint32_t i = 0; i < session.DiscCount(); ++i) {
                out.append(QString::fromStdString(session.DiscLabel(i)));
            }
            reply(QStringLiteral("OK %1 in; %2").arg(session.DiscCurrent() + 1).arg(out.join(QStringLiteral(" | "))));
        } else if (parts.value(1) == QStringLiteral("add")) {
            const int32_t index = session.AddDisc(parts.mid(2).join(QLatin1Char(' ')).toStdString());
            reply(index >= 0 ? QStringLiteral("OK %1").arg(index + 1) : QStringLiteral("ERR not added"));
        } else {
            reply(session.InsertDisc(parts.value(1).toInt() - 1) ? QStringLiteral("OK") : QStringLiteral("ERR refused"));
        }
    } else if (name == QStringLiteral("port")) {
        // port N DEVICE: the kind of controller in port N (from 1), by the core's id.
        reply(session.SetPortDevice(parts.value(1).toUInt() - 1, parts.value(2).toUInt()) ? QStringLiteral("OK")
                                                                                         : QStringLiteral("ERR refused"));
    } else if (name == QStringLiteral("patches")) {
        // patches: the game's patches, with * before those on.
        QStringList out;
        const QString key = QString::fromUtf8(current->core.info->key);
        const QStringList on = play_settings.Raw(QString::fromLatin1(READER_CORE_SETTING_PATCHES), key, playing_id)
                                   .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const CoreSession::Patch& patch : session.Patches(playing_id.toStdString())) {
            const QString patch_name = QString::fromStdString(patch.name);
            out.append((on.contains(patch_name) ? QStringLiteral("*") : QString()) + patch_name);
        }
        reply(QStringLiteral("OK %1: %2").arg(playing_id, out.join(QStringLiteral(", "))));
    } else if (name == QStringLiteral("touch")) {
        // touch X Y [up]: the pointer at the screen widget's X, Y, pressed (or let go).
        const bool up = parts.value(3) == QStringLiteral("up");
        const bool taken = PointerToGame(QPointF(parts.value(1).toDouble(), parts.value(2).toDouble()),
                                         up ? 0u : READER_CORE_POINTER_PRESSED);
        reply(taken ? QStringLiteral("OK") : QStringLiteral("ERR not on a screen that takes it"));
    } else if (name == QStringLiteral("menu")) {
        // menu: the program's menu opened (or closed), as Escape does.
        OpenMenu();
        reply(QStringLiteral("OK %1").arg(menu != nullptr && menu->isVisible() ? QStringLiteral("open") : QStringLiteral("closed")));
    } else if (name == QStringLiteral("pads")) {
        // pads: SDL's controllers, by port.
        QStringList out;
        for (int port = 0; port < MaxPorts; ++port) {
            if (port_pads[port] != nullptr) {
                out.append(QStringLiteral("%1=%2").arg(port + 1).arg(QString::fromUtf8(SDL_GameControllerName(port_pads[port]))));
            }
        }
        reply(QStringLiteral("OK ") + out.join(QStringLiteral(", ")));
    } else {
        return false;
    }
    return true;
}

void MainWindow::Rumble(uint32_t port, uint16_t strong, uint16_t weak) {
    if (port >= MaxPorts || port_pads[port] == nullptr) {
        return;
    }
    // Held until the core says otherwise (it tells each change); a long time at most.
    SDL_GameControllerRumble(port_pads[port], strong, weak, strong != 0 || weak != 0 ? 10000 : 0);
}

} // namespace Home
