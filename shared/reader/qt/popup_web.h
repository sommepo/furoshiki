// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace ReaderUi {

/**
 * The dictionary popup's page (shared/popup/, made into popup_page.inc by tools/popup.py) in a
 * web engine: Qt WebEngine on Linux, the system's WebView2 on Windows. The popup (LookupPopup)
 * calls the page's script (Call: "Reader.show(...)" and so on) and hears what the page says
 * (Message: its size, its buttons, a press). Calls made before the page is ready wait for it.
 *
 * Only Furoshiki builds the engines (READER_WEB_ENGINE, READER_WEB_VIEW2); in any other build,
 * and where the engine is missing (no WebView2 runtime), Create gives nothing and the popup
 * draws itself as before.
 */
class PopupWeb : public QWidget {
    Q_OBJECT

public:
    /// The engine this build and this system have, or nothing. `data_folder` is where an
    /// engine keeps its own files (WebView2's profile).
    static PopupWeb* Create(QWidget* parent, const QString& data_folder);

    /// Runs `script` in the page once it is ready.
    void Call(const QString& script);
    [[nodiscard]] bool IsReady() const {
        return ready;
    }

signals:
    /// What the page said: {"type": "size" | "add" | "book" | "dictionaries" | "ready", ...},
    /// or {"type": "failed"} when the engine could not be started.
    void Message(const QJsonObject& message);

protected:
    using QWidget::QWidget;
    /// Runs `script` in the page now.
    virtual void Run(const QString& script) = 0;
    /// A message from the page, as the JSON text it posted.
    void Received(const QString& text);

private:
    bool ready{};
    QStringList waiting;
};

} // namespace ReaderUi
