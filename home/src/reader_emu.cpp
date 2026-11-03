// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The program's emulator.cpp (engine/reader/core/emulator.h): what the shared reader windows
// ask of the emulator, asked of the core that runs the game.

#include <atomic>
#include <cstdio>
#include <vector>
#include "core_session.h"
#include "reader/core/emulator.h"

#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif

namespace Reader::Emu {

namespace {
std::atomic<Home::CoreSession*> g_session{nullptr};
} // namespace

void SetSession(Home::CoreSession* session) {
    g_session = session;
}

Home::CoreSession* Session() {
    return g_session.load();
}

void PressButton(int bit, bool down) {
    Home::CoreSession* session = g_session.load();
    if (session != nullptr && bit >= 0 && session->Api().press_button != nullptr) {
        session->Api().press_button(bit, down ? 1 : 0);
    }
}

void RestSticks() {
    // The program's controller hands the core its sticks each frame; while the reader's cursor
    // holds them the input filter keeps them from the game (Home's pad source asks it).
}

bool DumpMemory(const std::string& path) {
    Home::CoreSession* session = g_session.load();
    if (session == nullptr || !session->Running()) {
        return false;
    }
    const reader_core_api& api = session->Api();
    std::vector<reader_core_region> regions(16);
    for (;;) {
        const uint32_t count = api.memory_regions(regions.data(), static_cast<uint32_t>(regions.size()));
        if (count <= regions.size()) {
            regions.resize(count);
            break;
        }
        regions.resize(count);
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    bool ok = !regions.empty();
    std::vector<char> buffer;
    for (const reader_core_region& region : regions) {
        buffer.assign(region.size, '\0');
        const std::size_t got = api.read_memory(region.base, buffer.data(), buffer.size());
        ok = ok && std::fwrite(buffer.data(), 1, got, file) == got;
    }
    std::fclose(file);
    return ok;
}

void ToggleNextChoice() {}

bool GoingToNextChoice() {
    return false;
}

void SetNextChoiceCallback(std::function<void(bool, int, std::uint64_t)>) {}

bool EnsureSaveSync() {
#ifdef READER_NO_SAVE_SYNC
    return false;
#else
    return SaveSync::Host::Started();
#endif
}

} // namespace Reader::Emu
