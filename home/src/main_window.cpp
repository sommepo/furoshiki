// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <deque>
#include <utility>
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
#include <QClipboard>
#include "discs.h"
#include "home_window.h"
#include "hook_processes.h"
#include "hook_source.h"
#include "loopback_audio.h"
#include "line_source.h"
#include "ocr_overlay.h"
#include <QShortcut>
#include "global_key.h"
#include "ocr_source.h"
#include "core_ocr.h"
#include "ocr_helper.h"
#include "line_titles.h"
#include "text_strip.h"
#include "reader/qt/lookup_flow.h"
#include "reader/qt/reader_controller.h"
#include "library.h"
#include "addons.h"
#include "app_folders.h"
#include "main_window.h"
#include "controller_screen.h"
#include "pad_setup.h"
#include "pad_shortcuts.h"
#include <QCryptographicHash>
#include "old_settings.h"
#include "reader/core/input_filter.h"
#include "reader/qt/host.h"
#include "reader/qt/install.h"
#include "reader/qt/reader_link.h"
#include "reader/qt/reader_dialogs.h"
#include "reader/qt/reader_games.h"
#include "reader_rows.h"
#include "services.h"
#ifndef READER_NO_SAVE_SYNC // the guides come with save sync's pairing: the dev line's
#include "guide_window.h"
#endif
#ifndef HOME_PUBLIC // the updater is the dev line's
#include "updater.h"
#endif
#include "reader/qt/reader_session.h"
#include "reader/qt/lookup_popup.h"
#include "reader/desktop/popup_model.h"
#include "reader/qt/popup_web.h"
#include "crt_layers.h"
#include "popup_style.h"
#include "reader/qt/reader_crt_page.h"
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

/// The models a line can be explained with (Ctrl+E), the first the one it starts with.
QStringList ExplainModels() {
    return {QStringLiteral("claude-sonnet-5-5"), QStringLiteral("claude-opus-5-5"), QStringLiteral("claude-haiku-5-5"),
            QStringLiteral("claude-fable-5-2")};
}

} // namespace

