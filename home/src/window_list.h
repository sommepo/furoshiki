// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The windows open on this computer, and a picture of one (docs/furoshiki-3.md, "A window read
// by OCR"): what the wizard shows to choose from, and what is sent to the OCR helper four times
// a second while a title is read from its window.
//
// On Windows: the visible top-level windows that have a title (EnumWindows), each pictured by
// PrintWindow, which also gives a window that lies under another. Elsewhere, and in tests, a
// stand-in list: the program's own window (when one was named with SetOwn), and each picture
// file named in READER_HOME_FAKE_WINDOWS (a list as PATH is one; "Name=file" gives it a title,
// else the file's own name is its title), read again at every picture, so that a test may
// change the file as a game changes its screen.

#include <functional>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

namespace Home::Windows {

struct Entry {
    QString title;
    /// What the system knows it by (a HWND on Windows); 0 for a stand-in.
    quintptr handle{};
    /// A stand-in's picture file; "own" for the program's own window.
    QString file;
};

/// The windows there are now. The program's own comes last, the others in the system's order
/// (the one in front first).
[[nodiscard]] QList<Entry> List();
/// The window of that title now: the same one if it is still there (its handle), else the
/// first of that exact title. Nothing (an empty title) when it is not open.
[[nodiscard]] Entry Find(const QString& title, quintptr handle = 0);
/// Its picture as it is now (its inside, without the frame); null when it cannot be had
/// (closed, minimized).
[[nodiscard]] QImage Grab(const Entry& window);
/// Where its picture is on the screen, in Qt's own coordinates; empty where that is not known
/// (a stand-in, unless READER_HOME_FAKE_WINDOW_PLACE says "x,y,w,h").
[[nodiscard]] QRect Place(const Entry& window);
/// The program's own window, for the stand-in list: its title, and how it is pictured.
void SetOwn(const QString& title, std::function<QImage()> grab);

} // namespace Home::Windows
