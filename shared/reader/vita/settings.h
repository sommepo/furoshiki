// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The Vita apps' settings (PS1 Reader, Adrenaline Reader): one file for both,
// ux0:data/readers/settings.txt, lines of "name = value"; "#" begins a comment. A missing file
// or line leaves the default.
//
//   # How far the rear pad's pointer goes: screen widths for a stroke across the whole pad.
//   pointer_speed = 2.0
//   # The line read, written again in a band at the foot of the screen (Adrenaline Reader):
//   # off by the maintainer's word (2026-10-28, "it's in the way"); words are looked up in the
//   # game's own text.
//   show_line = off

#include <string>

namespace VitaReader {

struct Settings {
    float pointer_speed = 2.0f;
    bool show_line = false;

    /// The settings in `text` (the file's), over the defaults; what it does not understand is
    /// left out, and said in `notes`.
    static Settings Parse(const std::string& text, std::string* notes = nullptr);
    /// The file, or the defaults when there is none.
    static Settings Load(const std::string& path, std::string* notes = nullptr);
};

inline constexpr const char* SettingsPath = "ux0:data/readers/settings.txt";

} // namespace VitaReader
