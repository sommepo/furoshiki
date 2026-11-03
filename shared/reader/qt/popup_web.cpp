// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>
#include <QJsonDocument>
#include "reader/qt/popup_web.h"

namespace ReaderUi {

#if defined(READER_WEB_ENGINE)
PopupWeb* MakeWebEngine(QWidget* parent);
#endif
#if defined(READER_WEB_VIEW2) && defined(_WIN32)
PopupWeb* MakeWebView2(QWidget* parent, const QString& data_folder);
#endif

PopupWeb* PopupWeb::Create(QWidget* parent, const QString& data_folder) {
    Q_UNUSED(parent);
    Q_UNUSED(data_folder);
#if defined(READER_WEB_ENGINE)
    return MakeWebEngine(parent);
#elif defined(READER_WEB_VIEW2) && defined(_WIN32)
    return MakeWebView2(parent, data_folder);
#else
    return nullptr;
#endif
}

void PopupWeb::Call(const QString& script) {
    if (ready) {
        Run(script);
    } else {
        waiting.append(script);
    }
}

void PopupWeb::Received(const QString& text) {
    const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
    if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ready") && !ready) {
        ready = true;
        const QStringList calls = std::exchange(waiting, {});
        for (const QString& call : calls) {
            Run(call);
        }
    }
    emit Message(message);
}

} // namespace ReaderUi
