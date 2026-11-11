// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "window_list.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QPixmap>
#include <QScreen>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Home::Windows {

namespace {
QString own_title;
std::function<QImage()> own_grab;

QList<Entry> Fakes() {
    QList<Entry> found;
    const QString said = qEnvironmentVariable("READER_HOME_FAKE_WINDOWS");
    for (const QString& part : said.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        const int equals = static_cast<int>(part.indexOf(QLatin1Char('=')));
        // "C:\..." has no name before its colon; a name has no slash in it.
        const bool named = equals > 0 && !part.left(equals).contains(QLatin1Char('/')) && !part.left(equals).contains(QLatin1Char('\\'));
        const QString file = named ? part.mid(equals + 1) : part;
        found.append({named ? part.left(equals) : QFileInfo(file).completeBaseName(), 0, file});
    }
    return found;
}

#ifdef _WIN32
BOOL CALLBACK Each(HWND window, LPARAM into) {
    if (!IsWindowVisible(window) || IsIconic(window) || GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    // A tool window (a palette, a tray's own) is no game's window.
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    wchar_t text[512];
    const int length = GetWindowTextW(window, text, 512);
    RECT inside{};
    if (length <= 0 || !GetClientRect(window, &inside) || inside.right < 64 || inside.bottom < 64) {
        return TRUE;
    }
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) {
        return TRUE; // the program's own windows are not read
    }
    static_cast<QList<Entry>*>(reinterpret_cast<void*>(into))->append(
        {QString::fromWCharArray(text, length), reinterpret_cast<quintptr>(window), QString()});
    return TRUE;
}

QImage GrabWindow(HWND window) {
    RECT inside{};
    if (!IsWindow(window) || IsIconic(window) || !GetClientRect(window, &inside) || inside.right <= 0 || inside.bottom <= 0) {
        return {};
    }
    const int width = inside.right, height = inside.bottom;
    QImage picture(width, height, QImage::Format_RGB32);
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top down, as a QImage is
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool drawn = false;
    if (bitmap != nullptr && bits != nullptr) {
        HGDIOBJ before = SelectObject(memory, bitmap);
        // Its inside only (1), as the compositor has it (2: what Direct3D drew too, Windows 8.1 on).
        drawn = PrintWindow(window, memory, 1 | 2) != 0;
        if (!drawn) {
            // Else what is on the screen where it is (wrong while another window lies over it).
            HDC own = GetDC(window);
            drawn = BitBlt(memory, 0, 0, width, height, own, 0, 0, SRCCOPY) != 0;
            ReleaseDC(window, own);
        }
        GdiFlush();
        if (drawn) {
            for (int y = 0; y < height; ++y) {
                memcpy(picture.scanLine(y), static_cast<const uchar*>(bits) + static_cast<qsizetype>(y) * width * 4,
                       static_cast<size_t>(width) * 4);
            }
        }
        SelectObject(memory, before);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return drawn ? picture : QImage();
}
#endif

} // namespace

void SetOwn(const QString& title, std::function<QImage()> grab) {
    own_title = title;
    own_grab = std::move(grab);
}

QList<Entry> List() {
    QList<Entry> found;
#ifdef _WIN32
    EnumWindows(Each, reinterpret_cast<LPARAM>(static_cast<void*>(&found)));
#endif
    found.append(Fakes());
#ifndef _WIN32
    if (own_grab) {
        found.append({own_title, 0, QStringLiteral("own")});
    }
#endif
    return found;
}

Entry Find(const QString& title, quintptr handle) {
    const QList<Entry> all = List();
    for (const Entry& entry : all) {
        if (handle != 0 && entry.handle == handle) {
            return entry;
        }
    }
    for (const Entry& entry : all) {
        if (entry.title == title) {
            return entry;
        }
    }
    return {};
}

QImage Grab(const Entry& window) {
    if (window.file == QStringLiteral("own")) {
        return own_grab ? own_grab() : QImage();
    }
    if (!window.file.isEmpty()) {
        // Read anew each time: a test writes the file again as a game draws its screen.
        QImageReader reader(window.file);
        reader.setAutoTransform(true);
        return reader.read().convertToFormat(QImage::Format_RGB32);
    }
#ifdef _WIN32
    return GrabWindow(reinterpret_cast<HWND>(window.handle));
#else
    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr && window.handle != 0) {
        return screen->grabWindow(static_cast<WId>(window.handle)).toImage();
    }
    return {};
#endif
}

QRect Place(const Entry& window) {
    if (!window.file.isEmpty()) {
        const QStringList parts = qEnvironmentVariable("READER_HOME_FAKE_WINDOW_PLACE").split(QLatin1Char(','));
        return parts.size() == 4 ? QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt()) : QRect();
    }
#ifdef _WIN32
    const HWND handle = reinterpret_cast<HWND>(window.handle);
    RECT inside{};
    POINT corner{0, 0};
    if (!IsWindow(handle) || IsIconic(handle) || !GetClientRect(handle, &inside) || !ClientToScreen(handle, &corner)) {
        return {};
    }
    // The system's pixels to Qt's: by the scale of the screen the window's middle is on.
    const QPoint middle(corner.x + inside.right / 2, corner.y + inside.bottom / 2);
    for (const QScreen* screen : QGuiApplication::screens()) {
        const double scale = screen->devicePixelRatio();
        const QRect area = screen->geometry();
        const QRect native(area.x(), area.y(), static_cast<int>(area.width() * scale), static_cast<int>(area.height() * scale));
        if (native.contains(middle)) {
            return QRect(area.x() + static_cast<int>((corner.x - area.x()) / scale), area.y() + static_cast<int>((corner.y - area.y()) / scale),
                         static_cast<int>(inside.right / scale), static_cast<int>(inside.bottom / scale));
        }
    }
    return QRect(corner.x, corner.y, inside.right, inside.bottom);
#else
    return {};
#endif
}

} // namespace Home::Windows
