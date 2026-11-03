// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The popup's page in WebView2, the Edge engine Windows 10 and 11 carry (Windows). The page
// tells the popup what it has to say with chrome.webview.postMessage. WebView2Loader.dll, which
// finds the installed engine, ships beside the program (Microsoft's WebView2 SDK, BSD licence)
// and is loaded when the first popup is made: without it, or without the engine, there is no
// web popup and the popup draws itself.

#if defined(_WIN32)

#include <atomic>
#include <functional>
#include <utility>
#include <QDir>
#include <QResizeEvent>
#include <windows.h>
#include "WebView2.h"
#include "reader/qt/popup_web.h"

namespace ReaderUi {

namespace {

#include "reader/qt/popup_page.inc"

/// A COM object answering one of WebView2's callbacks with a function.
template <typename Interface, typename... Args>
class Callback final : public Interface {
public:
    explicit Callback(std::function<HRESULT(Args...)> call_) : call{std::move(call_)} {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, Iid())) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references;
        if (left == 0) {
            delete this;
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        return call(args...);
    }

private:
    static REFIID Iid();
    std::atomic<ULONG> references{1};
    std::function<HRESULT(Args...)> call;
};

using EnvironmentDone =
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>;
using ControllerDone =
    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>;
using MessageHeard =
    Callback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>;
using ScriptDone = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>;

template <>
REFIID EnvironmentDone::Iid() {
    return IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler;
}
template <>
REFIID ControllerDone::Iid() {
    return IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;
}
template <>
REFIID MessageHeard::Iid() {
    return IID_ICoreWebView2WebMessageReceivedEventHandler;
}
template <>
REFIID Callback<ICoreWebView2FocusChangedEventHandler, ICoreWebView2Controller*, IUnknown*>::Iid() {
    return IID_ICoreWebView2FocusChangedEventHandler;
}
template <>
REFIID ScriptDone::Iid() {
    return IID_ICoreWebView2ExecuteScriptCompletedHandler;
}

using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                   ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

/// The loader's entry point, or nothing where the loader or the engine is missing.
CreateEnvironment Loader() {
    static const CreateEnvironment create = [] {
        HMODULE module = LoadLibraryW(L"WebView2Loader.dll");
        if (!module) {
            return CreateEnvironment{};
        }
        auto* function = GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions");
        return reinterpret_cast<CreateEnvironment>(reinterpret_cast<void*>(function));
    }();
    return create;
}

class WebView2Popup : public PopupWeb {
public:
    WebView2Popup(QWidget* parent, const QString& data_folder) : PopupWeb(parent) {
        // The engine draws into a window of its own inside this one.
        setAttribute(Qt::WA_NativeWindow);
        setFocusPolicy(Qt::NoFocus);
        const HWND window = reinterpret_cast<HWND>(winId());
        QDir().mkpath(data_folder);
        const std::wstring folder = QDir::toNativeSeparators(data_folder).toStdWString();
        Loader()(nullptr, folder.c_str(), nullptr,
                 new EnvironmentDone([this, window](HRESULT result, ICoreWebView2Environment* environment) {
                     if (FAILED(result) || !environment) {
                         Received(QStringLiteral("{\"type\":\"failed\"}"));
                         return result;
                     }
                     return environment->CreateCoreWebView2Controller(
                         window, new ControllerDone([this](HRESULT made, ICoreWebView2Controller* made_controller) {
                             if (SUCCEEDED(made) && made_controller) {
                                 Start(made_controller);
                             } else {
                                 Received(QStringLiteral("{\"type\":\"failed\"}"));
                             }
                             return made;
                         }));
                 }));
    }

    ~WebView2Popup() override {
        if (controller) {
            controller->Close();
            controller->Release();
        }
        if (webview) {
            webview->Release();
        }
    }

protected:
    void Run(const QString& script) override {
        if (webview) {
            const std::wstring text = script.toStdWString();
            webview->ExecuteScript(text.c_str(), nullptr);
        }
    }
    void resizeEvent(QResizeEvent*) override {
        Place();
    }

private:
    void Start(ICoreWebView2Controller* made) {
        controller = made;
        controller->AddRef();
        controller->get_CoreWebView2(&webview);
        // See-through where the page draws nothing: a theme's rounded corners.
        ICoreWebView2Controller2* second = nullptr;
        if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2,
                                                 reinterpret_cast<void**>(&second))) &&
            second) {
            second->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
            second->Release();
        }
        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->Release();
        }
        EventRegistrationToken token{};
        // A click in the page gives it the keyboard; the game keeps the keyboard: it goes back to
        // the window under the popup at once.
        using FocusHeard = Callback<ICoreWebView2FocusChangedEventHandler, ICoreWebView2Controller*, IUnknown*>;
        controller->add_GotFocus(new FocusHeard([this](ICoreWebView2Controller*, IUnknown*) {
                                     QMetaObject::invokeMethod(
                                         this,
                                         [this] {
                                             QWidget* popup = window();
                                             QWidget* owner = popup ? popup->parentWidget() : nullptr;
                                             if (owner) {
                                                 const HWND game = reinterpret_cast<HWND>(owner->window()->winId());
                                                 SetForegroundWindow(game);
                                                 ::SetFocus(game);
                                             }
                                         },
                                         Qt::QueuedConnection);
                                     return S_OK;
                                 }),
                                 &token);
        webview->add_WebMessageReceived(
            new MessageHeard([this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
                LPWSTR text = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text) {
                    Received(QString::fromWCharArray(text));
                    CoTaskMemFree(text);
                }
                return S_OK;
            }),
            &token);
        Place();
        controller->put_IsVisible(TRUE);
        const std::wstring page = QString::fromUtf8(PopupPage).toStdWString();
        webview->NavigateToString(page.c_str());
    }

    void Place() {
        if (!controller) {
            return;
        }
        const qreal ratio = devicePixelRatioF();
        const RECT bounds{0, 0, static_cast<LONG>(width() * ratio), static_cast<LONG>(height() * ratio)};
        controller->put_Bounds(bounds);
    }

    ICoreWebView2Controller* controller{};
    ICoreWebView2* webview{};
};

} // namespace

PopupWeb* MakeWebView2(QWidget* parent, const QString& data_folder) {
    if (!Loader()) {
        return nullptr;
    }
    return new WebView2Popup(parent, data_folder);
}

} // namespace ReaderUi

#endif