namespace {

QHash<QString, int>& LayoutCache();
void SetStoredLayout(const QString& key, int family);

} // namespace

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
    if (event->spontaneous()) {
        ReaderUi::LookupPopup::SetPadInUse(false); // the keyboard is the input: no controller marks
    }
    if ((event->modifiers() & Qt::ControlModifier) != 0 && (event->modifiers() & Qt::ShiftModifier) != 0 && event->key() == Qt::Key_R) {
        emit ReadAskedWanted(); // the boxes read only on request, of a game read by OCR
        return;
    }
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
    MakeGuideKeys(); // Ctrl+Shift+G and N over the game (guide_window.h), on the dev line
    pages = new QStackedWidget(this);
    home = new HomeWindow(pages);
    screen = new GameScreen(pages);
    pages->addWidget(home);
    pages->addWidget(screen);
    setCentralWidget(pages);
    resize(1280, 720);
    PlaceFirst(this);
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
    // The game add-ons (addons.h): those with a companion get the companion channel, opened with
    // the reader's windows; a development run's own channel is theirs too.
    {
        QStringList problems;
        addons = FindAddons(AddonFolders(SharedFolder(), QCoreApplication::applicationDirPath()), &problems);
        for (const QString& problem : std::as_const(problems)) {
            qWarning("add-on: %s", qPrintable(problem));
        }
        QString address = qEnvironmentVariable("READER_DESKTOP_CONTROL");
#ifdef Q_OS_WIN
        if (!address.isEmpty() && !address.startsWith(QStringLiteral("\\\\"))) {
            address = QStringLiteral("\\\\.\\pipe\\") + address;
        }
#endif
        const bool companion = std::any_of(addons.cbegin(), addons.cend(),
                                           [](const Addon& addon) { return !addon.companion.isEmpty(); });
        if (companion && address.isEmpty()) {
            const CompanionChannel channel = MakeCompanionChannel(ProgramFolder());
            qputenv("READER_COMPANION_CONTROL", channel.listen.toUtf8());
            address = channel.address;
        }
        companions = new Companions(address, ProgramFolder() + QStringLiteral("/addons"), this);
        for (const Addon& addon : std::as_const(addons)) {
            qInfo("add-on %s (%s): %s", qPrintable(addon.name), qPrintable(addon.games.join(u',')),
                  qPrintable(addon.folder));
        }
    }
    popup_folder = PopupStyle::Folder(ProgramFolder());
    AddPopupSettings();
    home->SetConsoles(consoles, &play_settings);
    // The popup's settings in the layers come before the reader's own: for the game playing
    // and its console, or all consoles' on the page.
    ReaderUi::ReaderSession::Instance().Settings().SetLayered([this](const QString& key) -> std::optional<QString> {
        // The CRT picture: for the game playing, its console, all consoles (crt_layers.h).
        if (key == CrtLayers::ReaderKey) {
            const bool running = current != nullptr;
            return CrtLayers::Join(CrtLayers::Resolved(play_settings, CrtBase(),
                                                       running ? QString::fromUtf8(current->core.info->key) : QString(),
                                                       running ? playing_id : QString()));
        }
        // The controller map's (controller_map.h): the console playing's own bindings first.
        if (key == ReaderApp::ReaderSettings::KeyCursorButton || key == ReaderApp::ReaderSettings::KeyMenuButton ||
            key == ReaderApp::ReaderSettings::KeyStatsButton || key.startsWith(QStringLiteral("reader_back_button_")) ||
            key.startsWith(QStringLiteral("reader_button_action_")) || key.startsWith(QStringLiteral("pad_button_"))) {
            if (current == nullptr) {
                return std::nullopt;
            }
            const QString own = play_settings.Raw(QStringLiteral("controller.") + key, QString::fromUtf8(current->core.info->key));
            return own.isEmpty() ? std::nullopt : std::optional<QString>(own);
        }
        if (!key.startsWith(QStringLiteral("reader_popup_"))) {
            return std::nullopt;
        }
        const bool running = current != nullptr;
        return PopupStyle::Layered(play_settings, popup_folder, key,
                                   running ? QString::fromUtf8(current->core.info->key) : QString(),
                                   running ? playing_id : QString());
    });
    // The Reader menu's CRT page over a game keeps its values in the layers too: in the game's
    // own where it has them, else its console's where that has, else all consoles'.
    ReaderUi::ReaderSession::Instance().Settings().SetLayeredWrite([this](const QString& key, const QVariant& value) {
        if (key != CrtLayers::ReaderKey) {
            return false;
        }
        const bool running = current != nullptr;
        const CrtLayers::Layer layer = CrtLayers::Target(play_settings, running ? QString::fromUtf8(current->core.info->key) : QString(),
                                                         running ? playing_id : QString());
        CrtLayers::Store(play_settings, CrtBase(), CrtLayers::Parse(value.toString()), layer.console, layer.game);
        play_settings.Save();
        return true;
    });
    ReaderCrtPage::Load(ReaderUi::ReaderSession::Instance().Settings()); // all consoles' layer
    home->SetSettingsWindow([this](const QString& tab) { OpenSettings(tab); });
    home->SetPopupPreview([this](const QRect& place) {
        popup_preview_place = place;
        if (place.isEmpty()) {
            popup_preview_override_theme.clear();
            if (popup_preview != nullptr) {
                popup_preview->hide();
            }
            if (popup_preview_timer != nullptr) {
                popup_preview_timer->stop();
            }
            return;
        }
        if (popup_preview == nullptr) {
            popup_preview = ReaderUi::PopupWeb::Create(home, ReaderUi::ReaderSession::DataDirectory() + QStringLiteral("/webview2"));
            if (popup_preview == nullptr) {
                return; // no web engine here: the classic popup has no page to show
            }
            // As tall as the popup would be over a game: its entry, or the height set.
            connect(popup_preview, &ReaderUi::PopupWeb::Message, this, [this](const QJsonObject& message) {
                const QString type = message.value(QStringLiteral("type")).toString();
                if (type == QStringLiteral("ready")) {
                    popup_preview_shown.clear();
                    ShowPopupPreview();
                    return;
                }
                if (type != QStringLiteral("size") || popup_preview_place.isEmpty()) {
                    return;
                }
                auto& settings = ReaderUi::ReaderSession::Instance().Settings();
                const int content = static_cast<int>(std::ceil(message.value(QStringLiteral("height")).toDouble()));
                const int entry = static_cast<int>(std::ceil(message.value(QStringLiteral("fit")).toDouble(content)));
                const int need = static_cast<int>(std::ceil(message.value(QStringLiteral("need")).toDouble(0)));
                const int tall = settings.PopupFitEntry() ? std::min(entry, height() * ReaderApp::ReaderSettings::PopupFitLimitPercent / 100)
                                                          : std::min(content, std::max(need, height() * settings.PopupHeightPercent() / 100));
                popup_preview->resize(popup_preview->width(), std::clamp(tall, 40, popup_preview_place.height()));
            });
            popup_preview_timer = new QTimer(this);
            popup_preview_timer->setInterval(350);
            connect(popup_preview_timer, &QTimer::timeout, this, &MainWindow::ShowPopupPreview);
        }
        popup_preview->setGeometry(place.x(), place.y(), std::min(place.width(), std::max(360, place.width() * 62 / 100)), popup_preview->isVisible() ? popup_preview->height() : 60);
        popup_preview->show();
        popup_preview->raise();
        popup_preview_shown.clear();
        ShowPopupPreview();
        popup_preview_timer->start();
    });
    const auto popup_theme_setting = [this]() -> PlaySetting {
        for (const PlaySetting& s : PopupStyle::Settings(popup_folder)) {
            if (s.key == QStringLiteral("popup.theme")) {
                return s;
            }
        }
        return {};
    };
    home->SetPopupThemePreview([this, popup_theme_setting](int index) {
        const PlaySetting theme = popup_theme_setting();
        popup_preview_override_theme = index >= 0 && index < theme.choices.size() ? theme.choices[index] : QString();
        ShowPopupPreview();
    });
    // The Reader settings' popup page opens this program's customiser (the settings window's Popup tab).
    ReaderUi::PopupCustomiser() = [this] { OpenSettings(QStringLiteral("Popup")); };
    // What the paired save sync service says of the user's other services, a moment after the start.
    QTimer::singleShot(4000, this, [this] { AskHomeServer(false); });
    // A line is explained by Furochibi's service where the device knows it (services.h).
    ReaderUi::ExplainService() = [] { return Home::Services::Of(QStringLiteral("furochibi")); };
    // Cards go to the user's Anki on another computer when none answers here (services.h).
    ReaderUi::AnkiElsewhere() = [] { return Home::Services::Of(QStringLiteral("anki")); };
    // A card's tag is the program's, not the running core's app key ("ppsspp-reader").
    ReaderApp::ReaderSettings::AnkiTagsDefault() = QStringLiteral("furoshiki");
    // The Reader menu's settings are the pages too.
    ReaderUi::SettingsPages() = [this] { ShowSettingsPages(); };
    // The dictionaries are a page of the settings (Settings › Dictionaries), not a window of their own.
    dictionary_shelf = Home::MakeReaderDictionaryShelf();
    home->SetDictionaries(dictionary_shelf.get());
    // Every console's game scripts, their names and box art: a page of the settings too.
    script_shelf = Home::MakeReaderScriptShelf();
    script_shelf->scripts_changed = [this] { FindGames(); };
    script_shelf->dictionaries_changed = [this] {
        if (dictionary_shelf) {
            dictionary_shelf->Refresh();
        }
    };
    home->SetScripts(script_shelf.get());
    ReaderUi::DictionaryPages() = [this] {
        ShowSettingsPages();
        home->ShowDictionaries();
    };
    // L2 was the pointer's by default, and the Reader settings window wrote that default down
    // on saving, so a stored L2 says nothing of a choice. Once, it goes back to the games.
    if (QSettings program; !program.contains(QStringLiteral("pointer_button_left_to_games"))) {
        program.setValue(QStringLiteral("pointer_button_left_to_games"), true);
        if (ReaderUi::ReaderSession::Instance().Settings().CursorButton() == 104) {
            ReaderUi::SetPointerButton(-1);
        }
    }
    // The CRT picture off once for everyone (the maintainer, 2026-11-24: "are we still forcing crt
    // filters off in settings for users? we should do on first load, and actually even if they
    // loaded before"): every kept line that switches it on, in reader.ini and in every layer, is
    // switched off with its other values kept; switched on again after this, it stays on.
    if (QSettings program; !program.contains(QStringLiteral("crt_switched_off_once"))) {
        int changed = 0;
        bool play_changed = false;
        auto& reader = ReaderUi::ReaderSession::Instance().Settings();
        const QString own = reader.FileValue(CrtLayers::ReaderKey).toString();
        if (const QString off = CrtLayers::Off(own); off != own) {
            reader.SetFileValue(CrtLayers::ReaderKey, off);
            reader.Sync();
            ++changed;
        }
        for (const auto& [console, game] : play_settings.LayersWith(CrtLayers::Key)) {
            const QString kept = play_settings.Raw(CrtLayers::Key, console, game);
            if (const QString off = CrtLayers::Off(kept); off != kept) {
                play_settings.Set(CrtLayers::Key, off, console, game);
                play_changed = true;
                ++changed;
            }
        }
        if (play_changed) {
            play_settings.Save();
        }
        if (changed > 0) {
            ReaderCrtPage::Load(reader);
        }
        program.setValue(QStringLiteral("crt_switched_off_once"), true);
        qInfo("CRT picture switched off once: %d kept settings changed", changed);
    }
    // Reading time counted before it was tidied when a game stops (2026-11-02): once, every
    // earlier session is tidied too (ReaderDatabase::TidyReadingTime; the Reading tab of the
    // settings window puts it back or does it again).
    if (QSettings program; !program.contains(QStringLiteral("reading_time_tidied"))) {
        program.setValue(QStringLiteral("reading_time_tidied"), true);
        ReaderUi::ReaderSession::Instance().TidyReadingTime([this](const ReaderApp::TidyReport& report) {
            qInfo("reading time tidied once: %s", qPrintable(TidyReportText(report)));
            StatsChanged();
        });
    }
    home->SetSettingStep([this](const QString& row, int step) {
        using Settings = ReaderApp::ReaderSettings;
        auto& session = ReaderUi::ReaderSession::Instance();
        Settings& settings = session.Settings();
        // The reader's own rows (reader_rows.h), then the few that are something else.
        if (ReaderRows::IsNumber(row) && step == 0) {
            return;
        }
        if (row == QStringLiteral("Popup theme")) {
            const PlaySetting theme = PopupStyle::Settings(popup_folder).value(0);
            if (theme.key == QStringLiteral("popup.theme") && !theme.choices.isEmpty()) {
                const QString current_theme = play_settings.Value(theme, {}).value;
                const int now = std::max<int>(0, static_cast<int>(theme.choices.indexOf(current_theme)));
                const int delta = step == 0 ? 1 : step;
                const int count = static_cast<int>(theme.choices.size());
                const int next = ((now + delta) % count + count) % count;
                play_settings.Set(QStringLiteral("popup.theme"), theme.choices[next]);
                play_settings.Clear(QStringLiteral("popup.design"));
                play_settings.Save();
                popup_preview_override_theme.clear();
                ApplyPlaySettings(playing_id);
                ShowPopupPreview();
            }
            return;
        }
        if (row == QStringLiteral("How solid")) {
            // 4 fixed opacity choices: 100%, 80%, 60%, 40%.
            static const QStringList choices = {QStringLiteral("100"), QStringLiteral("80"), QStringLiteral("60"), QStringLiteral("40")};
            const QString raw = play_settings.Raw(QStringLiteral("popup.opacity"));
            const int cur_val = raw.isEmpty() ? 100 : raw.toInt();
            int now = cur_val >= 90 ? 0 : cur_val >= 70 ? 1 : cur_val >= 50 ? 2 : 3;
            const int delta = step == 0 ? 1 : step;
            const int next = ((now + delta) % choices.size() + choices.size()) % choices.size();
            play_settings.Set(QStringLiteral("popup.opacity"), choices[next]);
            play_settings.Save();
            ApplyPlaySettings(playing_id);
            ShowPopupPreview();
            return;
        }
        if (row == QStringLiteral("Corner radius")) {
            // 3 fixed corner choices: Sharp (0px), Rounded (12px), Rounded+ (24px).
            static const QStringList choices = {QStringLiteral("0"), QStringLiteral("12"), QStringLiteral("24")};
            const QString raw = play_settings.Raw(QStringLiteral("popup.radius"));
            const int cur_val = raw.isEmpty() ? 0 : raw.toInt();
            int now = cur_val >= 18 ? 2 : cur_val >= 6 ? 1 : 0;
            const int delta = step == 0 ? 1 : step;
            const int next = ((now + delta) % choices.size() + choices.size()) % choices.size();
            play_settings.Set(QStringLiteral("popup.radius"), choices[next]);
            play_settings.Save();
            ApplyPlaySettings(playing_id);
            ShowPopupPreview();
            return;
        }
        for (const PlaySetting& ps : PopupStyle::Settings(popup_folder)) {
            if (ps.label == row && ps.key != QStringLiteral("popup.theme") && ps.key != QStringLiteral("popup.design")) {
                if (ps.kind == PlaySetting::Switch) {
                    const bool on = play_settings.Value(ps, {}).value == QStringLiteral("1");
                    play_settings.Set(ps.key, on ? QStringLiteral("0") : QStringLiteral("1"));
                } else if (ps.kind == PlaySetting::Choice && !ps.choices.isEmpty()) {
                    const QString cur = play_settings.Value(ps, {}).value;
                    const int now = std::max<int>(0, static_cast<int>(ps.choices.indexOf(cur)));
                    const int delta = step == 0 ? 1 : step;
                    const int count = static_cast<int>(ps.choices.size());
                    play_settings.Set(ps.key, ps.choices[((now + delta) % count + count) % count]);
                } else if (ps.kind == PlaySetting::Number && step != 0) {
                    const int cur = play_settings.Value(ps, {}).value.toInt();
                    const int inc = std::max(1, static_cast<int>(ps.step));
                    const int next = std::clamp(cur + step * inc, static_cast<int>(ps.low), static_cast<int>(ps.high));
                    play_settings.Set(ps.key, QString::number(next));
                }
                play_settings.Save();
                ApplyPlaySettings(playing_id);
                ShowPopupPreview();
                return;
            }
        }
        if (ReaderRows::Step(settings, row, step == 0 ? 1 : step)) {
            session.OnSettingsChanged();
            return;
        }
        if (row == QStringLiteral("Pointer button")) {
            const QList<int> buttons = ReaderUi::PointerButtons();
            const int now = buttons.indexOf(settings.CursorButton());
            ReaderUi::SetPointerButton(buttons[(std::max(now, 0) + step + buttons.size()) % buttons.size()]);
        } else if (row == QStringLiteral("Explain with")) {
            const QStringList models = ExplainModels();
            const int now = std::max<int>(0, models.indexOf(settings.ExplainModel()));
            settings.SetValue(Settings::KeyExplainModel, models[(now + step + models.size()) % models.size()]);
            settings.Sync();
        } else if (row == QStringLiteral("Customise the popup")) {
            OpenSettings(QStringLiteral("Popup"));
        } else if (row == QStringLiteral("Map the controller")) {
            OpenControllerMap();
        } else if (row == QStringLiteral("Controller layout")) {
            // The letters on the controller in use, changed by hand (its setup's answer).
            if (SDL_GameController* pad = ActivePad(); pad != nullptr && !steam_last) {
                const int now = StoredLayout(PadKey(pad)) >= 0 ? StoredLayout(PadKey(pad)) : ReportedLayout();
                static constexpr int order[] = {Home::PadNames::Xbox, Home::PadNames::Nintendo, Home::PadNames::PlayStation};
                const int at = static_cast<int>(std::find(std::begin(order), std::end(order), now) - std::begin(order));
                SetStoredLayout(PadKey(pad), order[((at + step) % 3 + 3) % 3]);
            }
        } else if (row == QStringLiteral("Set up this controller again")) {
            if (SDL_GameController* pad = ActivePad(); pad != nullptr && !steam_last) {
                const QString key = PadKey(pad);
                SetStoredLayout(key, -1);
                pad_setup_skipped.remove(key);
                OpenPadSetup(key, PadInUseName(), ReportedLayout());
            } else {
                home->Say(QStringLiteral("No controller found. Connect one and press a button on it."));
            }
        } else if (row == QStringLiteral("CRT picture")) {
            OpenSettings(QStringLiteral("CRT"));
        } else if (row == QStringLiteral("Tidy reading time")) {
            OpenSettings(QStringLiteral("Reading"));
        } else if (row == QStringLiteral("Dictionaries")) {
            ReaderUi::RunDictionariesDialog(this);
            if (dictionary_shelf) {
                dictionary_shelf->Refresh();
            }
        } else if (row == QStringLiteral("Download Jitendex")) {
            // The first steps' dictionary (welcome_page.inc): fetched and installed, and said.
            QString problem;
            if (ReaderUi::FetchStarterDictionary(this, &problem)) {
                if (dictionary_shelf) {
                    dictionary_shelf->Refresh();
                }
                home->Say(QStringLiteral("Jitendex installed"));
            } else if (!problem.isEmpty()) {
                home->Say(QStringLiteral("Couldn't install Jitendex: %1").arg(problem));
            }
        } else if (row == QStringLiteral("Anki")) {
            ReaderUi::RunAnkiSetupDialog(this);
        } else if (row == QStringLiteral("Discord application ID")) {
            bool ok = false;
            const QString id = QInputDialog::getText(this, row, QStringLiteral("Application ID (17–20 digits)"),
                                                     QLineEdit::Normal, settings.DiscordApplicationId(), &ok).trimmed();
            if (ok) {
                settings.SetValue(Settings::KeyDiscordApplicationId, id);
                settings.Sync();
                session.OnSettingsChanged();
            }
#ifndef HOME_PUBLIC
        } else if (row == QStringLiteral("Update")) {
            // Enter looks; with a build offered, Enter takes it.
            if (!update_offer.IsNone()) {
                home->Say(QStringLiteral("Downloading update…"));
                Home::Updater::Take(this, update_offer, [this](const QString& error) {
                    if (error.isEmpty()) {
                        update_offer = {};
                        update_said = QStringLiteral("RESTART TO UPDATE");
                        home->Say(QStringLiteral("Update installed. Restart Furoshiki to finish"));
                    } else {
                        home->Say(QStringLiteral("Update failed: %1").arg(error));
                    }
                    home->Refresh();
                });
            } else {
                CheckUpdate(true);
            }
#endif
        } else if (row == QStringLiteral("Home server's services")) {
            AskHomeServer(true);
        } else if (row == QStringLiteral("Study tracker")) {
            bool ok = false;
            const QString url = QInputDialog::getText(this, row, QStringLiteral("Study tracker address"), QLineEdit::Normal,
                                                      home->TrackerUrl(), &ok).trimmed();
            if (ok) {
                home->SetTrackerUrl(url);
            }
        }
    });
    home->SetSettingChoices([this](const QString& row) -> std::pair<QStringList, int> {
        const ReaderApp::ReaderSettings& settings = ReaderUi::ReaderSession::Instance().Settings();
        if (row == QStringLiteral("Popup theme")) {
            const PlaySetting theme = PopupStyle::Settings(popup_folder).value(0);
            QStringList said;
            for (const QString& choice : theme.choices) {
                said.append(theme.Said(choice));
            }
            const QString current_theme = play_settings.Value(theme, {}).value;
            return {said, std::max<int>(0, static_cast<int>(theme.choices.indexOf(current_theme)))};
        }
        if (row == QStringLiteral("Corner radius")) {
            const QString raw = play_settings.Raw(QStringLiteral("popup.radius"));
            const int cur_val = raw.isEmpty() ? 0 : raw.toInt();
            const int now = cur_val >= 18 ? 2 : cur_val >= 6 ? 1 : 0;
            return {{QStringLiteral("Sharp"), QStringLiteral("Rounded"), QStringLiteral("Rounded+")}, now};
        }
        if (row == QStringLiteral("How solid")) {
            const QString raw = play_settings.Raw(QStringLiteral("popup.opacity"));
            const int cur_val = raw.isEmpty() ? 100 : raw.toInt();
            const int now = cur_val >= 90 ? 0 : cur_val >= 70 ? 1 : cur_val >= 50 ? 2 : 3;
            return {{QStringLiteral("100%"), QStringLiteral("80%"), QStringLiteral("60%"), QStringLiteral("40%")}, now};
        }
        for (const PlaySetting& ps : PopupStyle::Settings(popup_folder)) {
            if (ps.label == row && ps.kind == PlaySetting::Choice && !ps.choices.isEmpty()) {
                QStringList said;
                for (const QString& c : ps.choices) {
                    said.append(ps.Said(c));
                }
                const QString cur = play_settings.Value(ps, {}).value;
                return {said, std::max<int>(0, static_cast<int>(ps.choices.indexOf(cur)))};
            }
        }
        if (row == QStringLiteral("Controller layout")) {
            SDL_GameController* pad = ActivePad();
            if (pad == nullptr || steam_last) {
                return {};
            }
            const int now = StoredLayout(PadKey(pad)) >= 0 ? StoredLayout(PadKey(pad)) : ReportedLayout();
            return {{QStringLiteral("Xbox"), QStringLiteral("Nintendo"), QStringLiteral("PlayStation")},
                    now == Home::PadNames::Nintendo ? 1 : now == Home::PadNames::PlayStation ? 2 : 0};
        }
        if (row == QStringLiteral("Pointer button")) {
            QStringList said;
            const QList<int> buttons = ReaderUi::PointerButtons();
            for (const int button : buttons) {
                said.append(ReaderUi::PointerButtonName(button));
            }
            return {said, static_cast<int>(buttons.indexOf(settings.CursorButton()))};
        }
        if (row == QStringLiteral("Explain with")) {
            const QStringList models = ExplainModels();
            QStringList said;
            for (const QString& model : models) {
                // "claude-sonnet-5-5" as Sonnet 5.5
                QStringList words = model.mid(7).split(QLatin1Char('-'));
                QString first = words.isEmpty() ? model : words.takeFirst();
                if (!first.isEmpty()) {
                    first[0] = first[0].toUpper();
                }
                said.append(first + QLatin1Char(' ') + words.join(QLatin1Char('.')));
            }
            return {said, static_cast<int>(models.indexOf(settings.ExplainModel()))};
        }
        int now = -1;
        QStringList said = ReaderRows::Choices(settings, row, &now);
        return {said, now};
    });
    home->SetSettingReset(
        [this](const QString& row) {
            if (row == QStringLiteral("Popup theme")) {
                return play_settings.IsSet(QStringLiteral("popup.theme"));
            }
            if (row == QStringLiteral("How solid")) {
                return play_settings.IsSet(QStringLiteral("popup.opacity"));
            }
            if (row == QStringLiteral("Corner radius")) {
                return play_settings.IsSet(QStringLiteral("popup.radius"));
            }
            for (const PlaySetting& ps : PopupStyle::Settings(popup_folder)) {
                if (ps.label == row) {
                    return play_settings.IsSet(ps.key);
                }
            }
            return ReaderRows::IsSet(ReaderUi::ReaderSession::Instance().Settings(), row);
        },
        [this](const QString& row) {
            if (row == QStringLiteral("Popup theme")) {
                play_settings.Clear(QStringLiteral("popup.theme"));
                play_settings.Clear(QStringLiteral("popup.design"));
                play_settings.Save();
                popup_preview_override_theme.clear();
                ApplyPlaySettings(playing_id);
                ShowPopupPreview();
                return;
            }
            if (row == QStringLiteral("How solid")) {
                play_settings.Clear(QStringLiteral("popup.opacity"));
                play_settings.Save();
                ApplyPlaySettings(playing_id);
                ShowPopupPreview();
                return;
            }
            if (row == QStringLiteral("Corner radius")) {
                play_settings.Clear(QStringLiteral("popup.radius"));
                play_settings.Save();
                ApplyPlaySettings(playing_id);
                ShowPopupPreview();
                return;
            }
            for (const PlaySetting& ps : PopupStyle::Settings(popup_folder)) {
                if (ps.label == row) {
                    play_settings.Clear(ps.key);
                    play_settings.Save();
                    ApplyPlaySettings(playing_id);
                    ShowPopupPreview();
                    return;
                }
            }
            auto& session = ReaderUi::ReaderSession::Instance();
            if (ReaderRows::Reset(session.Settings(), row)) {
                session.OnSettingsChanged();
            }
        });
    // The shelf's stage and Log show what the reader kept of a title on this device.
    home->SetTitleReading([](const QString& game) {
        HomeWindow::TitleReading reading;
        ReaderApp::ReaderDatabase& database = ReaderUi::ReaderSession::Instance().Database();
        for (const ReaderApp::BacklogEntry& entry : database.RecentLines(game, 200)) {
            reading.lines.prepend({entry.line.speaker, entry.line.text, entry.line.timestamp});
        }
        reading.line_count = database.LineCount(game);
        for (const auto& day : database.DailyHistory(game, 30)) {
            reading.days.append({day.first, day.second.characters});
        }
        return reading;
    });
    home->SetSettingValue([this](const QString& row) -> QString {
        if (row == QStringLiteral("Start or stop reading")) {
            // The Keys page's row: what Settings, Controls has for it, in the controller's words.
            const QStringList caps = ReadStartCaps();
            return caps.isEmpty() ? QStringLiteral("Always during dialogue") : QStringLiteral("Controller: ") + caps.join(QStringLiteral(" + "));
        }
        if (row == QStringLiteral("Controller layout")) {
            SDL_GameController* pad = ActivePad();
            if (pad == nullptr || steam_last) {
                return home->Steam().Connected() ? QStringLiteral("STEAM CONTROLLER") : QStringLiteral("NO CONTROLLER");
            }
            const int now = StoredLayout(PadKey(pad)) >= 0 ? StoredLayout(PadKey(pad)) : ReportedLayout();
            return now == Home::PadNames::Nintendo ? QStringLiteral("NINTENDO") : now == Home::PadNames::PlayStation ? QStringLiteral("PLAYSTATION") : QStringLiteral("XBOX");
        }
        if (row == QStringLiteral("Guide's next step over the game")) {
            return QStringLiteral("Ctrl+Shift+G · ") + pad_names.select + QLatin1Char('+') + pad_names.l1;
        }
        if (row == QStringLiteral("Tick the guide's step shown")) {
            return QStringLiteral("Ctrl+Shift+N · ") + pad_names.select + QLatin1Char('+') + pad_names.r1;
        }
        if (row == QStringLiteral("RetroAchievements")) {
            const QString user = play_settings.Raw(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_USER));
            if (!user.isEmpty()) {
                return user.toUpper();
            }
            return qEnvironmentVariable("READER_RA_USER").isEmpty() ? QStringLiteral("NOT SIGNED IN") : QStringLiteral("SET BY THE LAUNCHER");
        }
        if (row == QStringLiteral("Pointer button")) {
            return ReaderUi::PointerButtonName(ReaderUi::ReaderSession::Instance().Settings().CursorButton()).toUpper();
        }
        if (row == QStringLiteral("Dictionaries")) {
            // How many are installed: with none, no word can be looked up.
            try {
                const qsizetype count = ReaderUi::ReaderSession::Instance().Dictionaries().Dictionaries().size();
                return count == 0 ? QStringLiteral("NONE INSTALLED") : QStringLiteral("%1 INSTALLED").arg(count);
            } catch (const std::exception&) {
                return QString();
            }
        }
        const ReaderApp::ReaderSettings& settings = ReaderUi::ReaderSession::Instance().Settings();
        if (const QString said = ReaderRows::Value(settings, row); !said.isEmpty()) {
            return said;
        }
        if (row == QStringLiteral("Popup theme")) {
            const PlaySetting theme = PopupStyle::Settings(popup_folder).value(0);
            return theme.Said(play_settings.Value(theme, {}).value).toUpper();
        }
        if (row == QStringLiteral("How solid")) {
            const QString raw = play_settings.Raw(QStringLiteral("popup.opacity"));
            const int now = raw.isEmpty() ? 100 : raw.toInt();
            return now >= 90 ? QStringLiteral("100%") : now >= 70 ? QStringLiteral("80%") : now >= 50 ? QStringLiteral("60%") : QStringLiteral("40%");
        }
        if (row == QStringLiteral("Corner radius")) {
            const QString raw = play_settings.Raw(QStringLiteral("popup.radius"));
            const int now = raw.isEmpty() ? 0 : raw.toInt();
            return now >= 18 ? QStringLiteral("ROUNDED+") : now >= 6 ? QStringLiteral("ROUNDED") : QStringLiteral("SHARP");
        }
        for (const PlaySetting& ps : PopupStyle::Settings(popup_folder)) {
            if (ps.label == row && ps.key != QStringLiteral("popup.theme") && ps.key != QStringLiteral("popup.design")) {
                return ps.Said(play_settings.Value(ps, {}).value).toUpper();
            }
        }
        if (row == QStringLiteral("Explain with")) {
            // "claude-sonnet-5-5" as SONNET 5.5
            QStringList words = settings.ExplainModel().mid(7).split(QLatin1Char('-'));
            const QString name = words.isEmpty() ? QString() : words.takeFirst();
            return (name + QLatin1Char(' ') + words.join(QLatin1Char('.'))).toUpper();
        }
#ifndef HOME_PUBLIC
        if (row == QStringLiteral("Update")) {
            return update_said;
        }
#endif
        if (row == QStringLiteral("Home server's services")) {
            const QMap<QString, QString> known = Home::Services::All();
            return known.isEmpty() ? QStringLiteral("NONE") : QStringList(known.keys()).join(QStringLiteral(" · ")).toUpper();
        }
        if (row == QStringLiteral("Study tracker")) {
            return home->TrackerUrl().isEmpty() ? QStringLiteral("NOT SET") : home->TrackerUrl();
        }
        if (row == QStringLiteral("Discord application ID")) {
            return settings.DiscordApplicationId().isEmpty() ? QStringLiteral("NOT SET") : settings.DiscordApplicationId();
        }
        if (row == QStringLiteral("Anki")) {
            if (settings.AnkiTransport() != QStringLiteral("ankiconnect")) {
                return QStringLiteral("OFF");
            }
            // Cards that wait for an Anki that answers are said with the deck.
            const int waiting = ReaderUi::Miner::Waiting();
            const QString deck = settings.AnkiDeck().isEmpty() ? QStringLiteral("NOT SET") : settings.AnkiDeck();
            return waiting > 0 ? QStringLiteral("%1 · %2 WAITING").arg(deck).arg(waiting) : deck;
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
            const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Export popup style"),
                                                              popup_folder + QLatin1Char('/') + name,
                                                              QStringLiteral("Popup style (*.json)"));
            if (file.isEmpty()) {
                return QString();
            }
            QSaveFile out(file);
            if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(style).toJson()) < 0 || !out.commit()) {
                return QStringLiteral("Couldn't save style to %1").arg(QDir::toNativeSeparators(file));
            }
            return QStringLiteral("Popup style exported to %1").arg(QDir::toNativeSeparators(file));
        },
        [this](const QString& console, const QString& game) {
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import popup style"), popup_folder,
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
                return QStringLiteral("Couldn't import: %1").arg(failed);
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
                          [this](const QString& key) {
                              announce_console = key; // what was found there is said once it is known
                              FindGames();
                          });
    home->SetConsoleScripts([this](const QString& key) { OpenGameScripts(key); });
    // The feet of the pages show a controller's buttons while one is the input (as the popup marks
    // its own: LookupPopup::PadInUse, set in PollPads), and keys again once the keyboard or mouse is used.
    // The first steps, on a first start with nothing set up: once the window is up and the
    // library was read. Not in a test run, which asks for them itself.
    if (qEnvironmentVariableIsEmpty("READER_DESKTOP_CONTROL")) {
        QTimer::singleShot(900, this, [this] {
            if (current == nullptr) {
                home->WelcomeIfFirst();
            }
        });
    }
    ReaderUi::LookupPopup::SetFeedback([this](const QString& kind) {
        // A card made has its tick; going from entry to entry is silent (the maintainer, 2026-11-20).
        if (kind == QStringLiteral("land")) {
            home->Tick(true);
        }
    });
    home->SetPadInput([this] { return ReaderUi::LookupPopup::PadInUse() ? pad_style : -1; },
                      [] { ReaderUi::LookupPopup::SetPadInUse(false); });
    // The titles read from lines, listed as a console's games are; the page's own commands for them.
    home->SetLinesChanged([this] { WriteLinesFile(); });
    // A console's games that no script reads, for the wizard's way of reading one by hook.
    home->SetUnscriptedGames([this](const QString& key) {
        QList<HomeWindow::UnscriptedGame> games;
        for (const OwnGame& game : unscripted[key]) {
            games.append({game.id, game.title, game.path});
        }
        return games;
    });
    {
        auto* watch = new QTimer(this);
        watch->setInterval(3000);
        connect(watch, &QTimer::timeout, this, [this] { WatchHooks(); });
        watch->start();
    }
    home->SetScriptCommands([this](const QString& word, const QString& rest) {
        // pad-setup [NAME]: a controller's first-use setup, for pictures (nothing is kept);
        // pad-setup-press south: a button pressed in it, by place. controller-map: the map.
        if (word == QStringLiteral("pad-setup")) {
            OpenPadSetup({}, rest.trimmed().isEmpty() ? QStringLiteral("8BitDo Pro 2") : rest.trimmed(), Home::PadNames::Xbox);
            return true;
        }
        if (word == QStringLiteral("pad-setup-press")) {
            if (pad_setup != nullptr) {
                static_cast<Home::PadSetup*>(pad_setup.data())->Pressed(rest.trimmed());
            }
            if (pad_setup != nullptr) {
                static_cast<Home::PadSetup*>(pad_setup.data())->Released(rest.trimmed());
            }
            return true;
        }
        // pad-setup-chord south select: two buttons held together in the setup, then let go.
        if (word == QStringLiteral("pad-setup-chord")) {
            const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                if (pad_setup != nullptr) {
                    static_cast<Home::PadSetup*>(pad_setup.data())->Pressed(part);
                }
            }
            for (const QString& part : parts) {
                if (pad_setup != nullptr) {
                    static_cast<Home::PadSetup*>(pad_setup.data())->Released(part);
                }
            }
            return true;
        }
        if (word == QStringLiteral("controller-map")) {
            OpenControllerMap(rest.trimmed() == QStringLiteral("shortcuts"));
            return true;
        }
        // pad-virtual [NAME]: a made-up controller plugged in (SDL's virtual joystick), so that a
        // test goes through everything a real pad's press does; pad-virtual-button N 1|0 presses
        // or lets go its button N (SDL's number); pad-virtual-off unplugs it.
        if (word == QStringLiteral("pad-virtual")) {
            static QByteArray name;
            name = (rest.trimmed().isEmpty() ? QStringLiteral("Test Pad") : rest.trimmed()).toUtf8();
            SDL_VirtualJoystickDesc desc;
            SDL_zero(desc);
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            desc.naxes = SDL_CONTROLLER_AXIS_MAX;
            desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            desc.vendor_id = 0x1234;
            desc.product_id = 0x5678;
            desc.name = name.constData();
            qInfo("controllers: virtual pad attached at %d", SDL_JoystickAttachVirtualEx(&desc));
            return true;
        }
        if (word == QStringLiteral("pad-virtual-button")) {
            const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (int i = 0; i < SDL_NumJoysticks(); ++i) {
                if (SDL_JoystickIsVirtual(i)) {
                    if (SDL_Joystick* joystick = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(i))) {
                        SDL_JoystickSetVirtualButton(joystick, parts.value(0).toInt(), static_cast<Uint8>(parts.value(1).toInt()));
                    }
                }
            }
            return true;
        }
        if (word == QStringLiteral("pad-virtual-off")) {
            for (int i = SDL_NumJoysticks() - 1; i >= 0; --i) {
                if (SDL_JoystickIsVirtual(i)) {
                    SDL_JoystickDetachVirtual(i);
                }
            }
            return true;
        }
        // map-press south: a controller's button pressed on the controller map, as a test has it.
        if (word == QStringLiteral("map-press")) {
            if (controller_screen != nullptr) {
                controller_screen->Pressed(rest.trimmed(), true);
                controller_screen->Pressed(rest.trimmed(), false);
            }
            return true;
        }
        // pad-hold N: the controller's button N (SDL's number) held, as the development channel's
        // pad-hold has it; -1 lets all go. Then what the console is handed, in the log.
        if (word == QStringLiteral("pad-hold")) {
            const int button = rest.trimmed().toInt();
            steam_buttons = button >= 0 && button < 32 ? 1U << button : 0;
            steam_seen = true;
            return true;
        }
        // read WHAT: reading with the d-pad, a button's doing said by its name (ReadCommand).
        if (word == QStringLiteral("read")) {
            qInfo("read %s: %s", qPrintable(rest.trimmed()), qPrintable(ReadCommand(rest.trimmed())));
            return true;
        }
        if (word == QStringLiteral("pad-say")) {
            std::scoped_lock lock{pads_mutex};
            qInfo("pad: buttons=0x%llx, popup %s", static_cast<unsigned long long>(gamepads[0].buttons),
                  OpenPopup() != nullptr ? "open" : "closed");
            return true;
        }
        return LinesCommand(word, rest);
    });
    if (!Lines::Load(SharedFolder()).titles.isEmpty()) {
        WriteLinesFile();
    }
    // The games of the consoles played here, listed by Furoshiki's page as an app's are.
    QTimer::singleShot(0, this, [this] { FindGames(); });
    home->SetCorePlayer([this](const QString& app, const QString& path) {
        QString error;
        if (app == Lines::AppKey) {
            if (!ReadLines(path, error)) {
                home->Notify(error);
            }
            return true;
        }
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
    connect(screen, &GameScreen::ReadAskedWanted, this, [this] { ReadAskedBoxes(); });
    home->SetCoreOcrAdder([this](const QString& console, const QString& path) { return AddCoreOcrGame(console, path); });
    connect(screen, &GameScreen::ReaderMenuWanted, this, [this] {
        OpenReadingMenu();
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
    // SDL's buttons by their place (south, east, west, north) on every pad, a Nintendo one too:
    // the program names them by the pad's kind (pad_names.h), and the game has them by place.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    // SDL's own ways of finding controllers are left as SDL has them (XInput, raw input and
    // HIDAPI on Windows): SDL makes sure one pad is opened once. The log says which way found
    // each (its GUID and path), so a pad seen twice would show there.
#if defined(_WIN32)
    // On Windows SDL 2 reads pads through raw input by default, whose messages go to a window of
    // the thread that started it and are lost when nothing hands them on (the maintainer's
    // 8BitDo, 2026-11-20: added through raw input, then not one press in the log). XInput pads
    // (the 8BitDo in X-input mode, Xbox pads) are read through XInput instead, which is asked
    // for each time; SDL's own thread takes the device messages that are left.
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
#endif
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    {
        // Mappings for pads SDL knows no layout of: the bundled SDL_GameControllerDB beside the
        // program (PPSSPP's assets in an older package), then the user's own file in the
        // program's folder, whose lines win.
        const QString beside = QCoreApplication::applicationDirPath();
        QStringList files;
        for (const QString& file : {beside + QStringLiteral("/gamecontrollerdb.txt"), beside + QStringLiteral("/cores/ppsspp/gamecontrollerdb.txt")}) {
            if (QFileInfo::exists(file)) {
                files.append(file);
                break;
            }
        }
        if (const QString own = ProgramFolder() + QStringLiteral("/gamecontrollerdb.txt"); QFileInfo::exists(own)) {
            files.append(own);
        }
#if defined(_WIN32)
        const QString platform = QStringLiteral("platform:Windows");
#elif defined(__APPLE__)
        const QString platform = QStringLiteral("platform:Mac OS X");
#else
        const QString platform = QStringLiteral("platform:Linux");
#endif
        for (const QString& file : std::as_const(files)) {
            const int added = SDL_GameControllerAddMappingsFromFile(QDir::toNativeSeparators(file).toUtf8().constData());
            qInfo("controllers: %d mappings from %s%s", std::max(added, 0), qPrintable(QDir::toNativeSeparators(file)),
                  added < 0 ? qPrintable(QStringLiteral(" (%1)").arg(QString::fromUtf8(SDL_GetError()))) : "");
            if (QFile in(file); in.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!in.atEnd()) {
                    const QString line = QString::fromUtf8(in.readLine()).trimmed();
                    if (!line.startsWith(QLatin1Char('#')) && line.contains(platform)) {
                        bundled_mappings.insert(line.section(QLatin1Char(','), 0, 0).toLower());
                    }
                }
            }
        }
        if (files.isEmpty()) {
            qInfo("controllers: no gamecontrollerdb.txt beside the program; SDL's own mappings only");
        }
    }
    connect(&home->Steam(), &SteamPad::ButtonChanged, this, [this](int button, bool down) {
        if (button >= 0 && button < 32) {
            steam_buttons = down ? steam_buttons | (1U << button) : steam_buttons & ~(1U << button);
            steam_seen = true;
            steam_last = steam_last || down; // the Steam Controller is the one in the hand now
        }
    });
    connect(&home->Steam(), &SteamPad::AxisChanged, this, [this](int axis, int value) {
        if (axis >= 0 && axis < static_cast<int>(std::size(steam_axes))) {
            steam_axes[axis] = value;
            steam_seen = true;
            steam_last = steam_last || std::abs(value) > 16000;
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
    if (home != nullptr) {
        home->SetDictionaries(nullptr);
        home->SetScripts(nullptr);
    }
    if (script_shelf) {
        script_shelf->scripts_changed = nullptr;
    }
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
    // Why the program ends, for a log that otherwise stops as a crash would (2026-11-20).
    qInfo("quitting: the window %s%s", event->spontaneous() ? "was closed by Windows or its close button" : "was closed by the program",
          current != nullptr && current->session && current->session->Running() ? ", a game running" : "");
    EndLines();
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
            ReaderUi::Notify(result != 0    ? QStringLiteral("Couldn't change disc")
                             : index < 0    ? QStringLiteral("Disc tray open")
                                            : QStringLiteral("Disc %1 inserted").arg(index + 1));
        });
    });
    // A core's message for the player (RetroAchievements' unlocks), where the game is shown.
    entry->session->SetNotifyCallback([this](const std::string& title, const std::string& text) {
        ReaderUi::PostToGui([this, line = QString::fromStdString(title.empty() ? text : text.empty() ? title : title + " · " + text)] {
            // A game that has ended (or never started) is on its way back to the page.
            const bool game_shown = pages->currentWidget() == screen && current != nullptr && !current->session->Ended();
            game_shown ? ReaderUi::Notify(line) : home->Notify(line);
        });
    });
    entry->session->SetStateCallback([](uint32_t what, int32_t slot, int32_t result, const std::string& message) {
        ReaderUi::PostToGui([what, slot, result, why = QString::fromStdString(message)] {
            if (result == 0) {
                ReaderUi::Notify(what == 0 ? QStringLiteral("Saved to slot %1").arg(slot)
                                           : QStringLiteral("Loaded slot %1").arg(slot));
            } else {
                ReaderUi::Notify((what == 0 ? QStringLiteral("Couldn't save to slot %1") : QStringLiteral("Couldn't load slot %1"))
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

#ifndef HOME_PUBLIC
void MainWindow::CheckUpdate(bool say) {
    Home::Updater::Check(this, Home::Services::Of(QStringLiteral("files")), [this, say](const Home::Updater::Offer& offer, const QString& error) {
        update_offer = offer;
        update_said = !error.isEmpty() ? QStringLiteral("UNKNOWN")
                      : offer.IsNone() ? QStringLiteral("UP TO DATE")
                                       : QStringLiteral("UPDATE AVAILABLE");
        if (!offer.IsNone()) {
            // Said once on the page, whoever asked: what changed, where the index says it.
            home->Say(offer.changes.isEmpty() ? QStringLiteral("Update available")
                                              : QStringLiteral("Update available: %1").arg(offer.changes));
        } else if (say) {
            home->Say(error.isEmpty() ? QStringLiteral("Furoshiki is up to date") : QStringLiteral("Couldn't check for updates: %1").arg(error));
        }
        home->Refresh();
    });
}
#endif

void MainWindow::AskHomeServer(bool say) {
#ifndef READER_NO_SAVE_SYNC
    QStringList addresses;
    QString code;
    PairedService(&addresses, &code);
    if (addresses.isEmpty() || code.isEmpty()) {
        if (say) {
            home->Say(QStringLiteral("Not paired. Pair a device first"));
        }
        return;
    }
    // Which device read each game last, for the stage.
    const QString own = ReaderUi::ReaderSession::Instance().Settings().StatsInstall();
    Home::Services::AskSync(this, addresses, code, QStringLiteral("/v1/stats"), [this, own](const Home::Services::Answer& answer) {
        if (!answer.Ok()) {
            return;
        }
        QHash<QString, QPair<QString, QString>> read;
        const auto found = Home::Services::ReadElsewhere(answer.body, own);
        for (auto it = found.constBegin(); it != found.constEnd(); ++it) {
            read.insert(it.key(), {it->device, it->day});
        }
        home->SetElsewhere(read);
    });
    Home::Services::AskPaired(this, addresses, code, [this, say](int count) {
        if (say) {
            home->Say(count < 0 ? QStringLiteral("Couldn't reach the save sync service")
                                : count == 0 ? QStringLiteral("No services found")
                                             : QStringLiteral("%1 services known").arg(count));
        }
        if (count > 0) {
            home->Refresh();
        }
        // With the Files page known: whether it has another build of this program.
        if (!say && !Home::Services::Of(QStringLiteral("files")).isEmpty()) {
            CheckUpdate(false);
        }
    });
#else
    Q_UNUSED(say);
#endif
}

void MainWindow::ShowSettingsPages() {
    if (current != nullptr && pages->currentWidget() == screen) {
        // Over the game, which stands still meanwhile; leaving the pages goes back to it.
        ReaderUi::ProgramPaused(true);
        ReaderUi::SetScreenShown(false);
        pages->setCurrentWidget(home);
        home->SetPlaying(false);
        home->setFocus();
        home->OpenPages([this] {
            if (current == nullptr) {
                return;
            }
            pages->setCurrentWidget(screen);
            home->SetPlaying(true);
            ReaderUi::SetScreenShown(true);
            screen->setFocus();
            ReaderUi::ProgramPaused(false);
        });
    } else {
        home->OpenPages();
    }
}

void MainWindow::OpenSettings(const QString& tab) {
    if (tab.isEmpty()) {
        // The settings are the shelf's pages; a tab named is one of the few editors they open.
        ShowSettingsPages();
        return;
    }
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
        hooks.stats_changed = [this] { StatsChanged(); };
        settings_window = new SettingsWindow(this, play_settings, consoles, popup_folder, home, std::move(hooks));
        settings_window->setAttribute(Qt::WA_DeleteOnClose);
    } else {
        settings_window->Refresh();
    }
    static const QString trying = QStringLiteral("Popup theme ");
    if (tab.startsWith(trying)) {
        settings_window->show();
        settings_window->TryTheme(tab.mid(trying.size()).toInt()); // a page script's picture of the list
    } else if (!tab.isEmpty()) {
        settings_window->ShowTab(tab);
    }
    settings_window->show();
    settings_window->raise();
    settings_window->activateWindow();
}

void MainWindow::AddPopupSettings() {
    // Popup settings live on Settings › Popup globally, not per-console under Consoles.
    for (PlayConsole& console : consoles) {
        console.settings.erase(std::remove_if(console.settings.begin(), console.settings.end(),
                                              [](const PlaySetting& setting) {
                                                  return setting.key.startsWith(QStringLiteral("popup."));
                                              }),
                               console.settings.end());
    }
}

QString MainWindow::CrtBase() const {
    return ReaderUi::ReaderSession::Instance().Settings().FileValue(CrtLayers::ReaderKey).toString();
}

void MainWindow::ApplyPlaySettings(const QString& game) {
    if (current == nullptr || !current->session) {
        return;
    }
    // The CRT picture of this game's layers, to the core and the frame (they follow its revision).
    ReaderCrtPage::Load(ReaderUi::ReaderSession::Instance().Settings());
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
    error = app.isEmpty() ? QStringLiteral("Couldn't open this file: no installed emulator plays .%1 files").arg(suffix)
                          : QStringLiteral("Couldn't start the game: no emulator for it is installed");
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

bool MainWindow::Play(const QString& chosen, const QString& app, QString& error) {
    // A game on several discs starts on the disc it was left on (kept by the menu's "Change disc").
    QString path = chosen;
    QStringList discs = Discs::Of(chosen);
    if (discs.size() > 1) {
        const QString kept = QSettings().value(QStringLiteral("discs/") + Discs::GroupKey(chosen)).toString();
        if (discs.contains(kept) && QFileInfo::exists(kept)) {
            path = kept;
        }
        discs.removeAll(path);
        discs.prepend(path);
    }
    // A file that is not there (a link to a folder since moved) is said here, before a game
    // that plays is stopped, an add-on started or a core handed a file it can only log.
    if (const std::string missing = CoreSession::MissingGame(path.toStdString()); !missing.empty()) {
        error = QString::fromStdString(missing);
        return false;
    }
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
    // One thing is read at a time: a title read from lines ends as a game starts.
    EndLines();
    StopCoreOcr();
    InstallReader();
    current = entry;
    button_actions_known = false; // the console's own buttons, if it has any
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
    StartAddons(QString::fromUtf8(info.key), playing_id);
    ApplyPlaySettings(playing_id);
    current->session->SetGameListener([this](const std::string& game_id) {
        ReaderUi::PostToGui([this, id = QString::fromStdString(game_id)] {
            if (current == nullptr) {
                return;
            }
            const QString key = QString::fromUtf8(current->core.info->key);
            if (id != playing_id) {
                playing_id = id;
                if (StartAddons(key, id)) {
                    ReaderUi::ReaderBridge::ReloadProfile(); // an add-on's script, come late
                }
                ApplyPlaySettings(id);
            }
            if (!ocr_pending.isEmpty() && ocr_pending == playing) {
                // The wizard's game: it says which it is now, and goes on the shelf read by OCR.
                ocr_pending.clear();
                if (HasScript(id)) {
                    ReaderUi::Notify(QStringLiteral("This game already has a game script, so it needs no OCR"));
                } else if (Lines::Sources sources = Lines::Load(SharedFolder()); Lines::GameOf(sources, id) == nullptr) {
                    Lines::SetGame(sources, PlayingCoreGame());
                    Lines::Save(SharedFolder(), sources);
                    ShelveOcrGames();
                }
            }
            RememberGame(key, id, playing);
            StartCoreOcr();
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
    // Its other discs, for the menu's "Change disc".
    playing_discs = discs;
    for (qsizetype i = 1; i < discs.size(); ++i) {
        current->session->AddDisc(discs[i].toStdString());
    }
    QTimer::singleShot(10000, this, [this] { SendStats(); }); // what earlier runs counted
    return true;
}

bool MainWindow::StartAddons(const QString& console_key, const QString& game) {
    bool scripts = false;
    for (const Addon& addon : std::as_const(addons)) {
        if (!addon.For(console_key, game)) {
            continue;
        }
        const QString state = ProgramFolder() + QStringLiteral("/addons.json");
        const QStringList copied =
            InstallScripts(addon, ReaderUi::ReaderBridge::UserProfileDirectory(), state);
        if (!copied.isEmpty()) {
            qInfo("add-on %s: %s into the profiles", qPrintable(addon.name), qPrintable(copied.join(u' ')));
            scripts = true;
        }
        if (ApplySettings(addon, play_settings, console_key, game, state)) {
            play_settings.Save();
        }
        if (addon.companion.isEmpty()) {
            continue;
        }
        const bool was = companions->Running(addon.name);
        QString problem;
        if (!companions->Start(addon, &problem)) {
            qWarning("add-on %s: its companion did not start: %s", qPrintable(addon.name), qPrintable(problem));
            ReaderUi::Notify(QStringLiteral("Couldn't start %1 companion").arg(addon.name));
        } else if (!was) {
            qInfo("add-on %s: its companion started", qPrintable(addon.name));
            ReaderUi::Notify(addon.url.isEmpty()
                                 ? QStringLiteral("%1 companion started").arg(addon.name)
                                 : QStringLiteral("%1 companion: %2").arg(addon.name, addon.url));
        }
    }
    return scripts;
}

const Addon* MainWindow::PlayingCompanion() const {
    if (current == nullptr) {
        return nullptr;
    }
    const QString key = QString::fromUtf8(current->core.info->key);
    for (const Addon& addon : addons) {
        if (addon.For(key, playing_id) && !addon.url.isEmpty()) {
            return &addon;
        }
    }
    return nullptr;
}

void MainWindow::InstallReader() {
    if (reader_installed) {
        return;
    }
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
            window->StopCoreOcr();
            if (window->current != nullptr) {
                window->WriteOwnFile(QString::fromUtf8(window->current->core.info->key), true);
            }
            window->SendStats();
        },
        this);
}

// ---- Titles read from lines (docs/furoshiki-3.md, "Sources") ----

bool MainWindow::ReadLines(const QString& path, QString& error) {
    const Lines::Sources sources = Lines::Load(SharedFolder());
    const Lines::Title* title = nullptr;
    for (const Lines::Title& known : sources.titles) {
        title = Lines::PathOf(known) == path ? &known : title;
    }
    if (title == nullptr) {
        error = QStringLiteral("Couldn't find this game in the library. Add it again");
        return false;
    }
    const bool by_ocr = title->by == QStringLiteral("ocr");
    if (title->by == QStringLiteral("hook")) {
        return ReadHook(*title, error); // a hook Furoshiki starts itself (hook_source.h)
    }
    if ((!by_ocr && lines != nullptr && lines->Running() && lines->GameId() == title->id && lines->By() == title->by) ||
        (by_ocr && ocr != nullptr && ocr->Running() && ocr->GameId() == title->id)) {
        // It is being read: its strip comes to the front.
        strip->show();
        strip->raise();
        return true;
    }
    // One thing is read at a time.
    if (current != nullptr && current->session && current->session->Running()) {
        EndGame();
    }
    EndLines();
    InstallReader();
    auto& session = ReaderUi::ReaderSession::Instance();
#ifndef READER_NO_TEXT_SERVER
    // Furoshiki's own text server sends the lines read on: reading from it would read each
    // line back for ever.
    if (title->by == QStringLiteral("websocket") && session.Settings().TextHookEnabled()) {
        const QUrl address(sources.address);
        const QString host = address.host().toLower();
        if (address.port() == session.Settings().TextHookPort() &&
            (host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1"))) {
            error = QStringLiteral("%1 is Furoshiki's text server. Use another port").arg(sources.address);
            return false;
        }
    }
#endif
    MakeStrip();
    if (!by_ocr && lines == nullptr) {
        lines = new LineSource(this);
        ReaderUi::UseSource(lines);
        connect(lines, &LineSource::StatusChanged, strip, [this] {
            if (lines->Running()) {
                strip->SetHead(lines->Title(), lines->Describe().label.toUpper(), lines->Said());
            }
        });
    }
    if (by_ocr && ocr == nullptr) {
        ocr = new OcrSource(this);
        ReaderUi::UseSource(ocr);
        // After the reader's own (connected by UseSource): its cards, from the window.
        connect(ocr, &ReaderUi::ReadingSource::Began, this, [this] { CardsFromWindow(); });
        connect(ocr, &ReaderUi::ReadingSource::Ended, this, [] { Loopback::Instance().Stop(); });
        overlay = new OcrOverlay(this);
        connect(ocr, &OcrSource::StatusChanged, strip, [this] {
            if (ocr->Running()) {
                strip->SetHead(ocr->Title(), QStringLiteral("OCR · WINDOW"), ocr->Said());
            }
        });
        connect(ocr, &OcrSource::ScreenChanged, this, [this] { PlaceOverlay(); });
        // The boxes read only on request (a menu, a status screen): R on the strip, and
        // Ctrl+Shift+R from anywhere, the game's own window too, where the system gives a key.
        const auto read_asked = [this] {
            if (ocr == nullptr || !ocr->Running()) {
                return;
            }
            ocr->ReadAsked([this](bool ok, const QString& what) {
                if (!ok && ocr != nullptr && ocr->Running()) {
                    strip->SetHead(ocr->Title(), QStringLiteral("OCR · WINDOW"), what.isEmpty() ? QStringLiteral("No text found in the text areas") : what);
                }
            });
        };
        connect(new QShortcut(QKeySequence(Qt::Key_R), strip), &QShortcut::activated, this, read_asked);
        auto* read_key = new QAction(this); // a back button's (MakeGuideKeys)
        read_key->setObjectName(QStringLiteral("furoshiki_ocr_read"));
        connect(read_key, &QAction::triggered, this, read_asked);
        auto* anywhere = new GlobalKey(ocr);
        connect(anywhere, &GlobalKey::Pressed, this, read_asked);
        connect(ocr, &ReaderUi::ReadingSource::Began, anywhere, [anywhere] { anywhere->Set('R'); });
        connect(ocr, &ReaderUi::ReadingSource::Ended, anywhere, [anywhere] { anywhere->Clear(); });
    }
    // The popup is the reader's own, in a look no console gave it.
    ReaderUi::Style::SetConsoleLook("lines");
    ReaderUi::LookupPopup::SetConsoleKey(QStringLiteral("lines"));
    const HomeWindow::StripLook look = home->LookOfStrip();
    if (by_ocr) {
        overlay->SetAccent(look.accent);
        overlay->SetLine({}, {});
        if (!ocr->Start(*title, OcrInUse(sources.ocr), error)) {
            return false;
        }
    } else if (!lines->Start(title->by, title->id, title->title, sources.address, error)) {
        return false;
    }
    strip->SetLook({look.ground, look.ink, look.muted, look.accent, look.rule, look.text, look.mono});
    strip->SetOpacity(session.Settings().PopupOpacity());
    strip->SetLine({});
    strip->Place();
    strip->show();
    home->Notify(QStringLiteral("%1 · %2").arg(title->title, by_ocr ? ocr->Said() : lines->Said()));
    return true;
}

// ---- Titles read by a hook Furoshiki starts itself (docs/hooks.md) ----

bool MainWindow::ReadHook(const Lines::Title& title, QString& error) {
    if (hook != nullptr && hook->Running() && hook->GameId() == title.id) {
        strip->show();
        strip->raise();
        return true;
    }
    if (current != nullptr && current->session && current->session->Running()) {
        EndGame();
    }
    EndLines();
    InstallReader();
    MakeStrip();
    if (hook == nullptr) {
        hook = new HookSource(this);
        ReaderUi::UseSource(hook);
        connect(hook, &ReaderUi::ReadingSource::Began, this, [this] { CardsFromWindow(); });
        connect(hook, &ReaderUi::ReadingSource::Ended, this, [] { Loopback::Instance().Stop(); });
        connect(hook, &HookSource::StatusChanged, strip, [this] {
            if (hook->Running()) {
                strip->SetHead(hook->Title(), hook->Label(), hook->Said());
            }
        });
    }
    ReaderUi::Style::SetConsoleLook("lines");
    ReaderUi::LookupPopup::SetConsoleKey(QStringLiteral("lines"));
    if (!hook->Start(title, SharedFolder(), error)) {
        return false;
    }
    // With an OCR helper, the hooked text is also laid over the game's own: invisibly, to be
    // pointed at there as in a game a core plays.
    if (const Lines::Helper helper = OcrInUse(Lines::Load(SharedFolder()).ocr); !helper.address.isEmpty()) {
        if (overlay == nullptr) {
            overlay = new OcrOverlay(this);
        }
        overlay->SetAccent(home->LookOfStrip().accent);
        overlay->SetLine({}, {});
        overlay->SetPlace({}, false);
        if (placer == nullptr) {
            placer = new OcrClient(this);
        }
        placer->Set(helper);
        placer->Choose(); // this device's pictures are the ones it reads
        place_area = QRectF(0, 0, 1, 1);
    } else if (placer != nullptr) {
        placer->Set({});
    }
    const HomeWindow::StripLook look = home->LookOfStrip();
    strip->SetLook({look.ground, look.ink, look.muted, look.accent, look.rule, look.text, look.mono});
    strip->SetOpacity(ReaderUi::ReaderSession::Instance().Settings().PopupOpacity());
    strip->SetLine({});
    strip->Place();
    strip->show();
    home->Notify(QStringLiteral("%1 · %2").arg(title.title, hook->Said()));
    return true;
}

void MainWindow::WatchHooks() {
    // A title read by a hook is read by itself when its program is seen running (once each
    // time the program starts: closing the strip does not bring it back).
    const bool busy = (current != nullptr && current->session && current->session->Running()) || (lines != nullptr && lines->Running()) ||
                      (ocr != nullptr && ocr->Running()) || (hook != nullptr && hook->Running()) || home->Adding();
    const Lines::Sources sources = Lines::Load(SharedFolder());
    QSet<QString> running;
    for (const Lines::Title& title : sources.titles) {
        const QString process = title.process.toLower();
        if (title.by != QStringLiteral("hook") || running.contains(process) || !Hooks::ProgramNamed(title.process)) {
            continue;
        }
        running.insert(process);
        if (busy || hooks_seen.contains(process)) {
            continue;
        }
        for (const Lines::Title& one : Lines::HookedIn(sources, title.process)) {
            QString error;
            if (!ReadHook(one, error)) {
                home->Notify(error);
            }
            hooks_seen = running;
            return;
        }
    }
    if (!busy) {
        hooks_seen = running;
    }
}

QString MainWindow::GameWindow() const {
    if (ocr != nullptr && ocr->Running()) {
        return ocr->Of().window;
    }
    if (hook != nullptr && hook->Running()) {
        if (const auto program = Hooks::ProgramNamed(hook->Of().process)) {
            return program->window;
        }
    }
    return {};
}

void MainWindow::CardsFromWindow() {
    auto& session = ReaderUi::ReaderSession::Instance();
    ReaderUi::MediaCapture* media = session.Media();
    if (media == nullptr) {
        return;
    }
    media->frame_source = [this](int max_width, std::function<void(const QImage&)> callback) {
        // The game's window as it is now (the reader asks a moment after a line came).
        const QString window = GameWindow();
        QImage picture = window.isEmpty() ? QImage() : Windows::Grab(Windows::Find(window));
        const int width = std::max(max_width, 320);
        if (!picture.isNull() && picture.width() > width) {
            picture = picture.scaledToWidth(width, Qt::SmoothTransformation);
        }
        callback(picture);
    };
    if (session.Settings().CaptureAudio()) {
        Loopback::Instance().Start();
    }
}

// ---- A core's game without a script, read by OCR (docs/furoshiki-3.md, "A core's game read by OCR") ----

bool MainWindow::HasScript(const QString& id) {
    return ReaderUi::ReaderBridge::HasBundledProfile(id) || ReaderUi::ReaderBridge::HasUserProfile(id);
}

bool MainWindow::AddCoreOcrGame(const QString& console, const QString& path) {
    QString error;
    ocr_pending = path;
    // The game's listener (Play) adds it once the core has said which game the file is.
    if (!Play(path, console.isEmpty() ? QString() : QStringLiteral("reader-home-") + console, error)) {
        ocr_pending.clear();
        home->Notify(error);
        return false;
    }
    return true;
}

Lines::CoreGame MainWindow::PlayingCoreGame() const {
    const Lines::Sources sources = Lines::Load(SharedFolder());
    if (const Lines::CoreGame* known = Lines::GameOf(sources, playing_id)) {
        return *known;
    }
    Lines::CoreGame game;
    game.id = playing_id;
    game.path = playing;
    if (current != nullptr) {
        game.console = QString::fromUtf8(current->core.info->key);
        game.title = QString::fromStdString(current->session->Title());
    }
    if (game.title.isEmpty()) {
        game.title = QFileInfo(playing).completeBaseName();
    }
    return game;
}

void MainWindow::ShelveOcrGames() {
    const Lines::Sources sources = Lines::Load(SharedFolder());
    QSet<QString> ids, changed;
    for (const Lines::CoreGame& game : sources.games) {
        ids.insert(game.id);
        if (game.console.isEmpty() || HasScript(game.id)) {
            continue;
        }
        QList<OwnGame>& games = own_games[game.console];
        if (std::none_of(games.cbegin(), games.cend(), [&](const OwnGame& other) { return other.id == game.id; })) {
            games.append({game.id, game.title, game.path});
            changed.insert(game.console);
        }
    }
    for (const QString& key : std::as_const(changed)) {
        WriteOwnFile(key, false);
        ShowConsoleGames(key);
    }
    home->SetOcrGames(ids);
    if (!changed.isEmpty()) {
        home->Refresh();
    }
}

void MainWindow::StopCoreOcr() {
    if (core_ocr != nullptr && core_ocr->Running()) {
        core_ocr->Stop();
    }
    ocr_said.clear();
}

void MainWindow::StartCoreOcr() {
    StopCoreOcr();
    if (current == nullptr || playing_id.isEmpty() || HasScript(playing_id)) {
        return;
    }
    const Lines::Sources sources = Lines::Load(SharedFolder());
    const Lines::CoreGame* game = Lines::GameOf(sources, playing_id);
    if (game == nullptr || !game->on) {
        return;
    }
    if (!game->area.isValid()) {
        ReaderUi::Notify(QStringLiteral("OCR on. When text appears, press Esc and choose \"Set text area…\""));
        return;
    }
    if (core_ocr == nullptr) {
        core_ocr = new CoreOcr(this);
        connect(core_ocr, &CoreOcr::StatusChanged, this, [this] {
            // What the helper says, over the game, each state once ("reading · http://…").
            const QString status = core_ocr->Status();
            const QString state = status.section(QStringLiteral(" · "), 0, 0);
            if (core_ocr->Running() && !state.isEmpty() && state != ocr_said && state != QStringLiteral("not asked yet") && state != QStringLiteral("answers")) {
                ocr_said = state;
                ReaderUi::Notify(state == QStringLiteral("reading") ? QStringLiteral("OCR is reading this game") : core_ocr->Said());
            }
        });
    }
    QString error;
    if (!core_ocr->Start(*game, OcrInUse(sources.ocr), error)) {
        ReaderUi::Notify(error);
    }
}

void MainWindow::SetCoreOcrOn(bool on) {
    if (current == nullptr || playing_id.isEmpty()) {
        return;
    }
    Lines::Sources sources = Lines::Load(SharedFolder());
    Lines::CoreGame game = PlayingCoreGame();
    const bool known = Lines::GameOf(sources, playing_id) != nullptr;
    game.on = on;
    if (!known && !on) {
        return;
    }
    Lines::SetGame(sources, game);
    Lines::Save(SharedFolder(), sources);
    ShelveOcrGames();
    if (!on) {
        StopCoreOcr();
        ReaderUi::Notify(QStringLiteral("OCR off"));
    } else if (game.area.isValid()) {
        StartCoreOcr();
    } else {
        // Its text's area first, over the picture as it stands (after the menu has closed).
        QTimer::singleShot(0, this, [this] { AskCoreArea(); });
    }
}

void MainWindow::AskCoreArea() {
    if (current == nullptr || playing_id.isEmpty() || pages->currentWidget() != screen) {
        return;
    }
    const QString id = playing_id;
    ReaderUi::GrabPicture(1920, [this, id](const QImage& picture) {
        if (current == nullptr || playing_id != id || pages->currentWidget() != screen) {
            return;
        }
        if (picture.isNull()) {
            ReaderUi::Notify(QStringLiteral("Couldn't take a picture of the game. Try again once it shows something"));
            return;
        }
        StopCoreOcr();
        const Lines::CoreGame game = PlayingCoreGame();
        QList<Lines::Box> boxes;
        if (game.area.isValid()) {
            boxes.append({game.area, false});
            boxes.append(game.more);
        }
        // Furoshiki's page over the game, which stands still meanwhile (as its settings pages do).
        ReaderUi::ProgramPaused(true);
        ReaderUi::SetScreenShown(false);
        pages->setCurrentWidget(home);
        home->SetPlaying(false);
        home->setFocus();
        home->AskCoreArea(game.title, picture, boxes, [this, id](bool taken, const QList<Lines::Box>& drawn) {
            if (current == nullptr || playing_id != id) {
                return;
            }
            if (taken && !drawn.isEmpty()) {
                Lines::Sources sources = Lines::Load(SharedFolder());
                Lines::CoreGame game = PlayingCoreGame();
                game.area = drawn.first().rect;
                game.more = drawn.mid(1);
                game.on = true;
                Lines::SetGame(sources, game);
                Lines::Save(SharedFolder(), sources);
                ShelveOcrGames();
            }
            pages->setCurrentWidget(screen);
            home->SetPlaying(true);
            ReaderUi::SetScreenShown(true);
            screen->setFocus();
            ReaderUi::ProgramPaused(false);
            StartCoreOcr();
        });
    });
}

void MainWindow::ReadAskedBoxes() {
    if (core_ocr == nullptr || !core_ocr->Running()) {
        return;
    }
    core_ocr->ReadAsked([](bool ok, const QString& what) {
        if (!ok) {
            ReaderUi::Notify(what.isEmpty() ? QStringLiteral("No text found in the text areas") : what);
        }
    });
}

void MainWindow::MakeStrip() {
    if (strip != nullptr) {
        return;
    }
    strip = new TextStrip(this);
    strip->close_wanted = [this] { EndLines(); };
    connect(&ReaderUi::ReaderSession::Instance(), &ReaderUi::ReaderSession::LinePresented, strip, [this](const ReaderApp::PresentedLine& presented) {
        if (presented.line.IsChoice()) {
            return;
        }
        if (lines != nullptr && lines->Running()) {
            strip->SetLine(presented);
        } else if (hook != nullptr && hook->Running()) {
            strip->SetLine(presented);
            PlaceHooked(presented);
        } else if (ocr != nullptr && ocr->Running()) {
            // In the strip, and where the helper says its characters are over the game's window.
            strip->SetLine(presented);
            overlay->SetLine(presented, ocr->Boxes());
            PlaceOverlay();
        }
    });
}

void MainWindow::PlaceHooked(const ReaderApp::PresentedLine& presented) {
    if (placer == nullptr || !placer->Configured() || overlay == nullptr) {
        return;
    }
    overlay->SetPlace({}, false); // the line before is no longer where it was
    const int generation = ++place_generation;
    // A moment for the game to draw it, then its window's picture, then the helper's reading.
    QTimer::singleShot(350, this, [this, presented, generation] {
        if (generation != place_generation || hook == nullptr || !hook->Running()) {
            return;
        }
        const Windows::Entry window = Windows::Find(GameWindow());
        const QImage picture = window.title.isEmpty() ? QImage() : Windows::Grab(window);
        if (picture.isNull() || !placer->Push(picture)) {
            return;
        }
        QTimer::singleShot(250, this, [this, presented, generation] {
            if (generation != place_generation) {
                return;
            }
            auto* once = new QObject(this);
            connect(placer, &OcrClient::Placed, once, [this, presented, generation, once](bool ok, const QString& seen, const QList<QRectF>& layout) {
                once->deleteLater();
                if (generation == place_generation) {
                    HookedPlaced(presented, ok, seen, layout);
                }
            });
            placer->Test(place_area);
        });
    });
}

void MainWindow::HookedPlaced(const ReaderApp::PresentedLine& presented, bool ok, const QString& seen, const QList<QRectF>& layout) {
    if (!ok || hook == nullptr || !hook->Running()) {
        return;
    }
    int matched = 0;
    const QList<QRectF> boxes = Lines::PlaceAlong(presented.line.text, seen, layout, &matched);
    const int length = static_cast<int>(presented.line.text.toUcs4().size());
    if (length == 0 || matched * 2 < length) {
        // Not there as the hook has it: the whole window is read next time.
        if (place_area != QRectF(0, 0, 1, 1)) {
            place_area = QRectF(0, 0, 1, 1);
            PlaceHooked(presented);
        }
        return;
    }
    // Where it stood, a little round it, is read first next time: quicker than the whole window.
    QRectF found;
    for (const QRectF& box : boxes) {
        if (!box.isEmpty()) {
            found = found.isNull() ? box : found.united(box);
        }
    }
    place_area = found.adjusted(-0.08, -0.12, 0.08, 0.12).intersected(QRectF(0, 0, 1, 1));
    overlay->SetLine(presented, boxes);
    const Windows::Entry window = Windows::Find(GameWindow());
    overlay->SetPlace(Windows::Place(window), !window.title.isEmpty());
}

void MainWindow::PlaceOverlay() {
    if (overlay != nullptr && ocr != nullptr) {
        overlay->SetPlace(ocr->Place(), ocr->Running() && ocr->OnScreen());
    }
}

void MainWindow::EndLines() {
    const bool from_lines = lines != nullptr && lines->Running(), from_window = ocr != nullptr && ocr->Running();
    const bool from_hook = hook != nullptr && hook->Running();
    if (!from_lines && !from_window && !from_hook) {
        return;
    }
    if (from_hook) {
        hook->Stop();
        ++place_generation;
        if (overlay != nullptr) {
            overlay->SetPlace({}, false);
        }
    }
    Loopback::Instance().Stop();
    if (from_lines) {
        lines->Stop();
    }
    if (from_window) {
        ocr->Stop();
        PlaceOverlay();
    }
    strip->hide();
    WriteLinesFile();
    SendStats();
}

void MainWindow::WriteLinesFile() {
    const QString folder = SharedFolder();
    const QString program = QCoreApplication::applicationFilePath();
    const Lines::Sources sources = Lines::Load(folder);
    if (!reader_installed) {
        Lines::WriteApp(folder, sources, program, nullptr);
        return;
    }
    // What was read of these titles, out of the program's one record (as WriteOwnFile).
    QSet<QString> ids;
    for (const Lines::Title& title : sources.titles) {
        ids.insert(title.id);
    }
    auto& session = ReaderUi::ReaderSession::Instance();
    ReaderApp::ReaderDatabase* database = &session.Database();
    session.AfterWrites([this, folder, program, sources, ids, database] {
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
        Lines::WriteApp(folder, sources, program, &record);
        QMetaObject::invokeMethod(this, [this] { home->Refresh(); }, Qt::QueuedConnection);
    });
}

bool MainWindow::LinesCommand(const QString& word, const QString& rest) {
    if (word == QStringLiteral("lines-say")) {
        // lines-say TEXT: text as if the source had sent it.
        if (lines != nullptr) {
            lines->Arrived(Lines::MessageText(rest));
        }
    } else if (word == QStringLiteral("hook-say")) {
        // What the hook does, for a test to read in the log.
        if (hook != nullptr) {
            qInfo("hook: %s; %lld lines handed over; %lld threads; said: %s", qPrintable(hook->Status()), static_cast<long long>(hook->Handed()),
                  static_cast<long long>(hook->Engine().Threads().size()), qPrintable(hook->Engine().Said().join(QStringLiteral(" / "))));
        } else {
            qInfo("hook: none");
        }
    } else if (word == QStringLiteral("lines-stop")) {
        EndLines();
    } else if (word == QStringLiteral("clipboard")) {
        QGuiApplication::clipboard()->setText(rest);
    } else if (word == QStringLiteral("strip-lookup")) {
        // strip-lookup OFFSET: the word at that place of the strip's line, as a click on it.
        if (strip != nullptr && !strip->LookUpAt(rest.toInt())) {
            qWarning("strip-lookup: no character %d in the strip's line", rest.toInt());
        }
    } else if (word == QStringLiteral("ocr-say")) {
        // What the window read by OCR does, for a test to read in the log.
        if (ocr != nullptr) {
            qInfo("ocr: %s; pictures sent %d, taken %d; %lld boxes, %s the screen, overlay %s, popup %s", qPrintable(ocr->Status()),
                  ocr->Client().Sent(), ocr->Client().Taken(), static_cast<long long>(ocr->Boxes().size()), ocr->OnScreen() ? "on" : "off",
                  overlay->isVisible() ? "shown" : "hidden", overlay->Lookups().IsOpen() ? "open" : "closed");
        }
    } else if (word == QStringLiteral("overlay-lookup")) {
        // overlay-lookup OFFSET: the word at that character of the line over the window.
        if (overlay != nullptr && !overlay->LookUpAt(rest.toInt())) {
            qWarning("overlay-lookup: character %d has no place over the window", rest.toInt());
        }
    } else if (word == QStringLiteral("core-ocr-add")) {
        // core-ocr-add CONSOLE|- PATH: what the wizard's "one game, read by OCR" does.
        AddCoreOcrGame(rest.section(QLatin1Char(' '), 0, 0) == QStringLiteral("-") ? QString() : rest.section(QLatin1Char(' '), 0, 0), rest.section(QLatin1Char(' '), 1));
    } else if (word == QStringLiteral("strip-say")) {
        // What the strip and its lookup hold, for a test to read in the log.
        if (strip != nullptr) {
            qInfo("strip: line %lld \"%s\", popup %s", static_cast<long long>(strip->Line().backlog_id),
                  qPrintable(strip->Line().line.text), strip->Lookups().IsOpen() ? "open" : "closed");
        }
    } else {
        return false;
    }
    return true;
}

Home::ControllerMap::Store MainWindow::ControllerStore() {
    Home::ControllerMap::Store store;
    store.get = [this](const QString& key, const QString& console) -> QString {
        if (!console.isEmpty()) {
            return play_settings.Raw(QStringLiteral("controller.") + key, console);
        }
        // All consoles': the reader's own settings, as they are kept (not a game's layer).
        return ReaderUi::ReaderSession::Instance().Settings().FileValue(key).toString();
    };
    store.set = [this](const QString& key, const QString& value, const QString& console) {
        if (!console.isEmpty()) {
            play_settings.Set(QStringLiteral("controller.") + key, value, console);
            return;
        }
        auto& settings = ReaderUi::ReaderSession::Instance().Settings();
        if (value.isEmpty()) {
            settings.Remove(key);
        } else {
            bool number = false;
            const int as_number = value.toInt(&number);
            settings.SetValue(key, number ? QVariant(as_number) : QVariant(value));
        }
    };
    store.save = [this] {
        play_settings.Save();
        auto& session = ReaderUi::ReaderSession::Instance();
        session.Settings().Sync();
        session.OnSettingsChanged();
        button_actions_known = false;
    };
    return store;
}

void MainWindow::OpenControllerMap(bool shortcuts) {
    if (controller_screen != nullptr) {
        return;
    }
    const HomeWindow::ScreenLook look = home->LookOfScreen();
    QList<ControllerScreen::Console> tabs{{QString(), QStringLiteral("All consoles")}};
    for (const PlayConsole& console : consoles) {
        tabs.append({console.key, console.name});
    }
    controller_screen = new ControllerScreen(home, {look.ground, look.raised, look.ink, look.muted, look.accent, look.rule, look.font},
                                             ControllerStore(), tabs, [this] {
                                                 // The Steam Controller's buttons work the page again, its grips their actions.
                                                 home->Steam().SetGameMode(current != nullptr && pages->currentWidget() == screen);
                                                 ReaderUi::CaptureBackButtons({});
                                                 button_actions_known = false;
                                                 home->setFocus();
                                                 home->Refresh();
                                             });
    if (shortcuts) {
        controller_screen->ShowShortcuts();
    }
    // While it is open every button comes to it as it is: the Steam Controller's too, and its back.
    home->Steam().SetGameMode(true);
    ReaderUi::CaptureBackButtons([this](int back) {
        if (controller_screen == nullptr) {
            return false;
        }
        for (const Home::ControllerMap::Input& input : Home::ControllerMap::Load().inputs) {
            if (input.back == back) {
                controller_screen->Pressed(input.id, true);
                QTimer::singleShot(120, controller_screen, [this, id = input.id] {
                    if (controller_screen != nullptr) {
                        controller_screen->Pressed(id, false, false);
                    }
                });
            }
        }
        return true;
    });
}

void MainWindow::MakeGuideKeys() {
#ifndef READER_NO_SAVE_SYNC
    // The keys work over the game too, where the menu is not open.
    auto* toggle_key = new QAction(this);
    toggle_key->setObjectName(QStringLiteral("furoshiki_guide"));
    toggle_key->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    toggle_key->setShortcutContext(Qt::ApplicationShortcut);
    connect(toggle_key, &QAction::triggered, this, [this] { ToggleGuide(); });
    addAction(toggle_key);
    auto* done_key = new QAction(this);
    done_key->setObjectName(QStringLiteral("furoshiki_guide_done"));
    done_key->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    done_key->setShortcutContext(Qt::ApplicationShortcut);
    connect(done_key, &QAction::triggered, this, [this] { GuideDone(); });
    addAction(done_key);
#endif
    // What a controller's back button may do besides the reader's own (reader_rows.cpp): the
    // keys of the program, found by these names (ReaderController::OnBackButton).
    auto* menu_key = new QAction(this);
    menu_key->setObjectName(QStringLiteral("furoshiki_game_menu"));
    connect(menu_key, &QAction::triggered, this, [this] { OpenMenu(); });
    auto* full_key = new QAction(this);
    full_key->setObjectName(QStringLiteral("furoshiki_full_screen"));
    connect(full_key, &QAction::triggered, this, [this] { ToggleFullScreen(); });
    auto* read_key = new QAction(this);
    read_key->setObjectName(QStringLiteral("furoshiki_read"));
    connect(read_key, &QAction::triggered, this, [this] { ReadCommand(QStringLiteral("toggle")); });
    // The text panel (the strip of lines from a hook, OCR or the clipboard) shown or hidden.
    auto* panel_key = new QAction(this);
    panel_key->setObjectName(QStringLiteral("furoshiki_text_panel"));
    connect(panel_key, &QAction::triggered, this, [this] { ToggleTextPanel(); });
    addAction(panel_key);
    auto* settings_key = new QAction(this);
    settings_key->setObjectName(QStringLiteral("furoshiki_settings"));
    connect(settings_key, &QAction::triggered, this, [this] { OpenSettings(); });
    // Ctrl+Q quits from anywhere: the library, a page, over a game (as Quit Furoshiki in its menu).
    auto* quit_key = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this);
    quit_key->setContext(Qt::ApplicationShortcut);
    connect(quit_key, &QShortcut::activated, this, [this] {
        qInfo("quitting: Ctrl+Q");
        close();
    });
}

void MainWindow::ToggleTextPanel() {
    if (strip == nullptr) {
        return; // no text panel: nothing is read from lines, a window or a hook now
    }
    strip->setVisible(!strip->isVisible());
}

void MainWindow::ToggleGuide() {
#ifndef READER_NO_SAVE_SYNC
    if (current == nullptr || pages->currentWidget() != screen) {
        return;
    }
    if (guide_overlay == nullptr) {
        guide_overlay = new GuideOverlay;
        connect(this, &QObject::destroyed, guide_overlay, &QObject::deleteLater);
    }
    guide_overlay->Toggle(screen, Home::Services::Of(QStringLiteral("furochibi")), playing_id);
#endif
}

void MainWindow::GuideDone() {
#ifndef READER_NO_SAVE_SYNC
    if (guide_overlay != nullptr) {
        guide_overlay->Done();
    }
#endif
}

void MainWindow::EndGame() {
#ifndef READER_NO_SAVE_SYNC
    if (guide_overlay != nullptr) {
        guide_overlay->Close();
    }
#endif
    StopCoreOcr();
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
    if (!known && HasScript(id)) {
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
                    // A track of a disc whose sheet is beside it is the sheet's, where the
                    // core takes sheets (Station's). One that does not (PCSX2's: a PS2 CD as
                    // .bin and .cue) is given the .bin, else nobody would list the disc.
                    if (suffix == QStringLiteral("bin") && task.types.contains(QStringLiteral("cue")) &&
                        (QFileInfo::exists(info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".cue")) ||
                         QFileInfo::exists(info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".CUE")))) {
                        continue;
                    }
                    add(path);
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, found = std::move(found)] {
            // The games no script reads: what the wizard offers to read by OCR, and those
            // already read that way, which stand on the shelf as the others do.
            const Lines::Sources sources = Lines::Load(SharedFolder());
            QList<HomeWindow::UnreadGame> unread;
            for (const auto& [key, games] : found) {
                QList<OwnGame> readable;
                QString console_name = key.toUpper();
                for (const PlayConsole& console : std::as_const(consoles)) {
                    console_name = console.key == key ? console.name : console_name;
                }
                unscripted[key].clear();
                for (OwnGame game : games) {
                    // Offered to be read by hook (wizard_core_hooks.inc), once a game.
                    if (!HasScript(game.id) &&
                        std::none_of(unscripted[key].cbegin(), unscripted[key].cend(), [&](const OwnGame& other) { return other.id == game.id; })) {
                        unscripted[key].append(game);
                    }
                    if (HasScript(game.id)) {
                        // The game's own name, as its profile writes it, where the disc has none.
                        if (const QString title = ProfileTitle(game.id); !title.isEmpty()) {
                            game.title = title;
                        }
                        readable.append(game);
                    } else if (const Lines::CoreGame* read = Lines::GameOf(sources, game.id)) {
                        game.title = read->title.isEmpty() ? game.title : read->title;
                        readable.append(game);
                    } else if (std::none_of(unread.cbegin(), unread.cend(), [&](const HomeWindow::UnreadGame& other) { return other.id == game.id; })) {
                        unread.append({key, console_name, game.id, game.title, game.path});
                    }
                }
                // One row a game: a game found under two spellings of its folder, or found and
                // played, is listed once; a game on several discs too (Discs), as its first disc,
                // even where its discs have IDs of their own (Final Fantasy IX).
                QList<OwnGame> once;
                QStringList groups; // beside `once`: each row's disc group, or empty
                for (OwnGame game : std::as_const(readable)) {
                    const int disc = Discs::Number(QFileInfo(game.path).fileName());
                    const QString group = disc > 0 ? Discs::GroupKey(game.path) : QString();
                    if (disc > 0) {
                        game.title = Discs::WithoutDisc(game.title);
                        if (const qsizetype at = groups.indexOf(group); at >= 0) {
                            // The game's row stands: it is its lowest disc's.
                            if (disc < Discs::Number(QFileInfo(once[at].path).fileName())) {
                                once[at] = game;
                            }
                            continue;
                        }
                    }
                    if (std::any_of(once.cbegin(), once.cend(), [&](const OwnGame& other) { return other.id == game.id; })) {
                        continue;
                    }
                    once.append(game);
                    groups.append(group);
                }
                own_games[key] = once;
                WriteOwnFile(key, false);
                ShowConsoleGames(key);
            }
            home->SetUnreadGames(unread);
            ShelveOcrGames(); // one read by OCR whose file is in no folder looked through
            home->Refresh();
            if (!announce_console.isEmpty()) {
                // A folder was added or a game script installed: what the library has of that console now.
                const QString key = std::exchange(announce_console, QString());
                const QString name = ConsoleFull(key, key.toUpper());
                const qsizetype have = own_games[key].size();
                const qsizetype without = std::count_if(unread.cbegin(), unread.cend(), [&](const HomeWindow::UnreadGame& game) { return game.console == key; });
                home->Say(have > 0      ? (have == 1 ? QStringLiteral("1 %1 game in the library") : QStringLiteral("%2 %1 games in the library").arg(have)).arg(name)
                          : without > 0 ? QStringLiteral("No %1 game there has a game script yet. Install game scripts in Settings, Consoles, or add one game to read by OCR").arg(name)
                                        : QStringLiteral("No %1 games found in the game folders").arg(name));
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::OpenGameScripts(const QString& key) {
    if (current != nullptr && key != QString::fromUtf8(current->core.info->key)) {
        home->Say(QStringLiteral("Quit the game first: it is another console's"));
        return;
    }
    for (auto& [file, entry] : loaded) {
        if (key != QString::fromUtf8(entry->core.info->key)) {
            continue;
        }
        if (current == nullptr) {
            ApplyConsole(*entry->core.info); // the list asked for is this console's
        }
        ReaderUi::RunGameScriptsDialog(this);
        announce_console = key;
        FindGames();
        return;
    }
}

void MainWindow::StatsChanged() {
    for (const auto& [key, games] : own_games) {
        WriteOwnFile(key, true);
    }
    SendStats();
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
        // The words looked up here, which Furochibi adds up with every device's.
        record.insert(QStringLiteral("lookups"), database->LookupsRecord());
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
                                   : QStringLiteral("BIOS MISSING");
    } else if (key == QStringLiteral("switch")) {
        setup.ready = folder.exists(QStringLiteral("prod.keys"));
        const bool firmware = QDir(setup.system_folder + QStringLiteral("/nand/system/Contents/registered")).isEmpty() == false;
        setup.status = setup.ready ? QStringLiteral("KEYS FOUND%1").arg(firmware ? QStringLiteral(", FIRMWARE INSTALLED") : QStringLiteral(", NO FIRMWARE"))
                                   : QStringLiteral("PROD.KEYS MISSING");
    } else if (key == QStringLiteral("dc")) {
        const QStringList bios = files({QStringLiteral("dc_boot.bin"), QStringLiteral("dc/dc_boot.bin")}, 0);
        setup.status = bios.isEmpty() && !folder.exists(QStringLiteral("dc/dc_boot.bin"))
                           ? QStringLiteral("NO BIOS (OPTIONAL)")
                           : QStringLiteral("BIOS FOUND");
    } else if (key == QStringLiteral("3ds")) {
        setup.status = folder.exists(QStringLiteral("sysdata/aes_keys.txt")) || folder.exists(QStringLiteral("aes_keys.txt"))
                           ? QStringLiteral("AES KEYS FOUND")
                           : QStringLiteral("NOT NEEDED FOR DECRYPTED GAMES");
    } else {
        setup.status = QStringLiteral("NOT NEEDED");
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

void MainWindow::ShowPopupPreview() {
    if (popup_preview == nullptr || popup_preview_place.isEmpty()) {
        return;
    }
    auto& settings = ReaderUi::ReaderSession::Instance().Settings();
    const auto value = [&](const QString& key) { return settings.Value(key).toString(); };
    const QString raw_theme = popup_preview_override_theme.isEmpty() ? value(QStringLiteral("reader_popup_theme"))
                                                                     : popup_preview_override_theme;
    QString theme = ReaderApp::ReaderSettings::ResolvePopupTheme(raw_theme);
    if (theme == QStringLiteral("classic")) {
        theme = QStringLiteral("console");
    }
    const QString mark = value(QStringLiteral("reader_popup_mark_unknown"));
    const QJsonObject model{{QStringLiteral("entries"), ReaderApp::PopupModel::Sample()},
                            {QStringLiteral("corner"), settings.CardCorner()},
                            {QStringLiteral("duplicates"), false},
                            {QStringLiteral("markUnknown"), mark != QStringLiteral("false") && mark != QStringLiteral("0")},
                            {QStringLiteral("scale"), settings.PopupScale()},
                            {QStringLiteral("look"), PopupStyle::LookOf(value)},
                            {QStringLiteral("labels"), QJsonObject{{QStringLiteral("forms"), QStringLiteral("forms")},
                                                                   {QStringLiteral("more"), QStringLiteral("more")}}}};
    const QString css = popup_preview_override_theme.isEmpty() ? settings.PopupCss() : QString();
    const QString now = theme + QLatin1Char('\n') + css + QLatin1Char('\n') + QString::number(settings.PopupFitEntry()) +
                        QString::number(settings.PopupHeightPercent()) + QString::fromUtf8(QJsonDocument(model).toJson(QJsonDocument::Compact));
    if (now == popup_preview_shown) {
        return; // nothing changed since it was last drawn
    }
    popup_preview_shown = now;
    const auto text = [](const QString& said) {
        return QString::fromUtf8(QJsonDocument(QJsonArray{said}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    };
    popup_preview->Call(QStringLiteral("Reader.setStyle(\"console\", %1)").arg(text(ReaderUi::LookupPopup::ConsoleStyle())));
    popup_preview->Call(QStringLiteral("Reader.setStyle(\"theme\", %1)").arg(text(ReaderUi::LookupPopup::ThemeStyle(theme))));
    popup_preview->Call(QStringLiteral("Reader.setStyle(\"user\", %1)").arg(text(css)));
    popup_preview->Call(QStringLiteral("Reader.show(%1)").arg(QString::fromUtf8(QJsonDocument(model).toJson(QJsonDocument::Compact))));
}

void MainWindow::PollPads() {
    // The game ended (by itself, or it never started): Furoshiki's page again.
    if (current != nullptr && current->session && current->session->Ended() && pages->currentWidget() == screen) {
        ShowGame(false);
    }

    SDL_GameControllerUpdate();
    OpenPads();
    // The controller in use is the one last pressed (a button, a trigger or a stick pushed past
    // halfway): any of SDL's, or the Steam Controller through the bridge.
    for (SDL_GameController* each : sdl_pads) {
        uint64_t now_in = 0;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
            if (SDL_GameControllerGetButton(each, static_cast<SDL_GameControllerButton>(b)) != 0) {
                now_in |= uint64_t{1} << b;
            }
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            const int value = SDL_GameControllerGetAxis(each, static_cast<SDL_GameControllerAxis>(a));
            if (value > 16000) {
                now_in |= uint64_t{1} << (32 + 2 * a);
            } else if (value < -16000) {
                now_in |= uint64_t{1} << (33 + 2 * a);
            }
        }
        const int id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(each));
        uint64_t& before = pad_input_held[id];
        if ((now_in & ~before) != 0) {
            if (!pads_pressed.contains(id)) {
                pads_pressed.insert(id); // the log says that its presses arrive, once
                qInfo("controllers: first press from %s", qPrintable(PadName(each)));
            }
            if (active_pad != id) {
                qInfo("controllers: in use now: %s", SDL_GameControllerName(each) != nullptr ? SDL_GameControllerName(each) : "(no name)");
            }
            active_pad = id;
            steam_last = false;
        }
        before = now_in;
    }
    SDL_GameController* pad = ActivePad();
    // What its buttons are called, wherever they are shown: said again when it changes.
    if (const Home::PadNames::Names names = NamesInUse(); !(names == pad_names)) {
        pad_names = names;
        pad_style = names.family;
        Home::PadNames::SetCurrent(names);
        SetPopupKeys();
        if (ReaderUi::LookupFlow* open = OpenPopup(); open != nullptr && open->Popup() != nullptr) {
            open->Popup()->RefreshPad();
        }
        home->update();
    }
    if (controller_screen != nullptr) {
        controller_screen->SetHand(PadInUseName(), pad_names, home->Steam().Connected());
    }
    // A controller's button by SDL's number: SDL's controller or the Steam Controller.
    const auto button = [this, pad](int sdl) {
        return (pad != nullptr && SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(sdl)) != 0) ||
               (steam_buttons & (1U << sdl)) != 0;
    };
    // A controller used for the first time is asked which letters it has (pad_setup.h), once,
    // before it does anything else; the Steam Controller has its own and is not asked.
    if (pad != nullptr && !steam_last && pad_setup == nullptr && controller_screen == nullptr && active_pad >= 0) {
        const QString key = PadKey(pad);
        if (StoredLayout(key) < 0 && !pad_setup_skipped.contains(key)) {
            OpenPadSetup(key, PadInUseName(), ReportedLayout());
        }
    }
    if (pad_setup != nullptr) {
        // Every button is the setup's while it is open, by place; what was held as it opened says
        // nothing until it is let go. The game and the page hear nothing.
        // Every button by its input's id ("south", "l3", "select"), pressed and let go, so that a
        // shortcut can be learnt from one button or two held together.
        uint32_t now_held = 0;
        for (std::size_t i = 0; i < std::size(Roles); ++i) {
            const Role& role = Roles[i];
            const bool down = role.sdl >= 0 ? button(role.sdl)
                                            : (pad != nullptr && SDL_GameControllerGetAxis(pad, role.filter == 104 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                                                                                                     : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) ||
                                                  steam_axes[role.filter == 104 ? ReaderApp::SteamBridge::LeftTrigger : ReaderApp::SteamBridge::RightTrigger] > 16000;
            if (down) {
                now_held |= 1U << i;
            }
        }
        const uint32_t pressed = now_held & ~pad_setup_held;
        const uint32_t released = pad_setup_held & ~now_held & ~pad_setup_ignored;
        pad_setup_ignored &= now_held; // held as it opened: nothing until let go
        pad_setup_held = now_held;
        for (std::size_t i = 0; i < std::size(Roles); ++i) {
            const Home::ControllerMap::Input* input = Home::ControllerMap::InputByNumber(Roles[i].filter);
            if (input == nullptr || pad_setup == nullptr) {
                continue;
            }
            auto* setup = static_cast<Home::PadSetup*>(pad_setup.data());
            if ((pressed & (1U << i)) != 0) {
                setup->Pressed(input->id);
            } else if ((released & (1U << i)) != 0) {
                setup->Released(input->id);
            }
        }
        std::scoped_lock lock{pads_mutex};
        std::fill(std::begin(gamepads), std::end(gamepads), reader_core_pad{});
        return;
    }
    // The controller map (controller_screen.h) has every button as it is while it is open:
    // learnt, lit on its drawing, or its own keys. What was held as it opened does nothing.
    if (controller_screen != nullptr) {
        static uint32_t map_held = 0;
        static const ControllerScreen* map_seen = nullptr;
        uint32_t now_held = 0;
        for (std::size_t i = 0; i < std::size(Roles); ++i) {
            const Role& role = Roles[i];
            bool down = false;
            if (role.sdl >= 0) {
                down = button(role.sdl);
            } else {
                const bool left = role.filter == 104;
                down = (pad != nullptr && SDL_GameControllerGetAxis(pad, left ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) ||
                       steam_axes[left ? ReaderApp::SteamBridge::LeftTrigger : ReaderApp::SteamBridge::RightTrigger] > 16000;
            }
            if (down) {
                now_held |= 1U << i;
            }
        }
        if (map_seen != controller_screen) {
            map_seen = controller_screen;
            map_held = now_held;
        }
        for (std::size_t i = 0; i < std::size(Roles); ++i) {
            const uint32_t mask = 1U << i;
            if ((now_held & mask) != (map_held & mask)) {
                if (const Home::ControllerMap::Input* input = Home::ControllerMap::InputByNumber(Roles[i].filter)) {
                    controller_screen->Pressed(input->id, (now_held & mask) != 0);
                }
            }
        }
        map_held = now_held;
        std::scoped_lock lock{pads_mutex};
        std::fill(std::begin(gamepads), std::end(gamepads), reader_core_pad{});
        return;
    }
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
    // The guide's next step: Select and L1 show or hide it, Select and R1 tick it. A published
    // build has no guide, and those buttons are the game's.
#ifndef READER_NO_SAVE_SYNC
    static bool guide_toggle_held = false, guide_done_held = false;
    const bool select_held = game_shown && button(SDL_CONTROLLER_BUTTON_BACK) && !button(SDL_CONTROLLER_BUTTON_START);
    const bool guide_toggle = select_held && button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    const bool guide_done = select_held && button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (guide_toggle && !guide_toggle_held) {
        ToggleGuide();
    }
    if (guide_done && !guide_done_held) {
        GuideDone();
    }
    guide_toggle_held = guide_toggle;
    guide_done_held = guide_done;
    const bool guide_combo = guide_toggle || guide_done;
#else
    const bool guide_combo = false;
#endif
    // While the in-game menu overlay or a QMenu is open, the controller drives it and the game gets nothing.
    if ((game_picker != nullptr && game_picker->isVisible()) || (menu != nullptr && menu->isVisible())) {
        static uint32_t menu_held = 0;
        const std::pair<int, int> keys[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, Qt::Key_Up},       {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Qt::Key_Down},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Qt::Key_Left},   {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Qt::Key_Right},
            {SDL_CONTROLLER_BUTTON_A, Qt::Key_Return},         {SDL_CONTROLLER_BUTTON_B, Qt::Key_Escape},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Qt::Key_PageUp}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Qt::Key_PageDown},
        };
        for (const auto& [sdl, key] : keys) {
            const bool down = button(sdl);
            const uint32_t mask = 1U << sdl;
            if (down && (menu_held & mask) == 0) {
                if (game_picker != nullptr && game_picker->isVisible()) {
                    game_picker->Key(key);
                } else if (QWidget* target = QApplication::activePopupWidget() ? QApplication::activePopupWidget() : menu) {
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
    // An open dictionary popup is modal, as on Android (PopupKeys): the d-pad goes from entry to
    // entry, L1 and R1 from tab to tab (words, grammar, kanji, names), north makes a card of the
    // entry without the line's audio and east one with it, south closes it, and the left stick
    // scrolls. By position, as SDL numbers them: on the Steam Controller Y is north, B east, A
    // south, X west. The game hears none of the controller meanwhile, and a button pressed in the
    // popup stays away from it until it is let go (A closing the popup does not press A in the
    // game). The cursor's button, the menu's (Guide; Select and Start) and the back grips stay theirs.
    static uint32_t popup_swallow = 0; // by SDL's number; bits 30 and 31 the triggers
    ReaderUi::LookupFlow* const open_popup = OpenPopup();
    // Reading with the d-pad (below): buttons kept from the game for now (what starts reading,
    // and a button of a two-button start for a moment), ones it has a moment late, and whether
    // the text has the buttons (an open popup is then not modal: the left stick is the game's).
    uint32_t read_hide = 0;
    uint32_t read_ghost = 0;
    bool read_text = false;
    {
        const int cursor = ReaderUi::ReaderSession::Instance().Settings().CursorButton();
        const auto trigger_down = [&](bool left) {
            return (pad != nullptr && SDL_GameControllerGetAxis(pad, left ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) ||
                   steam_axes[left ? ReaderApp::SteamBridge::LeftTrigger : ReaderApp::SteamBridge::RightTrigger] > 16000;
        };
        uint32_t down_now = 0;
        for (const Role& role : Roles) {
            if (role.filter == cursor || role.sdl == SDL_CONTROLLER_BUTTON_GUIDE) {
                continue;
            }
            if (role.sdl >= 0 ? button(role.sdl) : trigger_down(role.filter == 104)) {
                down_now |= 1U << (role.sdl >= 0 ? role.sdl : role.filter == 104 ? 30 : 31);
            }
        }
        // A controller is the input from its first press or push of a stick, whether or not a
        // popup is open: the popup then comes up with its buttons marked (the maintainer,
        // 2026-11-20: the marks only showed after a button was pressed in the popup).
        bool pushed = false;
        if (pad != nullptr) {
            for (const SDL_GameControllerAxis axis : {SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
                                                      SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY}) {
                pushed = pushed || std::abs(SDL_GameControllerGetAxis(pad, axis)) > 16000;
            }
        }
        for (const int axis : steam_axes) {
            pushed = pushed || std::abs(axis) > 16000;
        }
        if ((down_now != 0 || pushed) && !ReaderUi::LookupPopup::PadInUse()) {
            // A controller is the input: the popup marks its buttons, by the letters on them
            // (pad_names, set above for the controller in use).
            SetPopupKeys();
            ReaderUi::LookupPopup::SetPadInUse(true);
            home->update(); // its feet show the controller's buttons now
        }
        // Reading with the d-pad, no pointer (docs/furoshiki-3.md, 31; the cursor and its words are
        // ReaderController's). On the text the d-pad goes from word to word and line to line, the
        // bumpers to the next word not in Anki, the right stick makes the match shorter or longer
        // and scrolls the popup, north and east make cards, west goes into the popup (the modal
        // layer below, where west or south come back). South, Start, the left stick and the
        // triggers stay the game's, so the game is advanced without leaving. While a choice is up
        // every button is the game's. What the reader takes stays from the game until let go.
        using ReadState = ReaderUi::ReaderController::ReadState;
        using ReadKey = ReaderUi::ReaderController::ReadKey;
        ReaderUi::ReaderController* const reader = ReaderUi::GameController();
        const auto bit = [](int sdl) { return 1U << sdl; };
        const QString read_start = ReaderUi::ReaderSession::Instance().Settings().ReadStart();
        const bool read_always = read_start == QStringLiteral("always");
        const uint32_t start_mask =
            read_always                                  ? 0
            : read_start == QStringLiteral("l3")         ? bit(SDL_CONTROLLER_BUTTON_LEFTSTICK)
            : read_start == QStringLiteral("r3")         ? bit(SDL_CONTROLLER_BUTTON_RIGHTSTICK)
            : read_start == QStringLiteral("select+a")   ? bit(SDL_CONTROLLER_BUTTON_BACK) | bit(SDL_CONTROLLER_BUTTON_A)
            : read_start == QStringLiteral("l3+r3")      ? bit(SDL_CONTROLLER_BUTTON_LEFTSTICK) | bit(SDL_CONTROLLER_BUTTON_RIGHTSTICK)
            : read_start == QStringLiteral("lb+rb")      ? bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) | bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                                                         : bit(SDL_CONTROLLER_BUTTON_BACK);
        const bool can_read = reader != nullptr && game_shown && ReaderUi::ReaderSession::Instance().IsActive();
        static uint32_t start_held = 0;
        static bool start_tap = false;
        static qint64 start_since = 0;
        static bool start_fired = false;
        static std::deque<std::pair<qint64, uint32_t>> start_history;
        const uint32_t start_down = down_now & start_mask;
        // A pointer really used ended reading (ReaderController::ReadPointerUsed): with "Always"
        // it does not start again by itself.
        static int pointer_ends = 0;
        static bool always_waits = false;
        static uint32_t always_held = 0;
        if (reader != nullptr && reader->ReadPointerEnds() != pointer_ends) {
            pointer_ends = reader->ReadPointerEnds();
            always_waits = true;
        }
        if (!read_always || reader == nullptr || !game_shown) {
            always_waits = false;
        }
        static bool was_always = false;
        if (was_always && !read_always && reader != nullptr && reader->Reading()) {
            reader->SetReading(false); // "Always" was just switched off in the settings
        }
        was_always = read_always;
        if (!can_read) {
            if (reader != nullptr && reader->Reading()) {
                reader->SetReading(false);
            }
            start_history.clear();
            start_fired = false;
        } else if (read_always) {
            // After a pointer ended it, "Always" waits for the d-pad or a bumper (with a line
            // up): a pointer in use is not fought. That press starts reading and does no more.
            const uint32_t wakes = bit(SDL_CONTROLLER_BUTTON_DPAD_UP) | bit(SDL_CONTROLLER_BUTTON_DPAD_DOWN) |
                                   bit(SDL_CONTROLLER_BUTTON_DPAD_LEFT) | bit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) |
                                   bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) | bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            const uint32_t woke = down_now & ~always_held & wakes;
            if (!reader->Reading() && always_waits && woke != 0 && !ReaderUi::ReaderSession::Instance().OnScreen().isEmpty()) {
                always_waits = false;
                popup_swallow |= woke;
            }
            if (!reader->Reading() && !always_waits) {
                reader->SetReading(true);
            }
            start_history.clear();
        } else if ((start_mask & (start_mask - 1)) == 0) {
            // One button: a tap of it alone, counted as it is let go (held with Start it is the
            // menu's, with a bumper the guide's). The game never has it.
            if ((start_down & ~start_held) != 0) {
                start_tap = (down_now & ~start_mask) == 0;
                start_since = now;
            }
            if (start_down != 0 && ((down_now & ~start_mask) != 0 || both)) {
                start_tap = false;
            }
            if (start_down == 0 && start_held != 0 && start_tap && now - start_since < 700) {
                reader->SetReading(!reader->Reading());
            }
            read_hide = start_down;
            start_history.clear();
        } else {
            // Two buttons together: the game hears neither. Each alone is the game's, a moment
            // late (the game has them as they were 70 ms ago), so that the second can still come.
            if (start_down == start_mask && !start_fired) {
                start_fired = true;
                start_history.clear();
                reader->SetReading(!reader->Reading());
            }
            if (start_fired) {
                read_hide = start_down;
                start_fired = start_down != 0;
            } else {
                start_history.emplace_back(now, start_down);
                while (start_history.size() > 1 && start_history[1].first <= now - 70) {
                    start_history.pop_front();
                }
                const uint32_t late = start_history.front().first <= now - 70 ? start_history.front().second : 0;
                read_hide = start_down & ~late;
                read_ghost = late & ~start_down;
            }
        }
        start_held = start_down;
        always_held = down_now;
        // The buttons as the reading has them: without what starts it, with what comes late.
        const uint32_t seen = (down_now & ~read_hide) | read_ghost;
        static uint32_t seen_held = 0;
        const uint32_t seen_pressed = seen & ~seen_held & ~popup_swallow;
        seen_held = seen;
        const ReadState read_state = reader != nullptr ? reader->ReadingState() : ReadState::Off;
        static ReadState read_was = ReadState::Off;
        if (read_state == ReadState::Game && read_was != ReadState::Game && read_was != ReadState::Off) {
            reader->Lookups().Close(); // a choice is up: the buttons are the game's until the next line
        }
        read_was = read_state;
        uint32_t popup_skip = 0;
        if (read_state == ReadState::Text || (read_state == ReadState::Idle && !read_always)) {
            read_text = true;
            uint32_t taken = bit(SDL_CONTROLLER_BUTTON_DPAD_UP) | bit(SDL_CONTROLLER_BUTTON_DPAD_DOWN) | bit(SDL_CONTROLLER_BUTTON_DPAD_LEFT) |
                             bit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) | bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) |
                             bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) | bit(SDL_CONTROLLER_BUTTON_X) | bit(SDL_CONTROLLER_BUTTON_Y) |
                             bit(SDL_CONTROLLER_BUTTON_B);
            // The d-pad and the bumpers repeat while held, faster as they go.
            static const std::pair<int, ReadKey> repeating[] = {
                {SDL_CONTROLLER_BUTTON_DPAD_LEFT, ReadKey::Left},             {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, ReadKey::Right},
                {SDL_CONTROLLER_BUTTON_DPAD_UP, ReadKey::Up},                 {SDL_CONTROLLER_BUTTON_DPAD_DOWN, ReadKey::Down},
                {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, ReadKey::UnknownBefore}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, ReadKey::UnknownNext},
            };
            static int repeat_button = -1;
            static qint64 repeat_at = 0;
            static int repeats = 0;
            for (const auto& [sdl, key] : repeating) {
                if ((seen_pressed & bit(sdl)) != 0) {
                    reader->ReadKeyPressed(key);
                    repeat_button = sdl;
                    repeat_at = now + 380;
                    repeats = 0;
                } else if (repeat_button == sdl) {
                    if ((seen & bit(sdl)) == 0) {
                        repeat_button = -1;
                    } else if (now >= repeat_at) {
                        reader->ReadKeyPressed(key);
                        repeat_at = now + std::max(60, 170 - 18 * ++repeats);
                    }
                }
            }
            if (read_state == ReadState::Text) {
                if ((seen_pressed & bit(SDL_CONTROLLER_BUTTON_X)) != 0) {
                    reader->ReadKeyPressed(ReadKey::Popup);
                }
                if (open_popup != nullptr && open_popup == &reader->Lookups()) {
                    if ((seen_pressed & bit(SDL_CONTROLLER_BUTTON_Y)) != 0) {
                        open_popup->OnPad(ReaderUi::LookupFlow::PadKey::Card);
                    }
                    if ((seen_pressed & bit(SDL_CONTROLLER_BUTTON_B)) != 0) {
                        open_popup->OnPad(ReaderUi::LookupFlow::PadKey::CardWithAudio);
                    }
                    if (open_popup->IsAsking()) {
                        // A card's question about its audio is up: south answers "neither", and
                        // does not go on in the game.
                        taken |= bit(SDL_CONTROLLER_BUTTON_A);
                        if ((seen_pressed & bit(SDL_CONTROLLER_BUTTON_A)) != 0) {
                            open_popup->OnPad(ReaderUi::LookupFlow::PadKey::Close);
                        }
                    }
                }
                // The right stick: across, the match a character shorter or longer (once a
                // flick); up and down scroll the popup.
                const auto right_stick = [&](SDL_GameControllerAxis sdl_axis, int steam_axis) {
                    const int from_sdl = pad != nullptr ? SDL_GameControllerGetAxis(pad, sdl_axis) : 0;
                    const int from_steam = steam_axes[steam_axis];
                    return std::abs(from_steam) > std::abs(from_sdl) ? from_steam : from_sdl;
                };
                static int flick = 0;
                const int across = right_stick(SDL_CONTROLLER_AXIS_RIGHTX, ReaderApp::SteamBridge::RightX);
                const int flick_now = across > 20000 ? 1 : across < -20000 ? -1 : std::abs(across) < 10000 ? 0 : flick;
                if (flick_now != flick && flick_now != 0) {
                    reader->ReadKeyPressed(flick_now > 0 ? ReadKey::Longer : ReadKey::Shorter);
                }
                flick = flick_now;
                if (open_popup != nullptr) {
                    open_popup->PadScroll(static_cast<float>(right_stick(SDL_CONTROLLER_AXIS_RIGHTY, ReaderApp::SteamBridge::RightY)) / 32767.0F);
                }
            }
            popup_swallow |= down_now & taken;
        } else if (read_state == ReadState::Popup) {
            // In the popup west or south go back to the text (south does not close it here).
            popup_skip = bit(SDL_CONTROLLER_BUTTON_A) | bit(SDL_CONTROLLER_BUTTON_X);
            if ((seen_pressed & popup_skip) != 0) {
                reader->ReadKeyPressed(ReadKey::Text);
            }
        }
        if (read_state == ReadState::Text || read_state == ReadState::Popup || (read_state == ReadState::Idle && !read_always)) {
            // While reading the triggers go from tab to tab of the popup (the bumpers are the
            // words not in Anki on the text), and are kept from the game as long as they are held.
            const uint32_t triggers = (1U << 30) | (1U << 31);
            if (open_popup != nullptr && open_popup == &reader->Lookups()) {
                if ((seen_pressed & (1U << 30)) != 0) {
                    open_popup->OnPad(ReaderUi::LookupFlow::PadKey::TabBefore);
                }
                if ((seen_pressed & (1U << 31)) != 0) {
                    open_popup->OnPad(ReaderUi::LookupFlow::PadKey::TabNext);
                }
            }
            popup_swallow |= down_now & triggers;
        }
        // The popup's marks at the ends of its tabs: the triggers while reading, else the bumpers.
        static int keys_reading = -1;
        if (const int now_reading = read_state != ReadState::Off ? 1 : 0; now_reading != keys_reading) {
            keys_reading = now_reading;
            SetPopupKeys();
            if (open_popup != nullptr && open_popup->Popup() != nullptr) {
                open_popup->Popup()->RefreshPad();
            }
        }
        ShowReadHud();
        static uint32_t popup_held = 0;
        if (open_popup != nullptr && !read_text) {
            using Key = ReaderUi::LookupFlow::PadKey;
            const std::pair<int, Key> popup_keys[] = {
                {SDL_CONTROLLER_BUTTON_DPAD_UP, Key::EntryBefore},   {SDL_CONTROLLER_BUTTON_DPAD_DOWN, Key::EntryNext},
                {SDL_CONTROLLER_BUTTON_DPAD_LEFT, Key::KanjiBefore}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Key::KanjiNext},
                {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Key::TabBefore}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Key::TabNext},
                {SDL_CONTROLLER_BUTTON_Y, Key::Card},                {SDL_CONTROLLER_BUTTON_B, Key::CardWithAudio},
                {SDL_CONTROLLER_BUTTON_A, Key::Close},
            };
            // Select held: its chords with L1 and R1 are the guide's, not the tabs'.
            const uint32_t pressed = button(SDL_CONTROLLER_BUTTON_BACK) ? 0 : down_now & ~popup_held & ~popup_swallow & ~popup_skip;
            for (const auto& [sdl, key] : popup_keys) {
                if ((pressed & (1U << sdl)) != 0) {
                    open_popup->OnPad(key);
                    if (key == Key::Close) {
                        break;
                    }
                }
            }
            const int stick_y = pad != nullptr ? SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) : 0;
            const int steam_y = steam_axes[ReaderApp::SteamBridge::LeftY]; // as SDL has it: down positive
            const int y = std::abs(steam_y) > std::abs(stick_y) ? steam_y : stick_y;
            if (!Reader::InputFilter::Instance().SticksHeld()) {
                open_popup->PadScroll(static_cast<float>(y) / 32767.0F);
            }
            popup_swallow |= down_now;
        }
        popup_held = down_now;
        popup_swallow &= down_now;
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
        const bool page_shown = !game_shown && pad != nullptr && !steam_last && open_popup == nullptr;
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
        // The buttons the controller map gives an action (the console's own first): theirs, not the game's.
        if (!button_actions_known) {
            button_actions.clear();
            const auto& settings = ReaderUi::ReaderSession::Instance().Settings();
            for (const Home::ControllerMap::Input& input : Home::ControllerMap::Load().inputs) {
                if (input.number >= 0) {
                    const QString action = settings.Value(QStringLiteral("reader_button_action_") + QString::number(input.number),
                                                          QStringLiteral("none")).toString();
                    if (!action.isEmpty() && action != QStringLiteral("none")) {
                        button_actions.insert(input.number, action);
                    }
                }
            }
            // The console's buttons, each on the controller's button the map gave it.
            for (std::size_t t = 0; t < std::size(Roles); ++t) {
                game_source[t] = static_cast<int>(t);
                const Home::ControllerMap::Input* own = Home::ControllerMap::InputByNumber(Roles[t].filter);
                if (own == nullptr) {
                    continue;
                }
                const QString source = settings.Value(QStringLiteral("pad_button_") + own->id, own->id).toString();
                const Home::ControllerMap::Input* from = Home::ControllerMap::InputById(source);
                game_source[t] = -1;
                for (std::size_t r = 0; from != nullptr && r < std::size(Roles); ++r) {
                    if (Roles[r].filter == from->number) {
                        game_source[t] = static_cast<int>(r);
                    }
                }
            }
            chord_actions = Home::ControllerMap::Chords(ControllerStore(), QString::fromUtf8(info.key));
            button_actions_known = true;
        }
        // Two buttons held together that do an action: it is done as the second goes down, and
        // neither reaches the game until it is let go (the first may have reached it a moment).
        static uint64_t chord_kept = 0; // by role
        static QSet<QString> chords_held;
        {
            uint64_t raw = 0;
            for (std::size_t i = 0; i < std::size(Roles); ++i) {
                const Role& role = Roles[i];
                const bool down = role.sdl >= 0 ? button(role.sdl)
                                                : (pad != nullptr && SDL_GameControllerGetAxis(pad, role.filter == 104 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                                                                                                         : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000);
                raw |= down ? uint64_t{1} << i : 0;
            }
            const auto index_of = [](int filter) {
                for (std::size_t i = 0; i < std::size(Roles); ++i) {
                    if (Roles[i].filter == filter) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };
            for (const Home::ControllerMap::ChordBinding& chord : std::as_const(chord_actions)) {
                const int a = index_of(chord.a), b = index_of(chord.b);
                if (a < 0 || b < 0) {
                    continue;
                }
                const uint64_t both = (uint64_t{1} << a) | (uint64_t{1} << b);
                if ((raw & both) == both) {
                    if (!chords_held.contains(chord.id) && open_popup == nullptr) {
                        ReaderUi::RunAction(chord.action);
                    }
                    chords_held.insert(chord.id);
                    chord_kept |= both;
                } else {
                    chords_held.remove(chord.id);
                }
            }
            chord_kept &= raw;
        }
        uint64_t usable = 0; // the controller's buttons down that the game may have
        for (std::size_t i = 0; i < std::size(Roles); ++i) {
            const Role& role = Roles[i];
            bool down = false;
            const uint32_t role_bit = 1U << (role.sdl >= 0 ? role.sdl : role.filter == 104 ? 30 : 31);
            if (((popup_swallow | read_hide) & role_bit) != 0 || ((chord_kept >> i) & 1) != 0) {
                // The open popup's (or it was when it went down), or the reading's: not the
                // game's, nor an action's.
            } else if (role.sdl >= 0) {
                // Select and Start held together are the menu's, not the game's.
                down = button(role.sdl) && !(both && (role.sdl == SDL_CONTROLLER_BUTTON_BACK || role.sdl == SDL_CONTROLLER_BUTTON_START)) &&
                       !(guide_combo && (role.sdl == SDL_CONTROLLER_BUTTON_BACK || role.sdl == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
                                         role.sdl == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
            } else {
                const bool left = role.filter == 104;
                const auto axis = left ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                down = (pad != nullptr && SDL_GameControllerGetAxis(pad, axis) > 16000) ||
                       steam_axes[left ? ReaderApp::SteamBridge::LeftTrigger : ReaderApp::SteamBridge::RightTrigger] > 16000;
            }
            if ((read_ghost & role_bit) != 0) {
                down = true; // a button of a two-button start, let go within the moment it is kept back
            }
            const uint64_t mask = uint64_t{1} << i;
            if (down != ((held & mask) != 0)) {
                held = down ? held | mask : held & ~mask;
                const QString action = role.filter >= 0 ? button_actions.value(role.filter) : QString();
                if (!action.isEmpty()) {
                    // A button given an action does it as it goes down, and the game never has it.
                    if (down) {
                        ReaderUi::RunAction(action);
                    }
                    taken = down ? taken | mask : taken & ~mask;
                } else {
                    // The reader's own buttons (its cursor's, its menu's) never reach the game,
                    // for as long as they are held.
                    const bool kept = role.filter >= 0 && filter.OnButton(device, static_cast<std::size_t>(role.filter), down);
                    taken = kept ? taken | mask : taken & ~mask;
                }
            }
            if (down && (taken & mask) == 0) {
                usable |= mask;
            }
            if (role.sdl < 0 && down && (taken & mask) != 0) {
                trigger_taken[role.filter == 104 ? 0 : 1] = true;
            }
        }
        // Each button of the console, from the controller's button it is on.
        int trigger_from[2]{-2, -2}; // -2: its own trigger, as far as it is pulled
        for (std::size_t t = 0; t < std::size(Roles); ++t) {
            const int source = game_source[t];
            if (Roles[t].sdl < 0 && source != static_cast<int>(t)) {
                trigger_from[Roles[t].filter == 104 ? 0 : 1] = source;
            }
            const int bit = BitOf(info, Roles[t]);
            if (source >= 0 && (usable & (uint64_t{1} << source)) != 0 && bit >= 0) {
                state.buttons |= uint64_t{1} << bit;
            }
        }
        // Reading with the d-pad: south goes on in the game, so where the console's "yes" is
        // another button (○ on a Japanese PlayStation or PSP game) south presses that one, as
        // Space does on the keyboard. East is the reader's there (a card with audio).
        if (read_text && info.button_yes >= 0) {
            const int south = BitOf(info, Roles[4]);
            if (south >= 0 && south != info.button_yes && (state.buttons & (uint64_t{1} << south)) != 0) {
                state.buttons = (state.buttons & ~(uint64_t{1} << south)) | (uint64_t{1} << info.button_yes);
            }
        }
        // SDL's controller and the Steam Controller together: the one pushed further counts.
        const auto axis = [this, pad](SDL_GameControllerAxis sdl_axis, int steam_axis) {
            const int from_sdl = pad != nullptr ? SDL_GameControllerGetAxis(pad, sdl_axis) : 0;
            const int from_steam = steam_axes[steam_axis];
            return std::abs(from_steam) > std::abs(from_sdl) ? from_steam : from_sdl;
        };
        namespace Steam = ReaderApp::SteamBridge;
        if (!filter.SticksHeld() && (open_popup == nullptr || read_text)) {
            state.sticks[0] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_LEFTX, Steam::LeftX));
            state.sticks[1] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_LEFTY, Steam::LeftY));
            if (!read_text) { // reading: the right stick is the reader's
                state.sticks[2] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_RIGHTX, Steam::RightX));
                state.sticks[3] = static_cast<int16_t>(axis(SDL_CONTROLLER_AXIS_RIGHTY, Steam::RightY));
            }
        }
        // A trigger put on another button is all or nothing; on its own, as far as it is pulled.
        for (int side = 0; side < 2; ++side) {
            if (trigger_from[side] != -2) {
                state.triggers[side] = trigger_from[side] >= 0 && (usable & (uint64_t{1} << trigger_from[side])) != 0 ? 255 : 0;
            } else if (!trigger_taken[side] && (popup_swallow & (1U << (side == 0 ? 30 : 31))) == 0) {
                state.triggers[side] = static_cast<uint8_t>(std::max(
                    0, axis(side == 0 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                            side == 0 ? Steam::LeftTrigger : Steam::RightTrigger) >> 7));
            }
        }
    }
    // The other ports: SDL's other controllers, each as it is (the reader's buttons are the
    // first controller's only). The Steam Controller's own gamepad, where SDL sees it too, is
    // the first controller already. While a popup is open or the d-pad reads, the game hears
    // none of them: a second pad must not play the game under the popup either.
    reader_core_pad others[MaxPorts]{};
    std::fill(std::begin(port_pads), std::end(port_pads), nullptr);
    port_pads[0] = pad;
    const ReaderUi::ReaderController* const reading_now = ReaderUi::GameController();
    const bool others_quiet = open_popup != nullptr || (reading_now != nullptr && reading_now->Reading());
    const uint32_t ports = current != nullptr ? std::min<uint32_t>(PortCount(*current->core.info), MaxPorts) : 1;
    for (uint32_t port = 1, next = 0; port < ports && next < sdl_pads.size(); ++next) {
        SDL_GameController* other = sdl_pads[next];
        if (other == pad) {
            continue;
        }
        const char* name = SDL_GameControllerName(other);
        if (steam_seen && name != nullptr && QString::fromUtf8(name).contains(QStringLiteral("Steam"), Qt::CaseInsensitive)) {
            continue;
        }
        port_pads[port] = other;
        reader_core_pad& to = others[port++];
        to.connected = 1;
        if (others_quiet) {
            continue;
        }
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

void MainWindow::SetPopupKeys() {
    const ReaderUi::ReaderController* reader = ReaderUi::GameController();
    const bool reading = reader != nullptr && reader->Reading();
    ReaderUi::LookupPopup::SetPadKeys(QJsonObject{
        {QStringLiteral("add"), pad_names.north},
        {QStringLiteral("addAudio"), pad_names.east},
        {QStringLiteral("left"), reading ? pad_names.l2 : pad_names.l1},
        {QStringLiteral("right"), reading ? pad_names.r2 : pad_names.r1}});
}

QStringList MainWindow::TriggerWords() const {
    return {pad_names.l2, pad_names.r2};
}

QString MainWindow::SelectWord() const {
    return pad_names.select;
}

QStringList MainWindow::ReadStartCaps() const {
    const QString start = ReaderUi::ReaderSession::Instance().Settings().ReadStart();
    if (start == QStringLiteral("always")) {
        return {};
    }
    if (start == QStringLiteral("l3")) {
        return {pad_names.l3};
    }
    if (start == QStringLiteral("r3")) {
        return {pad_names.r3};
    }
    if (start == QStringLiteral("select+a")) {
        return {pad_names.select, pad_names.south};
    }
    if (start == QStringLiteral("l3+r3")) {
        return {pad_names.l3, pad_names.r3};
    }
    if (start == QStringLiteral("lb+rb")) {
        return {pad_names.l1, pad_names.r1};
    }
    return {pad_names.select};
}

SDL_GameController* MainWindow::ActivePad() const {
    for (SDL_GameController* each : sdl_pads) {
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(each)) == active_pad) {
            return each;
        }
    }
    return sdl_pads.empty() ? nullptr : sdl_pads.front();
}

namespace {

Home::PadNames::Kind KindOf(SDL_GameControllerType type) {
    using Kind = Home::PadNames::Kind;
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360:
        return Kind::Xbox360;
    case SDL_CONTROLLER_TYPE_XBOXONE:
        return Kind::XboxOne;
    case SDL_CONTROLLER_TYPE_PS3:
        return Kind::PS3;
    case SDL_CONTROLLER_TYPE_PS4:
        return Kind::PS4;
    case SDL_CONTROLLER_TYPE_PS5:
        return Kind::PS5;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return Kind::SwitchPro;
    case SDL_CONTROLLER_TYPE_VIRTUAL:
        return Kind::Virtual;
    case SDL_CONTROLLER_TYPE_UNKNOWN:
        return Kind::Unknown;
    default:
        return Kind::Other;
    }
}

const char* TypeName(SDL_GameControllerType type) {
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360:
        return "Xbox 360";
    case SDL_CONTROLLER_TYPE_XBOXONE:
        return "Xbox One";
    case SDL_CONTROLLER_TYPE_PS3:
        return "PS3";
    case SDL_CONTROLLER_TYPE_PS4:
        return "PS4";
    case SDL_CONTROLLER_TYPE_PS5:
        return "PS5";
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
        return "Switch Pro";
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return "Joy-Con";
    case SDL_CONTROLLER_TYPE_VIRTUAL:
        return "virtual";
    case SDL_CONTROLLER_TYPE_UNKNOWN:
        return "unknown";
    default:
        return "other";
    }
}

} // namespace

