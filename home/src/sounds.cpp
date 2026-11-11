// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>
#include <QDir>
#include <QFile>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMediaDevices>
#include <QThread>
#include <QTimer>
#include "sounds.h"

namespace Home {

namespace {

constexpr int Rate = 44100;
constexpr double Pi = 3.14159265358979323846;

/// A note: a sine with a soft second partial, a quick attack and a decay.
struct Note {
    double start;    ///< Seconds from the sound's beginning
    double length;
    double from, to; ///< Its pitch glides from one frequency to the other
    double level;
};

std::vector<Note> NotesOf(Sound sound) {
    switch (sound) {
    // Glass and air: low, soft, with a tail, as a console's menu of the year 2000 sounded.
    case Sound::Move:
        return {{0, 0.09, 660, 620, 0.26}, {0, 0.05, 1980, 1860, 0.07}};
    case Sound::Tab:
        return {{0, 0.16, 392, 440, 0.24}, {0.02, 0.12, 1320, 1320, 0.06}};
    case Sound::Select:
        return {{0, 0.34, 196, 98, 0.34}, {0, 0.22, 587, 880, 0.16}, {0.05, 0.30, 1760, 1760, 0.05}};
    case Sound::Back:
        return {{0, 0.30, 110, 185, 0.30}, {0, 0.20, 784, 523, 0.14}};
    case Sound::Launch:
        return {{0, 1.10, 65, 196, 0.30}, {0.10, 1.00, 131, 392, 0.20}, {0.35, 0.80, 784, 1175, 0.10},
                {0.60, 0.60, 1568, 2349, 0.05}};
    case Sound::Return:
        return {{0, 0.60, 196, 82, 0.28}, {0, 0.40, 880, 587, 0.12}};
    case Sound::Refuse:
        return {{0, 0.14, 147, 139, 0.34}, {0.16, 0.18, 147, 131, 0.34}};
    case Sound::Count:
        break;
    }
    return {};
}

/// The recording a sound is played from, or nothing for one made here.
QString RecordingOf(Sound sound) {
    switch (sound) {
    case Sound::Move:
        return QStringLiteral(":/sounds/move.wav");
    case Sound::Select:
        return QStringLiteral(":/sounds/select.wav");
    case Sound::Back:
        return QStringLiteral(":/sounds/back.wav");
    default:
        return {};
    }
}

void Put(QByteArray& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

} // namespace

QByteArray MakeSound(Sound sound) {
    const std::vector<Note> notes = NotesOf(sound);
    double length = 0.02;
    for (const Note& note : notes) {
        length = std::max(length, note.start + note.length);
    }
    std::vector<double> wave(static_cast<std::size_t>((length + 0.02) * Rate), 0.0);
    for (const Note& note : notes) {
        const int first = static_cast<int>(note.start * Rate);
        const int count = static_cast<int>(note.length * Rate);
        double phase = 0;
        for (int i = 0; i < count && first + i < static_cast<int>(wave.size()); ++i) {
            const double t = static_cast<double>(i) / count;
            const double frequency = note.from + (note.to - note.from) * t;
            phase += 2 * Pi * frequency / Rate;
            const double attack = std::min(1.0, i / (0.004 * Rate));
            const double decay = std::exp(-4.5 * t) * (1.0 - t);
            wave[first + i] += note.level * attack * decay * (std::sin(phase) + 0.22 * std::sin(2 * phase));
        }
    }
    // A room around it: two soft echoes.
    const int tail = static_cast<int>(0.22 * Rate);
    wave.resize(wave.size() + tail * 2, 0.0);
    for (std::size_t i = 0; i + tail < wave.size(); ++i) {
        wave[i + tail] += wave[i] * 0.22;
    }
    // And a gentle end, so that nothing clicks.
    const std::size_t fade = std::min<std::size_t>(wave.size(), static_cast<std::size_t>(0.03 * Rate));
    for (std::size_t i = 0; i < fade; ++i) {
        wave[wave.size() - 1 - i] *= static_cast<double>(i) / fade;
    }
    QByteArray data;
    for (const double sample : wave) {
        const int value = static_cast<int>(std::clamp(sample, -1.0, 1.0) * 32000);
        Put(data, static_cast<std::uint16_t>(static_cast<std::int16_t>(value)), 2);
    }
    QByteArray file;
    file.append("RIFF");
    Put(file, static_cast<std::uint32_t>(36 + data.size()), 4);
    file.append("WAVEfmt ");
    Put(file, 16, 4);
    Put(file, 1, 2);        // PCM
    Put(file, 1, 2);        // one channel
    Put(file, Rate, 4);
    Put(file, Rate * 2, 4); // bytes a second
    Put(file, 2, 2);
    Put(file, 16, 2);
    file.append("data");
    Put(file, static_cast<std::uint32_t>(data.size()), 4);
    file.append(data);
    return file;
}

// The sounds are mixed here and played through one audio output, opened once on a thread of
// its own. QSoundEffect opened the device again for every sound: on Windows that held its
// caller some 30 to 90 ms, and a wheel's quick turn (a move sound a row) stood the window still
// for seconds (2026-11-05, the Z13); on a thread of their own the effects then fell silent for
// long stretches after much scrolling. Now a sound is only added to the mix. The output is
// paused after a few quiet seconds, so that an open stream does not keep the machine awake.
namespace {

constexpr int MixRate = 48000; // the recordings' rate; stereo, 16 bit
constexpr int MixVoices = 6;   // at most at once; a seventh ends the oldest
constexpr float MixVolume = 0.6f;

/// A WAV file's 16-bit PCM as stereo at MixRate (another rate taken across by a straight line).
std::vector<std::int16_t> ReadWave(const QByteArray& file) {
    const auto u16 = [&](qsizetype at) { return static_cast<std::uint16_t>(static_cast<uchar>(file[at]) | static_cast<uchar>(file[at + 1]) << 8); };
    const auto u32 = [&](qsizetype at) { return static_cast<std::uint32_t>(u16(at)) | static_cast<std::uint32_t>(u16(at + 2)) << 16; };
    if (file.size() < 12 || !file.startsWith("RIFF") || file.mid(8, 4) != "WAVE") {
        return {};
    }
    int channels = 0, rate = 0, bits = 0;
    QByteArray data;
    for (qsizetype at = 12; at + 8 <= file.size();) {
        const QByteArray id = file.mid(at, 4);
        const qsizetype size = u32(at + 4);
        if (id == "fmt " && size >= 16 && at + 8 + 16 <= file.size()) {
            if (u16(at + 8) != 1) { // PCM only
                return {};
            }
            channels = u16(at + 10);
            rate = static_cast<int>(u32(at + 12));
            bits = u16(at + 22);
        } else if (id == "data") {
            data = file.mid(at + 8, size);
        }
        at += 8 + size + (size & 1);
    }
    if (bits != 16 || (channels != 1 && channels != 2) || rate <= 0 || data.isEmpty()) {
        return {};
    }
    const auto* in = reinterpret_cast<const std::int16_t*>(data.constData());
    const qsizetype frames = data.size() / (2 * channels);
    const auto sample = [&](qsizetype frame, int channel) { return static_cast<double>(in[frame * channels + std::min(channel, channels - 1)]); };
    const qsizetype out_frames = frames * MixRate / rate;
    std::vector<std::int16_t> out(static_cast<std::size_t>(out_frames) * 2);
    for (qsizetype f = 0; f < out_frames; ++f) {
        const double at = static_cast<double>(f) * rate / MixRate;
        const qsizetype a = std::min<qsizetype>(static_cast<qsizetype>(at), frames - 1), b = std::min<qsizetype>(a + 1, frames - 1);
        const double t = at - a;
        for (int c = 0; c < 2; ++c) {
            out[f * 2 + c] = static_cast<std::int16_t>(std::lround(sample(a, c) * (1 - t) + sample(b, c) * t));
        }
    }
    return out;
}

/// What the output reads: the sounds playing, added together, or quiet.
class Mixer : public QIODevice {
public:
    explicit Mixer(const std::array<std::vector<std::int16_t>, static_cast<std::size_t>(Sound::Count)>* waves) : waves{waves} {
        open(QIODevice::ReadOnly);
        quiet.start();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 20; }
    void Add(std::size_t sound) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (voices.size() >= MixVoices) {
            voices.erase(voices.begin());
        }
        voices.push_back({sound, 0});
    }
    /// How long nothing has played (ms), or 0 while something does.
    qint64 Quiet() {
        const std::lock_guard<std::mutex> lock(mutex);
        return voices.empty() ? quiet.elapsed() : 0;
    }

protected:
    qint64 readData(char* data, qint64 size) override {
        const qint64 frames = size / 4;
        auto* out = reinterpret_cast<std::int16_t*>(data);
        std::vector<float> mix(static_cast<std::size_t>(frames) * 2, 0.0f);
        {
            const std::lock_guard<std::mutex> lock(mutex);
            for (auto voice = voices.begin(); voice != voices.end();) {
                const std::vector<std::int16_t>& wave = (*waves)[voice->sound];
                const std::size_t left = (wave.size() - std::min(wave.size(), voice->at)) / 2;
                const std::size_t n = std::min<std::size_t>(left, static_cast<std::size_t>(frames));
                for (std::size_t i = 0; i < n * 2; ++i) {
                    mix[i] += wave[voice->at + i] * MixVolume;
                }
                voice->at += n * 2;
                voice = voice->at >= wave.size() ? voices.erase(voice) : voice + 1;
            }
            if (!voices.empty()) {
                quiet.restart();
            }
        }
        for (std::size_t i = 0; i < mix.size(); ++i) {
            out[i] = static_cast<std::int16_t>(std::clamp(std::lround(mix[i]), -32768L, 32767L));
        }
        return frames * 4;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    struct Voice {
        std::size_t sound;
        std::size_t at; ///< In samples (two a frame)
    };
    const std::array<std::vector<std::int16_t>, static_cast<std::size_t>(Sound::Count)>* waves;
    std::mutex mutex;
    std::vector<Voice> voices;
    QElapsedTimer quiet;
};

} // namespace

struct Sounds::Impl {
    std::array<std::vector<std::int16_t>, static_cast<std::size_t>(Sound::Count)> waves;
    QThread thread;
    QObject* host{}; ///< On the thread: the output, the mixer, the timer that pauses it
    Mixer* mixer{};
    QAudioSink* sink{};
    std::atomic<bool> paused{true};
    bool enabled{true};

