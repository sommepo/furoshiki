// SPDX-FileCopyrightText: Furoshiki contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <QTimer>
#include "steam_pad.h"

#if defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#include <tlhelp32.h>
extern "C" {
#include <hidsdi.h>
}
#endif

namespace Home {

namespace Bridge = ReaderApp::SteamBridge;

Bridge::Tuning TuningOf(const QJsonObject& steam) {
    const auto number = [&](const char* key, double otherwise, double low, double high) {
        return std::clamp(steam.value(QLatin1String(key)).toDouble(otherwise), low, high);
    };
    Bridge::Tuning made;
    made.swipe_pixels *= number("sensitivity", 1.0, 0.2, 4.0);
    made.acceleration = number("acceleration", made.acceleration, 0.0, 3.0);
    made.scroll_notches *= number("scroll", 1.0, 0.2, 4.0);
    made.trackball = steam.value(QStringLiteral("trackball")).toBool(true);
    made.haptics = steam.value(QStringLiteral("haptics")).toBool(true);
    made.tick_micros = static_cast<int>(made.tick_micros * number("tick_strength", 1.0, 0.0, 4.0));
    made.click_micros = static_cast<int>(made.click_micros * number("click_strength", 1.0, 0.0, 4.0));
    return made;
}

namespace {

[[maybe_unused]] constexpr unsigned ValveVendor = 0x28DE;
[[maybe_unused]] constexpr unsigned ValveProducts[] = {0x1302, 0x1304, 0x1305};

/// A button of the controller as a key of the menu; 0 for one the menu has no use for.
int KeyOf(Bridge::Button button) {
    switch (button) {
    case Bridge::A:
    case Bridge::Start:
        return Qt::Key_Return;
    case Bridge::B:
        return Qt::Key_Escape;
    case Bridge::Y:
        return Qt::Key_F5;
    case Bridge::LeftShoulder:
        return Qt::Key_Q;
    case Bridge::RightShoulder:
        return Qt::Key_E;
    case Bridge::DpadUp:
        return Qt::Key_Up;
    case Bridge::DpadDown:
        return Qt::Key_Down;
    case Bridge::DpadLeft:
        return Qt::Key_Left;
    case Bridge::DpadRight:
        return Qt::Key_Right;
    default:
        return 0;
    }
}

} // namespace

struct SteamPad::Private {
    explicit Private(SteamPad* owner_) : owner{owner_} {}

    /// Where the controller's doings go: the menu's keys and the system's mouse. Called from
    /// the thread that reads the controller.
    class Sink final : public Bridge::Sink {
    public:
        explicit Sink(SteamPad* owner_) : owner{owner_} {}
        void PadButton(Bridge::Button button, bool down) override {
            if (owner->d->game_mode.load()) {
                emit owner->ButtonChanged(button, down);
                return;
            }
            const int key = KeyOf(button);
            if (down && key != 0) {
                emit owner->KeyPressed(key);
            }
        }
        void PadAxis(Bridge::Axis axis, int value) override {
            if (owner->d->game_mode.load()) {
                emit owner->AxisChanged(axis, value);
                return;
            }
            // The left stick is the d-pad: over halfway is a press, once.
            if (axis != Bridge::LeftX && axis != Bridge::LeftY) {
                return;
            }
            int& held = axis == Bridge::LeftX ? stick_x : stick_y;
            const int now = value > 18000 ? 1 : value < -18000 ? -1 : (std::abs(value) < 9000 ? 0 : held);
            if (now != held) {
                held = now;
                if (now != 0) {
                    emit owner->KeyPressed(axis == Bridge::LeftX ? (now > 0 ? Qt::Key_Right : Qt::Key_Left)
                                                                 : (now > 0 ? Qt::Key_Down : Qt::Key_Up));
                }
            }
        }
        void MouseMove(int dx, int dy) override {
#if defined(_WIN32)
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dx = dx;
            input.mi.dy = dy;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(input));
#else
            (void)dx;
            (void)dy;
#endif
        }
        void MouseClick(Bridge::MouseButton button, bool down) override {
#if defined(_WIN32)
            INPUT input{};
            input.type = INPUT_MOUSE;
            if (button == Bridge::MouseButton::Left) {
                input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            } else {
                input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            }
            SendInput(1, &input, sizeof(input));
#else
            (void)button;
            (void)down;
#endif
        }
        void MouseWheel(int amount) override {
#if defined(_WIN32)
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = static_cast<DWORD>(amount);
            SendInput(1, &input, sizeof(input));
#else
            (void)amount;
#endif
        }