namespace {

/// The layouts set up, by controller key, as read from or written to the program's QSettings.
QHash<QString, int>& LayoutCache() {
    static QHash<QString, int> known;
    return known;
}

/// Keeps a controller's layout (-1: none, so it is asked again).
void SetStoredLayout(const QString& key, int family) {
    if (key.isEmpty()) {
        return;
    }
    if (family < 0) {
        QSettings().remove(QStringLiteral("pad_layouts/") + key);
    } else {
        QSettings().setValue(QStringLiteral("pad_layouts/") + key, Home::PadNames::LayoutKey(family));
    }
    LayoutCache().insert(key, family);
}

} // namespace

Home::PadNames::Names MainWindow::NamesInUse() const {
    SDL_GameController* pad = ActivePad();
    if (pad == nullptr || steam_last) {
        return Home::PadNames::Of({}); // the Steam Controller, or none: Select and Start, A B X Y
    }
    const Home::PadNames::Names names = Home::PadNames::Of({KindOf(SDL_GameControllerGetType(pad)), SDL_GameControllerGetVendor(pad),
                                                            SDL_GameControllerGetProduct(pad), PadName(pad)});
    // The letters its setup said are printed on it, where it was set up.
    const int layout = StoredLayout(PadKey(pad));
    return layout >= 0 ? Home::PadNames::WithLayout(names, layout) : names;
}

