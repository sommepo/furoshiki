// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <QDir>
#include <QFile>
#include <QSoundEffect>
#include <QTemporaryDir>
#include <QUrl>
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

struct Sounds::Impl {
    QTemporaryDir folder;
    std::array<std::unique_ptr<QSoundEffect>, static_cast<std::size_t>(Sound::Count)> effects;
    bool enabled{true};
};

Sounds::Sounds() : d{std::make_unique<Impl>()} {
    if (!d->folder.isValid()) {
        return;
    }
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i) {
        // The maintainer's own recordings (sounds/, built in as :/sounds/) where there is one.
        const QString recording = RecordingOf(static_cast<Sound>(i));
        if (!recording.isEmpty() && QFile::exists(recording)) {
            auto effect = std::make_unique<QSoundEffect>();
            effect->setSource(QUrl(QStringLiteral("qrc") + recording));
            effect->setVolume(0.6f);
            d->effects[i] = std::move(effect);
            continue;
        }
        const QString path = d->folder.filePath(QStringLiteral("%1.wav").arg(i));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            continue;
        }
        file.write(MakeSound(static_cast<Sound>(i)));
        file.close();
        auto effect = std::make_unique<QSoundEffect>();
        effect->setSource(QUrl::fromLocalFile(path));
        effect->setVolume(0.6f);
        d->effects[i] = std::move(effect);
    }
}

Sounds::~Sounds() = default;

void Sounds::SetEnabled(bool enabled) {
    d->enabled = enabled;
}

void Sounds::Play(Sound sound) {
    const auto& effect = d->effects[static_cast<std::size_t>(sound)];
    if (d->enabled && effect) {
        effect->play();
    }
}

} // namespace Home