    private:
        SteamPad* owner;
        int stick_x{}, stick_y{};
    };

    SteamPad* owner;
    Sink sink{owner};
    QTimer scan;
    bool active{};
    std::atomic<bool> game_mode{false};
    std::atomic<int> controllers{0};
    std::atomic<bool> steam_runs{false};
    std::atomic<int> pulses_sent{0}, pulses_failed{0};

    std::mutex tuning_mutex;
    Bridge::Tuning tuning;
    std::atomic<int> tuning_count{0};

    void TakeTuning(Bridge::Pad& pad, int& have) {
        const int now = tuning_count.load();
        if (now != have) {
            have = now;
            std::lock_guard lock{tuning_mutex};
            pad.SetTuning(tuning);
        }
    }

#if defined(_WIN32)
    struct Listener {
        std::thread thread;
        HANDLE device{INVALID_HANDLE_VALUE};
        HANDLE stop{nullptr};
        std::atomic<bool> gone{false};
        DWORD output_length{}; ///< Of an output report; 0 until asked
    };
    std::map<std::wstring, std::unique_ptr<Listener>> listeners; ///< by device path

    static bool SteamRuns() {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return false;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
            if (lstrcmpiW(entry.szExeFile, L"steam.exe") == 0) {
                found = true;
                break;
            }
        }
        CloseHandle(snapshot);
        return found;
    }

    /// Every three seconds: the controllers that came and went. Nothing while Steam runs (it
    /// plays the controller itself) or while Furoshiki is not the window in use.
    void Scan() {
        for (auto it = listeners.begin(); it != listeners.end();) {
            if (it->second->gone.load()) {
                End(*it->second);
                it = listeners.erase(it);
            } else {
                ++it;
            }
        }
        steam_runs.store(SteamRuns());
        if (!active || steam_runs.load()) {
            Stop();
            return;
        }
        GUID hid;
        HidD_GetHidGuid(&hid);
        const HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) {
            return;
        }
        SP_DEVICE_INTERFACE_DATA item{};
        item.cbSize = sizeof(item);
        for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, index, &item); ++index) {
            DWORD needed = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &item, nullptr, 0, &needed, nullptr);
            if (needed == 0) {
                continue;
            }
            std::vector<char> buffer(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &item, detail, needed, nullptr, nullptr)) {
                continue;
            }
            const std::wstring path = detail->DevicePath;
            if (listeners.count(path) != 0) {
                continue;
            }
            const HANDLE device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                              FILE_FLAG_OVERLAPPED, nullptr);
            if (device == INVALID_HANDLE_VALUE) {
                continue;
            }
            HIDD_ATTRIBUTES attributes{};
            attributes.Size = sizeof(attributes);
            bool ours = false;
            if (HidD_GetAttributes(device, &attributes) && attributes.VendorID == ValveVendor) {
                for (const unsigned product : ValveProducts) {
                    ours = ours || attributes.ProductID == product;
                }
            }
            if (!ours) {
                CloseHandle(device);
                continue;
            }
            auto listener = std::make_unique<Listener>();
            listener->device = device;
            listener->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Listener* raw = listener.get();
            listener->thread = std::thread([this, raw] { Listen(*raw); });
            listeners[path] = std::move(listener);
        }
        SetupDiDestroyDeviceInfoList(set);
    }

    /// On the listener's own thread: one of the controller's interfaces. The one that sends
    /// state reports is the controller; the others stay silent and cost nothing.
    void Listen(Listener& listener) {
        Bridge::Pad pad;
        int tuning_have = -1;
        OVERLAPPED pending{};
        pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        unsigned char report[256];
        bool counted = false;
        ULONGLONG lizard_at = 0;
        for (;;) {
            DWORD got = 0;
            ResetEvent(pending.hEvent);
            if (!ReadFile(listener.device, report, sizeof(report), &got, &pending)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    break; // the controller is gone
                }
                const HANDLE either[] = {pending.hEvent, listener.stop};
                if (WaitForMultipleObjects(2, either, FALSE, INFINITE) != WAIT_OBJECT_0) {
                    CancelIo(listener.device);
                    break; // Furoshiki is ending, or gave the controller up
                }
                if (!GetOverlappedResult(listener.device, &pending, &got, FALSE)) {
                    break;
                }
            }
            TakeTuning(pad, tuning_have);
            if (!pad.Report(report, got, sink)) {
                continue;
            }
            Bridge::Pulse pulse{};
            while (pad.TakePulse(pulse)) {
                Haptic(listener, pulse);
            }
            if (!counted) {
                counted = true;
                ++controllers;
            }
            // The controller's own keyboard and mouse are switched off, again and again: its
            // firmware switches them back on after a few seconds.
            const ULONGLONG now = GetTickCount64();
            if (now - lizard_at >= 2000) {
                lizard_at = now;
                unsigned char feature[Bridge::FeatureLength + 1] = {};
                for (std::size_t i = 0; i < Bridge::LizardOff.size(); ++i) {
                    feature[i] = Bridge::LizardOff[i];
                }
                HidD_SetFeature(listener.device, feature, static_cast<ULONG>(Bridge::FeatureLength));
            }
        }
        pad.Release(sink);
        if (counted) {
            --controllers;
        }
        CloseHandle(pending.hEvent);
        listener.gone.store(true);
    }

    /// A tick in one of the controller's pads: an output report, of the length the
    /// controller says its output reports have.
    void Haptic(Listener& listener, const Bridge::Pulse& pulse) {
        if (listener.output_length == 0) {
            listener.output_length = 64;
            PHIDP_PREPARSED_DATA parsed = nullptr;
            if (HidD_GetPreparsedData(listener.device, &parsed)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(parsed, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength >= Bridge::HapticLength) {
                    listener.output_length = caps.OutputReportByteLength;
                }
                HidD_FreePreparsedData(parsed);
            }
        }
        unsigned char message[512] = {};
        const auto bytes = Bridge::HapticMessage(pulse);
        std::copy(bytes.begin(), bytes.end(), message);
        const DWORD length = std::min<DWORD>(listener.output_length, sizeof(message));
        OVERLAPPED done{};
        done.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD written = 0;
        bool sent = WriteFile(listener.device, message, length, &written, &done) != 0;
        if (!sent && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(done.hEvent, 20) == WAIT_OBJECT_0) {
                sent = GetOverlappedResult(listener.device, &done, &written, FALSE) != 0;
            } else {
                CancelIoEx(listener.device, &done);
                GetOverlappedResult(listener.device, &done, &written, TRUE);
            }
        }
        CloseHandle(done.hEvent);
        if (sent || HidD_SetOutputReport(listener.device, message, length)) {
            ++pulses_sent;
        } else {
            ++pulses_failed;
        }
    }

    static void End(Listener& listener) {
        SetEvent(listener.stop);
        if (listener.thread.joinable()) {
            listener.thread.join();
        }
        CloseHandle(listener.device);
        CloseHandle(listener.stop);
    }

    void Stop() {
        for (auto& [path, listener] : listeners) {
            End(*listener);
        }
        listeners.clear();
    }