QString MainWindow::PadKey(SDL_GameController* pad) const {
    if (pad == nullptr) {
        return {};
    }
    // Pads that claim one id (an 8BitDo Pro 2, an SN30 Pro and a real Xbox One S all say
    // 045E:02E0 over Bluetooth, with one GUID and one name) are told apart by their address.
    const auto bluetooth = pad_bluetooth.find(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)));
    if (bluetooth != pad_bluetooth.end() && !bluetooth->second.address.isEmpty()) {
        return QStringLiteral("bt-") + bluetooth->second.address;
    }
    return PadKeyBySdl(pad);
}

QString MainWindow::PadName(SDL_GameController* pad) const {
    if (pad == nullptr) {
        return {};
    }
    const auto bluetooth = pad_bluetooth.find(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)));
    if (bluetooth != pad_bluetooth.end() && !bluetooth->second.name.isEmpty()) {
        return bluetooth->second.name;
    }
    const char* name = SDL_GameControllerName(pad);
    return QString::fromUtf8(name != nullptr ? name : "");
}

QString MainWindow::PadKeyBySdl(SDL_GameController* pad) {
    if (pad == nullptr) {
        return {};
    }
    char guid[64] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(pad)), guid, sizeof(guid));
    const char* name = SDL_GameControllerName(pad);
    const QByteArray both = QByteArray(guid) + '|' + QByteArray(name != nullptr ? name : "");
    return QString::fromLatin1(QCryptographicHash::hash(both, QCryptographicHash::Sha1).toHex().left(16));
}

