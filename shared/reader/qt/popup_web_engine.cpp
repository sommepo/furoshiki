// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The popup's page in Qt WebEngine (Linux). The page tells the popup what it has to say by
// writing it to its console ("reader:" and the message's JSON), which the page here reads.

#include <functional>
#include <utility>
#include <QResizeEvent>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include "reader/qt/popup_web.h"

namespace ReaderUi {

namespace {

#include "reader/qt/popup_page.inc"

class ConsolePage : public QWebEnginePage {
public:
    ConsolePage(QObject* parent, std::function<void(const QString&)> heard_)
        : QWebEnginePage(parent), heard{std::move(heard_)} {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString& message, int,
                                  const QString&) override {
        if (message.startsWith(QStringLiteral("reader:"))) {
            heard(message.mid(7));
        }
    }
    // A link in a dictionary's text does not take the popup away.
    bool acceptNavigationRequest(const QUrl&, NavigationType type, bool) override {
        return type == NavigationTypeTyped || type == NavigationTypeOther;
    }

private:
    std::function<void(const QString&)> heard;
};

class WebEnginePopup : public PopupWeb {
public:
    explicit WebEnginePopup(QWidget* parent) : PopupWeb(parent), view{new QWebEngineView(this)} {
        auto* page = new ConsolePage(view, [this](const QString& text) { Received(text); });
        page->setBackgroundColor(Qt::transparent);
        page->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
        view->setPage(page);
        view->setContextMenuPolicy(Qt::NoContextMenu);
        // Never the keyboard: the game keeps it.
        view->setFocusPolicy(Qt::NoFocus);
        setFocusPolicy(Qt::NoFocus);
        page->setHtml(QString::fromUtf8(PopupPage), QUrl(QStringLiteral("https://popup.reader/")));
    }

protected:
    void Run(const QString& script) override {
        view->page()->runJavaScript(script);
    }
    void resizeEvent(QResizeEvent* event) override {
        view->setGeometry(QRect(QPoint(0, 0), event->size()));
    }

private:
    QWebEngineView* view;
};

} // namespace

PopupWeb* MakeWebEngine(QWidget* parent) {
    // Drawn without the GPU: a popup is small, the game has the GPU, and without GL (a machine
    // whose drivers WebEngine cannot use) its GPU process would take the whole program down.
    // Read when the first page is made; whoever sets the flags has the last word.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --disable-gpu-compositing");
    }
    return new WebEnginePopup(parent);
}

} // namespace ReaderUi