#else
    void Scan() {}
    void Stop() {}
#endif
};

SteamPad::SteamPad(QObject* parent) : QObject(parent), d{std::make_unique<Private>(this)} {
    d->scan.setInterval(3000);
    connect(&d->scan, &QTimer::timeout, this, [this] { d->Scan(); });
    d->scan.start();
}

SteamPad::~SteamPad() {
    d->scan.stop();
    d->Stop();
}

void SteamPad::SetGameMode(bool on) {
    d->game_mode.store(on);
}

void SteamPad::SetActive(bool active) {
    if (d->active == active) {
        return;
    }
    d->active = active;
    // Given up at once (an app is starting and will play the controller); taken a moment
    // later, by the next look.
    if (!active) {
        d->Stop();
    } else {
        QTimer::singleShot(200, this, [this] { d->Scan(); });
    }
}

void SteamPad::SetTuning(const Bridge::Tuning& tuning) {
    {
        std::lock_guard lock{d->tuning_mutex};
        d->tuning = tuning;
    }
    ++d->tuning_count;
}

QString SteamPad::Describe() const {
#if defined(_WIN32)
    if (d->steam_runs.load()) {
        return QStringLiteral("Steam is running and plays the controller itself: these settings wait until it is closed.");
    }
    if (d->controllers.load() == 0) {
        return QStringLiteral("No Steam Controller found. With one connected, its pads work here as they will in a game.");
    }
    QString text = QStringLiteral("Steam Controller connected: try the pads here, a slider is felt as it moves.");
    if (d->pulses_failed.load() > 0 && d->pulses_sent.load() == 0) {
        text += QStringLiteral(" The controller refuses the ticks (%1 tried).").arg(d->pulses_failed.load());
    }
    return text;
#else
    return QStringLiteral("On Windows the Steam Controller's pads work here too; on this system its own bridge plays it.");
#endif
}

} // namespace Home