int MainWindow::StoredLayout(const QString& key) {
    if (key.isEmpty()) {
        return -1;
    }
    // Asked many times a second: kept in memory once read; QSettings is written on a change.
    QHash<QString, int>& known = LayoutCache();
    if (const auto found = known.constFind(key); found != known.constEnd()) {
        return *found;
    }
    const int layout = Home::PadNames::LayoutOf(QSettings().value(QStringLiteral("pad_layouts/") + key).toString());
    known.insert(key, layout);
    return layout;
}

int MainWindow::ReportedLayout() const {
    SDL_GameController* pad = ActivePad();
    if (pad == nullptr) {
        return Home::PadNames::Xbox;
    }
    return Home::PadNames::Of({KindOf(SDL_GameControllerGetType(pad)), SDL_GameControllerGetVendor(pad), SDL_GameControllerGetProduct(pad),
                               PadName(pad)})
        .family;
}

void MainWindow::OpenPadSetup(const QString& key, const QString& name, int reported) {
    if (pad_setup != nullptr) {
        return;
    }
    pad_setup_key = key;
    // What is held now says nothing to it (the press that made the pad the one in use).
    pad_setup_held = ~0U;
    pad_setup_ignored = ~0U;
    const HomeWindow::ScreenLook look = home->LookOfScreen();
    // Over the page as the controller map is; over a game the whole window's, and a native
    // window of its own, as the game's picture is one.
    const bool over_game = current != nullptr && pages->currentWidget() == screen;
    QWidget* over = over_game ? static_cast<QWidget*>(pages) : static_cast<QWidget*>(home);
    // What its buttons are called by its kind (Select, LB); the setup sets the face letters.
    Home::PadNames::Names base = Home::PadNames::Of({Home::PadNames::Kind::XboxOne, 0x045E, 0x02E0, name});
    if (SDL_GameController* pad = ActivePad(); pad != nullptr && !key.isEmpty()) {
        base = Home::PadNames::Of({KindOf(SDL_GameControllerGetType(pad)), SDL_GameControllerGetVendor(pad),
                                   SDL_GameControllerGetProduct(pad), PadName(pad)});
    }
    qInfo("controllers: setup opened for %s", qPrintable(name));
    // Its third step: the reading shortcuts, kept where the controller map keeps its Shortcuts,
    // for all consoles (pad_shortcuts.h), so both show the same.
    Home::PadSetup::Shortcuts shortcuts;
    shortcuts.actions = Home::PadShortcuts::Rows();
    shortcuts.label = [](const QString& action) {
        for (const Home::ControllerMap::Action& one : Home::ControllerMap::Load().actions) {
            if (one.id == action) {
                return one.label;
            }
        }
        return action;
    };
    const auto name_of = [](const QString& input, const Home::PadNames::Names& letters) {
        QStringList out;
        for (const QString& part : input.split(QLatin1Char('+'))) {
            const QString own = Home::PadNames::Label(letters, part);
            out.append(own.isEmpty() ? Home::ControllerMap::NameOf(Home::ControllerMap::PadOf({}), part) : own);
        }
        return out.join(QStringLiteral(" + "));
    };
    shortcuts.name = name_of;
    shortcuts.current = [this, name_of](const QString& action, const Home::PadNames::Names& letters) {
        QStringList said;
        if (action == QStringLiteral("furoshiki_read")) {
            // What "Start reading with" is now, in this controller's letters.
            const QString start = ReaderUi::ReaderSession::Instance().Settings().ReadStart();
            static const QHash<QString, QString> inputs{{QStringLiteral("select"), QStringLiteral("select")},
                                                        {QStringLiteral("l3"), QStringLiteral("l3")},
                                                        {QStringLiteral("r3"), QStringLiteral("r3")},
                                                        {QStringLiteral("select+a"), QStringLiteral("select+south")},
                                                        {QStringLiteral("l3+r3"), QStringLiteral("l3+r3")},
                                                        {QStringLiteral("lb+rb"), QStringLiteral("l1+r1")}};
            said.append(start == QStringLiteral("always") ? QStringLiteral("Always during dialogue") : name_of(inputs.value(start, QStringLiteral("select")), letters));
        }
        for (const Home::ControllerMap::Action& one : Home::ControllerMap::Load().actions) {
            if (one.id == action) {
                for (const Home::ControllerMap::Binding& binding : Home::ControllerMap::BindingsOf(ControllerStore(), one, {})) {
                    said.append(name_of(binding.input, letters));
                }
            }
        }
        return said.isEmpty() ? QStringLiteral("None") : said.join(QStringLiteral(" · "));
    };
    shortcuts.conflict = [](const QString& input) { return Home::PadShortcuts::Conflict(input); };
    shortcuts.assign = [this](const QString& action, const QString& input) {
        const bool ok = Home::PadShortcuts::Assign(ControllerStore(), action, input, [](const QString& start) {
            auto& settings = ReaderUi::ReaderSession::Instance().Settings();
            settings.SetValue(ReaderApp::ReaderSettings::KeyReadStart, start);
            settings.Sync();
        });
        button_actions_known = false;
        qInfo("controllers: shortcut %s on %s%s", qPrintable(action), qPrintable(input), ok ? "" : " refused");
        return ok;
    };
    shortcuts.free = home->Steam().Connected() ? QStringLiteral("L3, R3, Select with another button, the back grips")
                                               : QStringLiteral("L3, R3, Select with another button");
    // The Steam Controller's grips are learnt here too while it is open.
    ReaderUi::CaptureBackButtons([this](int back) {
        if (pad_setup == nullptr) {
            return false;
        }
        for (const Home::ControllerMap::Input& input : Home::ControllerMap::Load().inputs) {
            if (input.back == back) {
                auto* setup = static_cast<Home::PadSetup*>(pad_setup.data());
                setup->Pressed(input.id);
                setup->Released(input.id);
            }
        }
        return true;
    });
    auto* setup = new Home::PadSetup(
        over, {look.ground, look.raised, look.ink, look.muted, look.accent, look.rule, look.font}, name, base, reported, shortcuts,
        [this, key](int family) {
            if (key.isEmpty()) {
                return; // a page script's: nothing kept
            }
            SetStoredLayout(key, family);
            qInfo("controllers: layout %s set up for %s", qPrintable(Home::PadNames::LayoutKey(family)), qPrintable(PadInUseName()));
        },
        [this, key](bool set_up) {
            if (!key.isEmpty() && StoredLayout(key) < 0) {
                pad_setup_skipped.insert(key); // closed without an answer: asked again next time
            }
            ReaderUi::CaptureBackButtons({});
            home->setFocus();
            if (set_up) {
                OpenControllerMap(true); // on its Shortcuts
            }
        });
    if (over_game) {
        setup->setAttribute(Qt::WA_NativeWindow);
    }
    setup->raise();
    setup->setFocus();
    pad_setup = setup;
}

