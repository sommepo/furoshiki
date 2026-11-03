// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// What the desktop reader's windows need of the emulator, in one place. The windows were
// written for another emulator's desktop app (the Eden Reader fork); this header gives them
// the few things they ask of their surroundings in the shape they ask for them.

#include <format>
#include <optional>
#include <functional>
#include <string>
#include <QByteArray>
#include <QStringList>
#include <QUrl>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QWidget>

class QObject;

namespace ReaderUi {

enum class HostLog { Info, Warning, Error };
/// A line for the emulator's log.
void WriteLog(HostLog level, const std::string& message);

/// For the development control channel: a command of the console's own (reader_control.cpp
/// asks here first). True when it was one; `reply` is then called, at once or later
/// (`context` keeps a timer alive). In host_commands.cpp.
bool HostCommand(const QString& name, const QStringList& parts, const QString& rest,
                 const std::function<void(const QString&)>& reply, QObject* context);
/// Fetches a file from the network (the script list and its scripts) as the emulator can: the
/// caller's window waits, the app goes on drawing. Nothing, and `error` set, when it cannot.
std::optional<QByteArray> HostFetch(const QUrl& url, QString* error);

/// The folder the reader keeps its own files in (dictionaries, history, settings, profiles
/// of the user's own): "reader" in the emulator's folder for the PSP's memory stick.
[[nodiscard]] QString ReaderDirectory();

/**
 * Runs something on the main thread soon (within a few milliseconds), in the order asked.
 * **This is how the emulator's own threads reach the reader's Qt side**: the loader, the
 * CPU and the audio threads are not Qt's, and on Windows a thread that has used Qt objects
 * and then ends takes the program down with it (the first Windows package froze at every
 * game start that way). Nothing of Qt is touched by the call itself.
 */
void PostToGui(std::function<void()> task);
/// Where short-lived files go (a card's picture and sound on their way to Anki).
[[nodiscard]] QString CacheDirectory();
/// The app's own file of reader settings: what stays the app's when the rest are shared in
/// Reader Home's folder (ReaderSettings). Here "reader.ini" in ReaderDirectory().
[[nodiscard]] QString ReaderSettingsFile();

/**
 * Who gets the pointer first: where the game shows text of its own, a click is the reader's.
 * Positions are those of the widget the game is drawn in.
 */
class ReaderPointerFilter {
public:
    virtual ~ReaderPointerFilter() = default;
    /// The pointer went down. True when the reader takes it: the game does not hear of it.
    virtual bool OnPointerPress(const QPointF& position, bool left_button) = 0;
    virtual void OnPointerMove(const QPointF& position) = 0;
    /// The pointer of a press the reader took went up.
    virtual void OnPointerRelease(const QPointF& position) = 0;
};

/// Where the pointer is on the screen. The system's answer, unless the development channel
/// has said where it is: on a screen without a pointer device there is nobody to ask, and
/// moving the system's pointer there takes the X server down.
[[nodiscard]] QPoint PointerPosition();
void SetPointerPositionForTests(const QPoint& global);

/// For the development control channel: the emulator's own picture (its screens too, which
/// the capture of a game's frame does not have) is written to `path` after the next frame.
void RequestSurfaceGrab(const QString& path);

/// What the emulator's window does for the reader.
struct EmulatorHost {
    /// A game is running (not paused, not behind the emulator's own menus).
    std::function<bool()> is_running;
    /// A game is loaded, running or paused.
    std::function<bool()> is_powered_on;
    std::function<void()> pause;
    std::function<void()> resume;
    /// Where the game's picture is in the widget it is drawn in, in the widget's own units.
    std::function<QRectF()> picture_rect;
    /// Where one of the console's screens is drawn (0 the upper, 1 the lower), for a console
    /// with two (console.h, LowerScreenWidth); nothing for a screen that is not shown. May be
    /// empty: the one screen is picture_rect.
    std::function<QRectF(int screen)> screen_rect;
    /// Asks for the game's picture as it is now; the answer (a null image when there is
    /// none) comes on the main thread, a frame or so later.
    std::function<void(std::function<void(const QImage&)>)> grab_picture;
    /// The window the game is drawn in as it is drawn now, both screens as it shows them, for
    /// the development channel's pictures; a null image when it cannot be had (a graphics
    /// card draws past the window's own picture). May be empty: the game's picture
    /// (grab_picture) is drawn where picture_rect says.
    std::function<QImage()> window_picture;
    /// Sets who gets the pointer first, or nobody.
    std::function<void(ReaderPointerFilter*)> set_pointer_filter;
    /// The emulator's window does not tell of a mouse that moves without a button held (or
    /// does only under some settings of its own): the reader then looks where the pointer is
    /// itself, a few times a second, so that a word under a resting pointer is looked up.
    bool watch_pointer{};
    /// The reader's settings were changed in its window: for what the app keeps of its own
    /// beside them (its save sync, say). May be empty.
    std::function<void()> settings_changed;
};

} // namespace ReaderUi

// The windows log through these, with std::format's placeholders.
#define LOG_INFO(category, ...) ::ReaderUi::WriteLog(::ReaderUi::HostLog::Info, std::format(__VA_ARGS__))
#define LOG_WARNING(category, ...) ::ReaderUi::WriteLog(::ReaderUi::HostLog::Warning, std::format(__VA_ARGS__))
#define LOG_ERROR(category, ...) ::ReaderUi::WriteLog(::ReaderUi::HostLog::Error, std::format(__VA_ARGS__))
