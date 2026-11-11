// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include "global_key.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Home {

GlobalKey::GlobalKey(QObject* parent) : QObject(parent) {
    static int next = 0x4652; // "FR": no other of this process has it
    id = next++;
}

GlobalKey::~GlobalKey() {
    Clear();
}

bool GlobalKey::Set(char letter) {
    Clear();
#ifdef _WIN32
    // No window: the message comes to this thread's queue, where the filter sees it.
    if (RegisterHotKey(nullptr, id, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, static_cast<UINT>(letter)) != 0) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        held = true;
    }
#else
    (void)letter;
#endif
    return held;
}

void GlobalKey::Clear() {
    if (!held) {
        return;
    }
#ifdef _WIN32
    UnregisterHotKey(nullptr, id);
#endif
    QCoreApplication::instance()->removeNativeEventFilter(this);
    held = false;
}

bool GlobalKey::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
#ifdef _WIN32
    if (type == "windows_generic_MSG" || type == "windows_dispatcher_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == id) {
            emit Pressed();
            return true;
        }
    }
#else
    (void)type;
    (void)message;
#endif
    return false;
}

} // namespace Home
