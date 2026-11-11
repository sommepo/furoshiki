// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A key the program hears while another program's window has the keyboard (Windows:
// RegisterHotKey): a game read by OCR is in front, and the key that reads its on-request boxes
// is pressed there. Elsewhere there is none (Set says so), and the strip's own key does it.

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace Home {

class GlobalKey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalKey(QObject* parent = nullptr);
    ~GlobalKey() override;

    /// Ctrl+Shift+`letter` from now on (a capital letter or a digit); false where the system
    /// gives none, or another program has it. Clear lets it go.
    bool Set(char letter);
    void Clear();
    [[nodiscard]] bool Held() const {
        return held;
    }

    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

signals:
    void Pressed();

private:
    bool held{};
    int id{};
};

} // namespace Home
