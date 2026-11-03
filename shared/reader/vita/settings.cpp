// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reader/vita/settings.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace VitaReader {

namespace {

std::string Trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        return {};
    }
    return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
}

} // namespace

Settings Settings::Parse(const std::string& text, std::string* notes) {
    Settings settings;
    std::istringstream lines{text};
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }
        const auto equals = line.find('=');
        const std::string name = Trim(line.substr(0, equals));
        const std::string value = equals == std::string::npos ? std::string() : Trim(line.substr(equals + 1));
        char* end = nullptr;
        const float number = std::strtof(value.c_str(), &end);
        if (name == "pointer_speed" && !value.empty() && end != nullptr && *end == '\0' && number > 0) {
            settings.pointer_speed = number;
        } else if (name == "show_line" && (value == "on" || value == "off")) {
            settings.show_line = value == "on";
        } else if (notes != nullptr) {
            *notes += (notes->empty() ? "" : "; ") + std::string("settings: not understood: ") + line;
        }
    }
    return settings;
}

Settings Settings::Load(const std::string& path, std::string* notes) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::string text;
    char chunk[1024];
    std::size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        text.append(chunk, got);
    }
    std::fclose(file);
    return Parse(text, notes);
}

} // namespace VitaReader