QString MainWindow::PadInUseName() const {
    if (steam_last && home->Steam().Connected()) {
        return QStringLiteral("Steam Controller");
    }
    SDL_GameController* pad = ActivePad();
    if (pad == nullptr) {
        return home->Steam().Connected() ? QStringLiteral("Steam Controller") : QString();
    }
    const QString name = PadName(pad);
    return name.isEmpty() ? QStringLiteral("Controller") : name;
}

void MainWindow::OpenPads() {
    // One line for a pad in the log: SDL's name, GUID, vendor:product, type, where it was found
    // (XInput, HID, raw input: the path and the GUID's driver byte say) and its mapping.
    const auto said = [this](SDL_Joystick* joystick, SDL_GameController* pad, int index) {
        char guid[64] = {};
        const SDL_JoystickGUID id = joystick != nullptr ? SDL_JoystickGetGUID(joystick) : SDL_JoystickGetDeviceGUID(index);
        SDL_JoystickGetGUIDString(id, guid, sizeof(guid));
        const char* name = joystick != nullptr ? SDL_JoystickName(joystick) : SDL_JoystickNameForIndex(index);
        const char* path = joystick != nullptr ? SDL_JoystickPath(joystick) : SDL_JoystickPathForIndex(index);
        const unsigned vendor = joystick != nullptr ? SDL_JoystickGetVendor(joystick) : SDL_JoystickGetDeviceVendor(index);
        const unsigned product = joystick != nullptr ? SDL_JoystickGetProduct(joystick) : SDL_JoystickGetDeviceProduct(index);
        QString mapping = QStringLiteral("no mapping");
        if (char* text = SDL_GameControllerMappingForGUID(id)) {
            mapping = bundled_mappings.contains(QString::fromLatin1(guid).toLower()) ? QStringLiteral("mapped by the bundled database")
                                                                                     : QStringLiteral("mapped by SDL");
            SDL_free(text);
        } else if (pad != nullptr) {
            mapping = QStringLiteral("mapped by SDL");
        }
        const SDL_GameControllerType type = pad != nullptr ? SDL_GameControllerGetType(pad) : SDL_GameControllerTypeForIndex(index);
        return QStringLiteral("\"%1\", GUID %2, %3:%4, type %5, %6, path %7")
            .arg(QString::fromUtf8(name != nullptr ? name : "(no name)"), QString::fromLatin1(guid))
            .arg(vendor, 4, 16, QLatin1Char('0'))
            .arg(product, 4, 16, QLatin1Char('0'))
            .arg(QString::fromLatin1(TypeName(type)), mapping, QString::fromUtf8(path != nullptr ? path : "-"));
    };
    // Controllers gone are let go.
    std::erase_if(sdl_pads, [&](SDL_GameController* pad) {
        if (SDL_GameControllerGetAttached(pad)) {
            return false;
        }
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(pad);
        const int id = SDL_JoystickInstanceID(joystick);
        qInfo("controllers: removed %s", qPrintable(said(joystick, pad, -1)));
        pad_input_held.erase(id);
        pad_bluetooth.erase(id);
        if (active_pad == id) {
            active_pad = -1;
        }
        SDL_GameControllerClose(pad);
        return true;
    });
    // One that comes is opened: every joystick SDL has a mapping for, its own or the database's.
    QSet<int> present;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        present.insert(id);
        const bool open = std::any_of(sdl_pads.cbegin(), sdl_pads.cend(), [id](SDL_GameController* pad) {
            return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == id;
        });
        if (open) {
            continue;
        }
        if (!SDL_IsGameController(i)) {
            if (!pads_unmapped.contains(id)) {
                pads_unmapped.insert(id);
                qInfo("controllers: not used (SDL has no mapping for it; a line for it in gamecontrollerdb.txt in the "
                      "program's folder adds one): %s",
                      qPrintable(said(nullptr, nullptr, i)));
            }
            continue;
        }
        if (SDL_GameController* pad = SDL_GameControllerOpen(i)) {
            sdl_pads.push_back(pad);
            SDL_Joystick* joystick = SDL_GameControllerGetJoystick(pad);
            QString bluetooth;
#if defined(_WIN32)
            // Its Bluetooth device, once: the address tells alike pads apart, the name says what it is.
            const char* path = SDL_JoystickPath(joystick);
            const Home::PadBluetooth::Found found = Home::PadBluetooth::Find(QString::fromUtf8(path != nullptr ? path : ""),
                                                                             SDL_JoystickGetVendor(joystick), SDL_JoystickGetProduct(joystick));
            pad_bluetooth[id] = found;
            bluetooth = found.address.isEmpty() ? QStringLiteral(", Bluetooth address unknown")
                                                : QStringLiteral(", Bluetooth %1 \"%2\"").arg(found.address, found.name);
            // An answer kept under the old key (GUID and name) is taken over once by the address's.
            if (!found.address.isEmpty()) {
                const QString by_address = PadKey(pad);
                const int before = StoredLayout(PadKeyBySdl(pad));
                if (StoredLayout(by_address) < 0 && before >= 0) {
                    SetStoredLayout(by_address, before);
                }
            }
#endif
            qInfo("controllers: added %s%s", qPrintable(said(joystick, pad, i)), qPrintable(bluetooth));
        } else if (!pads_unmapped.contains(id)) {
            pads_unmapped.insert(id); // said once, tried again at every look
            qWarning("controllers: couldn't open %s: %s", qPrintable(said(nullptr, nullptr, i)), SDL_GetError());
        }
    }
    pads_unmapped.intersect(present);
}

