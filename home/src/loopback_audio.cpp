// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "loopback_audio.h"

#include <algorithm>
#include <chrono>
#include <QtGlobal>
#include <vector>
#include "reader/core/audio_tap.h"

#ifdef _WIN32
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#endif

namespace Home {

#ifdef _WIN32
namespace {
// The interfaces' IDs, written out so that no import library has to carry them.
const CLSID DeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID EnumeratorId = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID ClientId = {0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const IID CaptureId = {0xC8ADBD64, 0xE71E, 0x48a0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const GUID FloatFormat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

template <class T> void Release(T*& thing) {
    if (thing != nullptr) {
        thing->Release();
        thing = nullptr;
    }
}
} // namespace
#endif

Loopback& Loopback::Instance() {
    static Loopback one;
    return one;
}

Loopback::~Loopback() {
    Stop();
}

bool Loopback::Start() {
#ifdef _WIN32
    if (running.load()) {
        return true;
    }
    if (worker.joinable()) {
        worker.join();
    }
    stop = false;
    running = true;
    worker = std::thread([this] { Run(); });
    return true;
#else
    return false;
#endif
}

void Loopback::Stop() {
    stop = true;
    if (worker.joinable()) {
        worker.join();
    }
    running = false;
}

void Loopback::Run() {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* devices = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    const auto done = [&] {
        if (client != nullptr) {
            client->Stop();
        }
        Release(capture);
        Release(client);
        Release(device);
        Release(devices);
        if (format != nullptr) {
            CoTaskMemFree(format);
        }
        CoUninitialize();
        running = false;
    };
    if (FAILED(CoCreateInstance(DeviceEnumerator, nullptr, CLSCTX_ALL, EnumeratorId, reinterpret_cast<void**>(&devices))) ||
        FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->Activate(ClientId, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) || FAILED(client->GetMixFormat(&format)) ||
        FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2'000'000, 0, format, nullptr)) ||
        FAILED(client->GetService(CaptureId, reinterpret_cast<void**>(&capture))) || FAILED(client->Start())) {
        done();
        return;
    }
    // What the output mixes at: float or 16-bit, its rate and channels.
    bool floats = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        floats = IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat, FloatFormat);
    }
    const unsigned channels = std::max<unsigned>(1, format->nChannels), rate = std::max<DWORD>(1, format->nSamplesPerSec);
    const unsigned bits = format->wBitsPerSample;
    constexpr unsigned Rate = Reader::AudioTap::SampleRate;
    // A plain step through the samples from the output's rate to the history's (both are 48 kHz
    // on most computers, where it passes them through).
    double step = static_cast<double>(rate) / Rate, at = 0;
    std::vector<qint16> out;
    const auto start = std::chrono::steady_clock::now();
    unsigned long long pushed = 0;
    while (!stop.load()) {
        UINT32 waiting = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&waiting)) && waiting > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            const auto sample = [&](UINT32 frame, unsigned channel) -> float {
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                    return 0;
                }
                const unsigned c = std::min(channel, channels - 1);
                if (floats && bits == 32) {
                    return reinterpret_cast<const float*>(data)[frame * channels + c];
                }
                if (bits == 16) {
                    return reinterpret_cast<const qint16*>(data)[frame * channels + c] / 32768.0f;
                }
                if (bits == 32) {
                    return reinterpret_cast<const qint32*>(data)[frame * channels + c] / 2147483648.0f;
                }
                return 0;
            };
            out.clear();
            for (; at < frames; at += step) {
                const UINT32 frame = static_cast<UINT32>(at);
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const float value = std::clamp(sample(frame, channel), -1.0f, 1.0f);
                    out.push_back(static_cast<qint16>(value * 32767.0f));
                }
            }
            at -= frames;
            capture->ReleaseBuffer(frames);
            if (!out.empty()) {
                Reader::AudioTap::Instance().Push(out.data(), out.size() / 2);
                pushed += out.size() / 2;
            }
        }
        // Nothing plays, nothing comes: the quiet is put in, so that the history keeps time.
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto due = static_cast<unsigned long long>(seconds * Rate);
        if (due > pushed + Rate / 25) {
            const std::vector<qint16> quiet(static_cast<std::size_t>(due - pushed) * 2, 0);
            Reader::AudioTap::Instance().Push(quiet.data(), due - pushed);
            pushed = due;
        }
        Sleep(10);
    }
    done();
#endif
}

} // namespace Home
