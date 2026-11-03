// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The menu's sounds. Move, Select and Back are recordings the maintainer gave (sounds/, built
// in); the others are made here from sine waves when the app starts.

#include <memory>
#include <QByteArray>
#include <QString>

namespace Home {

enum class Sound { Move, Tab, Select, Back, Launch, Return, Refuse, Count };

/// One sound as a WAV file's bytes (16 bit, mono, 44100 Hz).
[[nodiscard]] QByteArray MakeSound(Sound sound);

class Sounds {
public:
    Sounds();
    ~Sounds();
    void SetEnabled(bool enabled);
    void Play(Sound sound);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Home