void MainWindow::ShowReadHud() {
    using ReadState = ReaderUi::ReaderController::ReadState;
    ReaderUi::ReaderController* const reader = ReaderUi::GameController();
    if (reader == nullptr || reader->Overlay() == nullptr) {
        return;
    }
    const ReaderApp::ReaderSettings& settings = ReaderUi::ReaderSession::Instance().Settings();
    const ReadState state = reader->ReadingState();
    const QString start = settings.ReadStart();
    // With "Always" and nothing to read, nothing is said: the game is simply played.
    const bool said = state != ReadState::Off && !(state == ReadState::Idle && start == QStringLiteral("always"));
    QString mark;
    QList<QPair<QStringList, QString>> hints;
    if (said && settings.ReadMark()) {
        mark = state == ReadState::Game ? QStringLiteral("GAME · A CHOICE IS UP") : QStringLiteral("READING");
    }
    if (said && settings.ReadHints() && (state == ReadState::Text || state == ReadState::Popup)) {
        // The buttons by the letters on them, where they are (SDL numbers them by position).
        const QString south = pad_names.south;
        const QString east = pad_names.east;
        const QString west = pad_names.west;
        const QString north = pad_names.north;
        const QStringList bumpers{pad_names.l1, pad_names.r1};
        if (state == ReadState::Text) {
            hints = {{{QStringLiteral("←"), QStringLiteral("→")}, QStringLiteral("WORD")},
                     {{QStringLiteral("↑"), QStringLiteral("↓")}, QStringLiteral("LINE")},
                     {bumpers, QStringLiteral("NOT IN ANKI")},
                     {{south}, QStringLiteral("NEXT LINE")},
                     {{north}, QStringLiteral("CARD")},
                     {{east}, QStringLiteral("CARD + AUDIO")},
                     {{west}, QStringLiteral("POPUP")}};
        } else {
            hints = {{{QStringLiteral("↑"), QStringLiteral("↓")}, QStringLiteral("ENTRY")},
                     {{north}, QStringLiteral("CARD")},
                     {{east}, QStringLiteral("CARD + AUDIO")},
                     {{west}, QStringLiteral("TEXT")}};
        }
        // The triggers go from tab to tab, where the popup showing has more than one.
        if (const ReaderUi::LookupPopup* popup = reader->Lookups().Popup();
            popup != nullptr && reader->Lookups().IsOpen() && popup->Tabs() > 1) {
            hints.insert(state == ReadState::Text ? 3 : 1, {TriggerWords(), QStringLiteral("TAB")});
        }
        if (const QStringList caps = ReadStartCaps(); !caps.isEmpty()) {
            QStringList upper;
            for (const QString& cap : caps) {
                upper.append(cap.toUpper());
            }
            hints.append({upper, QStringLiteral("STOP")});
        }
    }
    QString shown = mark;
    for (const auto& [caps, action] : std::as_const(hints)) {
        shown += QLatin1Char('|') + caps.join(QLatin1Char(' ')) + QLatin1Char(' ') + action;
    }
    if (shown == read_hud_shown) {
        return;
    }
    read_hud_shown = shown;
    if (shown.isEmpty()) {
        reader->Overlay()->SetHud(nullptr);
        return;
    }
    reader->Overlay()->SetHud([this, reader, mark, hints](QPainter& painter, const QRect&) {
        home->PaintReadingHud(painter, reader->PictureArea(), mark, hints);
    });
}

QString MainWindow::ReadCommand(const QString& what) {
    using ReadState = ReaderUi::ReaderController::ReadState;
    using ReadKey = ReaderUi::ReaderController::ReadKey;
    ReaderUi::ReaderController* const reader = ReaderUi::GameController();
    if (reader == nullptr || current == nullptr || !ReaderUi::ReaderSession::Instance().IsActive()) {
        return QStringLiteral("no game is being read");
    }
    static const QHash<QString, ReadKey> keys{
        {QStringLiteral("left"), ReadKey::Left},        {QStringLiteral("right"), ReadKey::Right},
        {QStringLiteral("up"), ReadKey::Up},            {QStringLiteral("down"), ReadKey::Down},
        {QStringLiteral("lb"), ReadKey::UnknownBefore}, {QStringLiteral("rb"), ReadKey::UnknownNext},
        {QStringLiteral("shorter"), ReadKey::Shorter},  {QStringLiteral("longer"), ReadKey::Longer},
    };
    const ReadState state = reader->ReadingState();
    if (what == QStringLiteral("start") || what == QStringLiteral("stop")) {
        reader->SetReading(what == QStringLiteral("start"));
    } else if (what == QStringLiteral("toggle")) {
        reader->SetReading(!reader->Reading());
    } else if (const auto key = keys.constFind(what); key != keys.constEnd()) {
        if (state == ReadState::Text) {
            reader->ReadKeyPressed(*key);
        } else if (state == ReadState::Popup && (what == QStringLiteral("up") || what == QStringLiteral("down"))) {
            reader->Lookups().OnPad(what == QStringLiteral("up") ? ReaderUi::LookupFlow::PadKey::EntryBefore : ReaderUi::LookupFlow::PadKey::EntryNext);
        } else if (state == ReadState::Popup && (what == QStringLiteral("lb") || what == QStringLiteral("rb"))) {
            reader->Lookups().OnPad(what == QStringLiteral("lb") ? ReaderUi::LookupFlow::PadKey::TabBefore : ReaderUi::LookupFlow::PadKey::TabNext);
        }
    } else if (what == QStringLiteral("lt") || what == QStringLiteral("rt")) {
        if (state == ReadState::Text || state == ReadState::Popup) {
            reader->Lookups().OnPad(what == QStringLiteral("lt") ? ReaderUi::LookupFlow::PadKey::TabBefore : ReaderUi::LookupFlow::PadKey::TabNext);
        }
    } else if (what == QStringLiteral("pointer")) {
        // A pointer really used: two places far apart, as the game's window would say them.
        reader->OnPointerMove(QPointF(40, 40));
        reader->OnPointerMove(QPointF(140, 120));
    } else if (what == QStringLiteral("x")) {
        reader->ReadKeyPressed(state == ReadState::Popup ? ReadKey::Text : ReadKey::Popup);
    } else if (what == QStringLiteral("y") || what == QStringLiteral("b")) {
        if (state == ReadState::Text || state == ReadState::Popup) {
            reader->Lookups().OnPad(what == QStringLiteral("y") ? ReaderUi::LookupFlow::PadKey::Card : ReaderUi::LookupFlow::PadKey::CardWithAudio);
        }
    } else if (what == QStringLiteral("a")) {
        if (state == ReadState::Popup) {
            reader->ReadKeyPressed(ReadKey::Text);
        } else {
            // South is the game's: its button held a moment, through the controller's own way.
            steam_buttons |= 1U << SDL_CONTROLLER_BUTTON_A;
            steam_seen = true;
            QTimer::singleShot(120, this, [this] { steam_buttons &= ~(1U << SDL_CONTROLLER_BUTTON_A); });
        }
    } else if (what != QStringLiteral("say")) {
        return QStringLiteral("read: start, stop, toggle, left, right, up, down, lb, rb, lt, rt, shorter, longer, x, y, b, a, pointer or say");
    }
    ShowReadHud();
    return reader->ReadingSaid();
}