    /// On the thread: the output for the system's output device now (again when it changes).
    void Open() {
        if (sink != nullptr) {
            sink->stop();
            delete sink;
            sink = nullptr;
        }
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull()) {
            qWarning("menu sounds: no audio output");
            return;
        }
        QAudioFormat format;
        format.setSampleRate(MixRate);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!mixer->isOpen()) {
            mixer->open(QIODevice::ReadOnly);
        }
        sink = new QAudioSink(device, format, host);
        sink->setBufferSize(MixRate / 1000 * 4 * 60); // some 60 ms ahead
        sink->start(mixer);
        if (sink->error() != QAudio::NoError) {
            qWarning("menu sounds: the output did not start (%d)", static_cast<int>(sink->error()));
        }
        sink->suspend();
        paused = true;
    }
};

Sounds::Sounds() : d{std::make_unique<Impl>()} {
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i) {
        // The maintainer's own recordings (sounds/, built in as :/sounds/) where there is one.
        const QString recording = RecordingOf(static_cast<Sound>(i));
        QFile file(recording);
        if (!recording.isEmpty() && file.open(QIODevice::ReadOnly)) {
            d->waves[i] = ReadWave(file.readAll());
        } else {
            d->waves[i] = ReadWave(MakeSound(static_cast<Sound>(i)));
        }
    }
    d->thread.setObjectName(QStringLiteral("menu sounds"));
    d->host = new QObject;
    d->host->moveToThread(&d->thread);
    d->thread.start();
    Impl* impl = d.get();
    QMetaObject::invokeMethod(
        d->host,
        [impl] {
            impl->mixer = new Mixer(&impl->waves);
            impl->mixer->setParent(impl->host);
            auto* devices = new QMediaDevices(impl->host);
            QObject::connect(devices, &QMediaDevices::audioOutputsChanged, impl->host, [impl] { impl->Open(); });
            impl->Open();
            // Paused after three quiet seconds; Play takes it up again.
            auto* idle = new QTimer(impl->host);
            QObject::connect(idle, &QTimer::timeout, impl->host, [impl] {
                if (impl->sink != nullptr && !impl->paused && impl->mixer->Quiet() > 3000) {
                    impl->sink->suspend();
                    impl->paused = true;
                }
            });
            idle->start(500);
        },
        Qt::BlockingQueuedConnection);
}

Sounds::~Sounds() {
    Impl* impl = d.get();
    QMetaObject::invokeMethod(
        d->host,
        [impl] {
            if (impl->sink != nullptr) {
                impl->sink->stop();
            }
            qDeleteAll(impl->host->children()); // the output, the mixer, the timer, on their thread
            impl->sink = nullptr;
            impl->mixer = nullptr;
        },
        Qt::BlockingQueuedConnection);
    d->thread.quit();
    d->thread.wait();
    delete d->host;
}

void Sounds::SetEnabled(bool enabled) {
    d->enabled = enabled;
}

void Sounds::Play(Sound sound) {
    const auto i = static_cast<std::size_t>(sound);
    if (!d->enabled || d->mixer == nullptr || d->waves[i].empty()) {
        return;
    }
    d->mixer->Add(i);
    if (d->paused.exchange(false)) {
        Impl* impl = d.get();
        QMetaObject::invokeMethod(d->host, [impl] {
            if (impl->sink != nullptr) {
                impl->sink->resume();
            }
        });
    }
}

} // namespace Home