ReaderUi::LookupFlow* MainWindow::OpenPopup() const {
    if (ReaderUi::LookupFlow* game = ReaderUi::GameLookups(); game != nullptr && game->IsOpen()) {
        return game;
    }
    if (overlay != nullptr && overlay->Lookups().IsOpen()) {
        return &overlay->Lookups();
    }
    if (strip != nullptr && strip->Lookups().IsOpen()) {
        return &strip->Lookups();
    }
    return nullptr;
}

void MainWindow::CloseGamePicker() {
    if (game_picker != nullptr) {
        game_picker_replacing = true;
        game_picker->hide();
        delete game_picker;
        game_picker = nullptr;
        game_picker_replacing = false;
    }
}

void MainWindow::OpenPickerOverlay(const QString& title, const QStringList& labels, int now, std::function<void(int)> picked) {
    if (labels.isEmpty()) {
        return;
    }
    CloseGamePicker();
    ReaderUi::ProgramPaused(true);
    Home::ChoicePicker::Colours colours;
    colours.scale = std::clamp(std::min(width() / 1280.0, height() / 720.0), 0.75, 1.8);
    colours.card = QColor(QStringLiteral("#101a3a"));
    colours.txt = QColor(QStringLiteral("#ece5d3"));
    colours.sel = QColor(QStringLiteral("#ece5d3"));
    colours.dim = QColor(QStringLiteral("#8a93b5"));
    colours.accent = QColor(QStringLiteral("#d9472b"));
    colours.rule = QColor(236, 229, 211, 62);
    colours.title = QFont(QStringLiteral("Michroma"), static_cast<int>(std::lround(11 * colours.scale)));
    colours.title.setLetterSpacing(QFont::PercentageSpacing, 114);
    colours.row = QFont(QStringLiteral("Space Grotesk"), static_cast<int>(std::lround(15 * colours.scale)), QFont::Medium);
    game_picker = Home::ChoicePicker::Open(screen, title, labels, now, colours, [this, fn = std::move(picked)](int index) {
        QTimer::singleShot(0, this, [this, fn, index] {
            if (fn) {
                fn(index);
            }
        });
    });
    connect(game_picker, &QObject::destroyed, this, [this] {
        if (!game_picker_replacing && (menu == nullptr || !menu->isVisible()) && pages->currentWidget() == screen) {
            ReaderUi::ProgramPaused(false);
            screen->setFocus();
        }
    });
}

void MainWindow::OpenStatePicker(bool load) {
    if (current == nullptr) {
        return;
    }
    QStringList labels;
    std::vector<std::function<void()>> actions;
    labels.append(QStringLiteral("‹ Back to Game menu"));
    actions.push_back([this] { OpenMenu(); });
    const int count = StateSlots(*current->core.info);
    for (int slot = 1; slot <= count; ++slot) {
        const std::optional<CoreSession::StateSlot> info = current->session->StateInfo(slot);
        if (load && info && !info->exists) {
            continue;
        }
        QString text = QStringLiteral("Slot %1").arg(slot);
        if (info && info->exists) {
            text += QStringLiteral("  ·  ") + QDateTime::fromMSecsSinceEpoch(info->saved_ms).toString(QStringLiteral("yyyy/MM/dd HH:mm"));
        } else if (info) {
            text += QStringLiteral("  ·  Empty");
        }
        if (slot == state_slot) {
            text += load ? QStringLiteral("  ·  Quick load") : QStringLiteral("  ·  Quick save");
        }
        labels.append(text);
        actions.push_back([this, slot, load] { load ? LoadStateFrom(slot) : SaveStateTo(slot); });
    }
    OpenPickerOverlay(load ? QStringLiteral("Load state") : QStringLiteral("Save state"), labels, 1 < labels.size() ? 1 : 0,
                      [actions = std::move(actions)](int index) {
                          if (index >= 0 && index < static_cast<int>(actions.size())) {
                              actions[index]();
                          }
                      });
}

void MainWindow::OpenReadingMenu() {
    if (current == nullptr) {
        return;
    }
    const auto trigger_reader = [](const char* name) {
        if (QMenu* reader = ReaderUi::Menu()) {
            if (auto* act = reader->findChild<QAction*>(QString::fromLatin1(name))) {
                act->trigger();
            }
        }
    };
    QStringList labels;
    std::vector<std::function<void()>> actions;
    const auto add = [&](const QString& label, std::function<void()> fn) {
        labels.append(label);
        actions.push_back(std::move(fn));
    };
    add(QStringLiteral("‹ Back to Game menu"), [this] { OpenMenu(); });
#ifndef READER_NO_EXPLAIN
    add(QStringLiteral("Explain this line  ·  Ctrl+E"), [trigger_reader] { trigger_reader("reader_explain"); });
#endif
    add(QStringLiteral("Backlog  ·  Ctrl+H"), [trigger_reader] { trigger_reader("reader_backlog"); });
    add(QStringLiteral("Add sentence card  ·  Ctrl+D"), [trigger_reader] { trigger_reader("reader_sentence_card"); });
    add(QStringLiteral("Undo last card  ·  Ctrl+Z"), [trigger_reader] { trigger_reader("reader_undo_card"); });
#ifndef READER_NO_SAVE_SYNC
    if (!Home::Services::Of(QStringLiteral("furochibi")).isEmpty()) {
        add(QStringLiteral("Toggle guide step"), [this] { ToggleGuide(); });
    }
#endif
    auto& session = ReaderUi::ReaderSession::Instance();
    const bool click_on = session.Settings().ScreenText();
    add(QStringLiteral("Click to look up  ·  %1").arg(click_on ? QStringLiteral("ON") : QStringLiteral("OFF")), [this] {
        auto& s = ReaderUi::ReaderSession::Instance();
        s.Settings().SetValue(ReaderApp::ReaderSettings::KeyScreenText, !s.Settings().ScreenText());
        s.Settings().Sync();
        s.OnSettingsChanged();
        OpenReadingMenu();
    });
    if (!playing_id.isEmpty() && !HasScript(playing_id)) {
        add(QStringLiteral("Read text now (OCR)  ·  Ctrl+Shift+R"), [this] { ReadAskedBoxes(); });
    }
    add(QStringLiteral("Statistics"), [trigger_reader] { trigger_reader("reader_statistics"); });
    add(QStringLiteral("Dictionaries"), [this] {
        ShowSettingsPages();
        home->ShowDictionaries();
    });
    add(QStringLiteral("Reload game script"), [trigger_reader] { trigger_reader("reader_reload_profile"); });
#ifndef READER_NO_SAVE_SYNC
    add(QStringLiteral("Save sync…"), [this] { OpenSaveSync(); });
#endif
    OpenPickerOverlay(QStringLiteral("Reading & Tools"), labels, 1, [actions = std::move(actions)](int index) {
        if (index >= 0 && index < static_cast<int>(actions.size())) {
            actions[index]();
        }
    });
}

void MainWindow::OpenMenu() {
    if (game_picker != nullptr && game_picker->isVisible()) {
        CloseGamePicker();
        ReaderUi::ProgramPaused(false);
        screen->setFocus();
        return;
    }
    if (current == nullptr) {
        return;
    }
    QStringList labels;
    std::vector<std::function<void()>> actions;
    const auto add = [&](const QString& label, std::function<void()> fn) {
        labels.append(label);
        actions.push_back(std::move(fn));
    };
    const uint32_t features = current->session->Features();
    add(QStringLiteral("Resume"), [] {});
    add(QStringLiteral("Reading & Tools  ·  F1"), [this] { OpenReadingMenu(); });
    if ((features & READER_CORE_HAS_STATES) != 0) {
        add(QStringLiteral("Save state"), [this] { OpenStatePicker(false); });
        add(QStringLiteral("Load state"), [this] { OpenStatePicker(true); });
    }
    add(QStringLiteral("Full screen  ·  %1").arg(isFullScreen() ? QStringLiteral("ON") : QStringLiteral("OFF")), [this] {
        ToggleFullScreen();
    });
    add(QStringLiteral("Settings  ·  F2"), [this] { ShowSettingsPages(); });
    add(QStringLiteral("Quit game"), [this] {
        OpenPickerOverlay(QStringLiteral("Quit game?"), {QStringLiteral("Cancel"), QStringLiteral("Quit to Library")}, 0, [this](int idx) {
            if (idx == 1) {
                ShowGame(false);
            }
        });
    });
    OpenPickerOverlay(QStringLiteral("Game menu"), labels, 0, [actions = std::move(actions)](int index) {
        if (index >= 0 && index < static_cast<int>(actions.size())) {
            actions[index]();
        }
    });
    return;
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
#ifndef READER_NO_SAVE_SYNC
        // The game's playthrough guide where it stands, ticked here (guide_window.h): where the
        // device knows Furochibi's service.
        QAction* guide = menu->addAction(QStringLiteral("Guide…"));
        guide->setObjectName(QStringLiteral("guide"));
        connect(guide, &QAction::triggered, this, [this] {
            const QString service = Home::Services::Of(QStringLiteral("furochibi"));
            if (service.isEmpty() || current == nullptr) {
                return;
            }
            ReaderUi::ProgramPaused(true);
            GuideWindow window(this, service, playing_id);
            window.exec();
            ReaderUi::ProgramPaused(false);
            screen->setFocus();
        });
        QAction* guide_over = menu->addAction(QStringLiteral("Show next guide step"));
        guide_over->setObjectName(QStringLiteral("guide-over"));
        connect(guide_over, &QAction::triggered, this, [this] { ToggleGuide(); });
#endif
        // A game no script reads: read by OCR from the core's own picture (core_ocr.h), the
        // game's own switch, and where its text is.
        QAction* by_ocr = menu->addAction(QStringLiteral("Use OCR for this game"));
        by_ocr->setObjectName(QStringLiteral("ocr-on"));
        by_ocr->setCheckable(true);
        connect(by_ocr, &QAction::triggered, this, [this](bool on) { SetCoreOcrOn(on); });
        QAction* ocr_area = menu->addAction(QStringLiteral("Set text area…"));
        ocr_area->setObjectName(QStringLiteral("ocr-area"));
        // Not before the menu has closed and let the game go on: the area step stops it itself.
        connect(ocr_area, &QAction::triggered, this, [this] { QTimer::singleShot(0, this, [this] { AskCoreArea(); }); });
        QAction* ocr_read = menu->addAction(QStringLiteral("Read text now"));
        ocr_read->setObjectName(QStringLiteral("ocr-read"));
        ocr_read->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
        connect(ocr_read, &QAction::triggered, this, [this] { ReadAskedBoxes(); });
        QAction* folder = menu->addAction(QStringLiteral("Open save folder"));
        connect(folder, &QAction::triggered, this, [this] { OpenSaveFolder(); });
        // The page of the game's add-on's companion (addons.h), in the browser.
        QAction* companion_page = menu->addAction(QStringLiteral("Open companion"));
        companion_page->setObjectName(QStringLiteral("companion"));
        connect(companion_page, &QAction::triggered, this, [this] {
            if (const Addon* addon = PlayingCompanion()) {
                QDesktopServices::openUrl(QUrl(addon->url));
            }
        });
        menu->addSeparator();
        QAction* quit = menu->addAction(QStringLiteral("Quit game"));
        connect(quit, &QAction::triggered, this, [this] { EndGame(); });
        // Out of the program altogether, the game stopped as on closing the window (its last
        // save sent): where a front end such as EmulationStation started the game, the way
        // back to it from the controller.
        QAction* exit = menu->addAction(QStringLiteral("Quit Furoshiki\tCtrl+Q"));
        connect(exit, &QAction::triggered, this, [this] {
            qInfo("quitting: Quit Furoshiki in the menu");
            close();
        });
        connect(menu, &QMenu::aboutToShow, this, [this, discs, ports, patches] {
            // What the running core cannot do is not offered.
            const uint32_t features = current != nullptr ? current->session->Features() : 0;
            discs->menuAction()->setVisible((features & READER_CORE_HAS_DISCS) != 0);
            ports->menuAction()->setVisible(current != nullptr && (PortCount(*current->core.info) > 1 ||
                                                                   !Devices(*current->core.info).isEmpty()));
            patches->menuAction()->setVisible((features & (READER_CORE_HAS_PATCHES | READER_CORE_HAS_CODES)) != 0);
            if (auto* entry = menu->findChild<QAction*>(QStringLiteral("guide"))) {
                entry->setVisible(!Home::Services::Of(QStringLiteral("furochibi")).isEmpty());
            }
            if (auto* entry = menu->findChild<QAction*>(QStringLiteral("guide-over"))) {
                entry->setVisible(!Home::Services::Of(QStringLiteral("furochibi")).isEmpty());
            }
            {
                const bool can = !playing_id.isEmpty() && !HasScript(playing_id);
                const Lines::Sources sources = Lines::Load(SharedFolder());
                const Lines::CoreGame* read = Lines::GameOf(sources, playing_id);
                auto* on = menu->findChild<QAction*>(QStringLiteral("ocr-on"));
                on->setVisible(can);
                on->setChecked(read != nullptr && read->on);
                menu->findChild<QAction*>(QStringLiteral("ocr-area"))->setVisible(can && read != nullptr && read->on);
                menu->findChild<QAction*>(QStringLiteral("ocr-read"))->setVisible(
                    can && read != nullptr && read->on && std::any_of(read->more.cbegin(), read->more.cend(), [](const Lines::Box& box) { return box.ask; }));
            }
            if (auto* page = menu->findChild<QAction*>(QStringLiteral("companion"))) {
                const Addon* addon = PlayingCompanion();
                page->setVisible(addon != nullptr);
                if (addon != nullptr) {
                    page->setText(QStringLiteral("Open %1 companion").arg(addon->name));
                }
            }
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
            text += QStringLiteral(" · ") +
                    QDateTime::fromMSecsSinceEpoch(info->saved_ms).toString(QStringLiteral("yyyy/MM/dd HH:mm"));
        } else if (info) {
            text += QStringLiteral(" · Empty");
        }
        if (slot == state_slot) {
            // The slot the quick keys use: F5 saves to it, F8 loads it (the Keys page says so).
            text += load ? QStringLiteral(" · Quick load (F8)") : QStringLiteral(" · Quick save (F5)");
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
        ReaderUi::Notify(QStringLiteral("Couldn't save to slot %1").arg(slot));
    } else if ((current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Saved to slot %1").arg(slot)); // a core that does not answer
    }
}

void MainWindow::LoadStateFrom(int slot) {
    if (current == nullptr || !current->session->Running() || slot < 1 || slot > StateSlots(*current->core.info)) {
        return;
    }
    state_slot = slot;
    if (!current->session->LoadState(slot) && (current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Couldn't load slot %1").arg(slot));
    } else if ((current->session->Features() & READER_CORE_HAS_STATES) == 0) {
        ReaderUi::Notify(QStringLiteral("Loaded slot %1").arg(slot));
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
                ReaderUi::Notify(QStringLiteral("Couldn't change disc"));
            } else if (i < static_cast<uint32_t>(playing_discs.size())) {
                // The game starts on this disc next time.
                QSettings().setValue(QStringLiteral("discs/") + Discs::GroupKey(playing_discs[i]), playing_discs[i]);
            }
        });
    }
    into->addSeparator();
    QAction* open = into->addAction(QStringLiteral("Open disc tray"));
    open->setEnabled(now >= 0);
    connect(open, &QAction::triggered, this, [this] {
        if (current != nullptr) {
            current->session->InsertDisc(-1);
        }
    });
    QAction* add = into->addAction(QStringLiteral("Choose disc file…"));
    connect(add, &QAction::triggered, this, [this] {
        if (current == nullptr) {
            return;
        }
        QStringList types;
        for (uint32_t i = 0; i < current->core.info->file_type_count; ++i) {
            types.append(QStringLiteral("*.") + QString::fromUtf8(current->core.info->file_types[i]));
        }
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose disc"), QFileInfo(playing).path(),
                                                          QStringLiteral("Discs (%1)").arg(types.join(QLatin1Char(' '))));
        if (file.isEmpty() || current == nullptr) {
            return;
        }
        const int32_t index = current->session->AddDisc(QDir::toNativeSeparators(file).toStdString());
        if (index < 0 || !current->session->InsertDisc(index)) {
            ReaderUi::Notify(QStringLiteral("Couldn't insert disc"));
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
            who = who.isEmpty() ? QStringLiteral("keyboard") : who + QStringLiteral(" + keyboard");
        }
        const QString title = QStringLiteral("Port %1: %2").arg(port + 1).arg(who.isEmpty() ? QStringLiteral("None") : who);
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
                    ReaderUi::Notify(QStringLiteral("Port %1 changes at next start").arg(port + 1));
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
        into->addAction(QStringLiteral("Game not identified yet"))->setEnabled(false);
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
        QStringLiteral("Codes for %1, one per line:").arg(game),
        play_settings.Raw(setting, key, game), &ok);
    ReaderUi::ProgramPaused(false);
    screen->setFocus();
    if (!ok || current == nullptr) {
        return;
    }
    play_settings.Set(setting, codes.trimmed(), key, game);
    play_settings.Save();
    ApplyPlaySettings(game);
    ReaderUi::Notify(codes.trimmed().isEmpty() ? QStringLiteral("No cheat codes") : QStringLiteral("Cheat codes applied"));
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
        ReaderUi::Notify(QStringLiteral("Couldn't open the save folder. It is %1").arg(QDir::toNativeSeparators(folder)));
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
    if (name == QStringLiteral("quit")) {
        // quit: out of the program, as Quit Furoshiki in its menu (the game stopped first).
        qInfo("quitting: the control channel's quit");
        reply(QStringLiteral("OK"));
        QTimer::singleShot(0, this, [this] { close(); });
        return true;
    }
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
    if (name == QStringLiteral("read")) {
        // read WHAT: reading with the d-pad without a controller (ReadCommand says the words).
        reply(QStringLiteral("OK ") + ReadCommand(parts.value(1)));
        return true;
    }
    if (name == QStringLiteral("ocr")) {
        // The playing game read by OCR: ocr (what it does); ocr area X Y W H [X Y W H ask|always]...
        // (its boxes, without the page that draws them); ocr on | off; ocr ask (that page);
        // ocr read (the boxes read on request); ocr add CONSOLE|- PATH.
        const QString what = parts.value(1);
        if (what == QStringLiteral("area") && parts.size() >= 6 && current != nullptr && !playing_id.isEmpty()) {
            Lines::Sources sources = Lines::Load(SharedFolder());
            Lines::CoreGame game = PlayingCoreGame();
            game.area = QRectF(parts[2].toDouble(), parts[3].toDouble(), parts[4].toDouble(), parts[5].toDouble());
            game.more.clear();
            for (int i = 6; i + 4 < parts.size(); i += 5) {
                game.more.append({QRectF(parts[i].toDouble(), parts[i + 1].toDouble(), parts[i + 2].toDouble(), parts[i + 3].toDouble()), parts[i + 4] == QStringLiteral("ask")});
            }
            game.on = true;
            Lines::SetGame(sources, game);
            Lines::Save(SharedFolder(), sources);
            ShelveOcrGames();
            StartCoreOcr();
        } else if (what == QStringLiteral("on") || what == QStringLiteral("off")) {
            SetCoreOcrOn(what == QStringLiteral("on"));
        } else if (what == QStringLiteral("ask")) {
            AskCoreArea();
        } else if (what == QStringLiteral("read")) {
            ReadAskedBoxes();
        } else if (what == QStringLiteral("add")) {
            AddCoreOcrGame(parts.value(2) == QStringLiteral("-") ? QString() : parts.value(2), QStringList(parts.mid(3)).join(QLatin1Char(' ')));
        }
        const Lines::Sources sources = Lines::Load(SharedFolder());
        const Lines::CoreGame* game = Lines::GameOf(sources, playing_id);
        const auto& session = ReaderUi::ReaderSession::Instance();
        reply(QStringLiteral("OK game %1 | %2 | %3 | pictures looked at %4, sent %5, taken %6 | lines %7, %8 | on screen %9 | session %10")
                  .arg(playing_id.isEmpty() ? QStringLiteral("none") : playing_id,
                       game == nullptr ? QStringLiteral("not read by OCR") : !game->on ? QStringLiteral("off") : game->area.isValid() ? QStringLiteral("on") : QStringLiteral("no area yet"),
                       core_ocr != nullptr && core_ocr->Running() ? core_ocr->Status() : QStringLiteral("not running"))
                  .arg(core_ocr != nullptr ? core_ocr->Gate().Seen() : 0)
                  .arg(core_ocr != nullptr ? core_ocr->Client().Sent() : 0)
                  .arg(core_ocr != nullptr ? core_ocr->Client().Taken() : 0)
                  .arg(core_ocr != nullptr ? core_ocr->Handed() : 0)
                  .arg(core_ocr != nullptr && core_ocr->Placed() ? QStringLiteral("placed") : QStringLiteral("not placed"))
                  .arg(session.OnScreen().size())
                  .arg(session.Current() ? session.Current()->line.text : QStringLiteral("-")));
        return true;
    }
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
    if (name == QStringLiteral("pad-hold")) {
        // pad-hold N [N...]: the controller's buttons N (SDL's numbers) held, as the Steam Controller
        // would say it; -1 lets all go. For checking what the console is then handed (pad-sent).
        steam_buttons = 0;
        for (const QString& part : parts.mid(1)) {
            const int button = part.toInt();
            steam_buttons |= button >= 0 && button < 32 ? 1U << button : 0;
        }
        steam_seen = true;
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
