// SPDX-FileCopyrightText: melonDS Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// reader/core/emulator.h: the DS as the reader sees it, for the core's glue and the harness.

#include "reader/core/emulator.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <unordered_map>

#include "ARM.h"
#include "NDS.h"
#include "NDSCart.h"

namespace Reader::Emu {

std::atomic<bool> g_hooks_on{false};

namespace {

const Sink* g_sink = nullptr;
std::atomic<bool> g_stale{true};

// What is known of each halfword of main RAM (0x02000000 to 0x023FFFFF): 0 nothing yet, 1 not
// hooked, 2 hooked. Elsewhere (the ITCM, a mirror) a map.
constexpr u32 MainBase = 0x02000000;
constexpr u32 MainSize = 0x00400000;
std::unique_ptr<u8[]> g_main_known;
std::unordered_map<u32, bool> g_other_known;

bool Hooked(u32 pc) {
    if (g_stale.exchange(false)) {
        if (!g_main_known) {
            g_main_known.reset(new u8[MainSize / 2]);
        }
        std::memset(g_main_known.get(), 0, MainSize / 2);
        g_other_known.clear();
    }
    if (pc - MainBase < MainSize) {
        u8& known = g_main_known[(pc - MainBase) >> 1];
        if (known == 0) {
            known = g_sink != nullptr && g_sink->is_hooked != nullptr && g_sink->is_hooked(pc) ? 2 : 1;
        }
        return known == 2;
    }
    const auto found = g_other_known.find(pc);
    if (found != g_other_known.end()) {
        return found->second;
    }
    const bool hooked = g_sink != nullptr && g_sink->is_hooked != nullptr && g_sink->is_hooked(pc);
    g_other_known.emplace(pc, hooked);
    return hooked;
}

} // namespace

void SetSink(const Sink* sink) {
    g_sink = sink;
    g_stale.store(true);
}

void HooksChanged(bool any) {
    g_stale.store(true);
    g_hooks_on.store(any);
}

void HookAtSlow(u32 pc, const u32* registers, bool thumb) {
    (void)thumb;
    if (!Hooked(pc) || g_sink == nullptr || g_sink->hook == nullptr) {
        return;
    }
    u32 values[16];
    std::memcpy(values, registers, sizeof(values));
    values[15] = pc;
    g_sink->hook(values);
}

void OnFrame() {
    if (g_sink != nullptr && g_sink->frame != nullptr) {
        g_sink->frame();
    }
}

// ---- Memory ----

namespace {

/// Where `address` is in memory the reader may touch directly, and how much follows it there.
u8* Direct(u32 address, u32& left) {
    if (NDS::ARM9 == nullptr || NDS::MainRAM == nullptr) {
        return nullptr;
    }
    ARMv5* arm9 = NDS::ARM9;
    if (address < arm9->ITCMSize) {
        const u32 at = address & (ITCMPhysicalSize - 1);
        left = std::min(ITCMPhysicalSize - at, arm9->ITCMSize - address);
        return arm9->ITCM + at;
    }
    if (arm9->DTCM != nullptr && address - arm9->DTCMBase < arm9->DTCMSize) {
        const u32 at = (address - arm9->DTCMBase) & (DTCMPhysicalSize - 1);
        left = std::min(DTCMPhysicalSize - at, arm9->DTCMBase + arm9->DTCMSize - address);
        return arm9->DTCM + at;
    }
    if ((address >> 24) == 0x02) {
        const u32 at = address & NDS::MainRAMMask;
        left = std::min(NDS::MainRAMMask + 1 - at, 0x03000000u - address);
        return NDS::MainRAM + at;
    }
    if ((address >> 24) == 0x03 && NDS::SharedWRAM != nullptr && address < 0x03000000 + NDS::SharedWRAMSize) {
        // As the ARM9 sees shared WRAM when all of it is its own (the usual case for a game).
        const u32 at = address - 0x03000000;
        left = NDS::SharedWRAMSize - at;
        return NDS::SharedWRAM + at;
    }
    return nullptr;
}

/// Video memory and the rest the ARM9 reads with no side effect: palettes, VRAM, OAM.
bool ReadableAside(u32 address) {
    const u32 area = address >> 24;
    return area == 0x05 || area == 0x06 || area == 0x07;
}

} // namespace

std::size_t ReadMemory(u64 address, void* buffer, std::size_t size) {
    auto* out = static_cast<u8*>(buffer);
    std::size_t done = 0;
    while (done < size) {
        const u64 at = address + done;
        if (at > 0xFFFFFFFFull) {
            break;
        }
        u32 left = 0;
        if (u8* from = Direct(static_cast<u32>(at), left)) {
            const std::size_t take = std::min<std::size_t>(left, size - done);
            std::memcpy(out + done, from, take);
            done += take;
            continue;
        }
        if (ReadableAside(static_cast<u32>(at))) {
            out[done++] = NDS::ARM9Read8(static_cast<u32>(at));
            continue;
        }
        break;
    }
    return done;
}

bool WriteMemory(u64 address, const void* buffer, std::size_t size) {
    // All or nothing: every byte must be writable first.
    for (std::size_t done = 0; done < size;) {
        u32 left = 0;
        if (address + done > 0xFFFFFFFFull || Direct(static_cast<u32>(address + done), left) == nullptr) {
            return false;
        }
        done += left;
    }
    const auto* in = static_cast<const u8*>(buffer);
    for (std::size_t done = 0; done < size;) {
        u32 left = 0;
        u8* to = Direct(static_cast<u32>(address + done), left);
        const std::size_t take = std::min<std::size_t>(left, size - done);
        std::memcpy(to, in + done, take);
        done += take;
    }
    return true;
}

std::vector<Region> MemoryRegions() {
    std::vector<Region> regions;
    if (NDS::ARM9 == nullptr || NDS::MainRAM == nullptr) {
        return regions;
    }
    ARMv5* arm9 = NDS::ARM9;
    if (arm9->ITCMSize > 0) {
        regions.push_back({0, std::min<u64>(arm9->ITCMSize, 0x01000000), true, true, 2});
    }
    regions.push_back({MainBase, NDS::MainRAMMask + 1ull, true, true, 2});
    regions.push_back({0x03000000, NDS::SharedWRAMSize, true, true, 2});
    if (arm9->DTCMSize > 0) {
        regions.push_back({arm9->DTCMBase, arm9->DTCMSize, true, false, 2});
    }
    regions.push_back({0x05000000, 0x800, false, false, 3});
    regions.push_back({0x06000000, 0x00A4000, false, false, 3});
    regions.push_back({0x06800000, 0x00A4000, false, false, 3});
    regions.push_back({0x07000000, 0x800, false, false, 3});
    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) { return a.base < b.base; });
    return regions;
}

// ---- The game ----

namespace {

std::string Code(const char* code) {
    std::string id;
    for (int i = 0; i < 4; ++i) {
        const char c = code[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return {};
        }
        id += c;
    }
    return id;
}

/// A banner's title (UTF-16) as UTF-8: its lines but the last (the publisher's) joined by a
/// space, or its one line.
std::string FirstLine(const char16_t* text, std::size_t length) {
    std::vector<std::string> lines(1);
    for (std::size_t i = 0; i < length && text[i] != 0; ++i) {
        const u32 c = text[i];
        std::string& out = lines.back();
        if (c == u'\n') {
            lines.emplace_back();
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    if (lines.size() > 1) {
        lines.pop_back();
    }
    std::string title;
    for (const auto& line : lines) {
        if (!line.empty()) {
            title += (title.empty() ? "" : " ") + line;
        }
    }
    return title;
}

std::string HeaderTitle(const char* title) {
    std::string out;
    for (int i = 0; i < 12 && title[i] != 0; ++i) {
        out += title[i];
    }
    return out;
}

} // namespace

std::string GameId() {
    if (NDSCart::CartROM == nullptr) {
        return {};
    }
    return Code(NDSCart::Header.GameCode);
}

std::string GameTitle() {
    if (NDSCart::CartROM == nullptr) {
        return {};
    }
    const u32 banner = NDSCart::Header.BannerOffset;
    if (banner != 0 && banner + 0x240 + 0x100 <= NDSCart::CartROMSize) {
        char16_t japanese[128];
        std::memcpy(japanese, NDSCart::CartROM + banner + 0x240, sizeof(japanese));
        const std::string line = FirstLine(japanese, 128);
        if (!line.empty()) {
            return line;
        }
    }
    return HeaderTitle(NDSCart::Header.GameTitle);
}

bool IdentifyFile(const std::string& path, std::string& id, std::string& title) {
    std::ifstream file(path, std::ios::binary);
    char header[0x200];
    if (!file.read(header, sizeof(header))) {
        return false;
    }
    id = Code(header + 0x0C);
    // A DS cartridge's header: its unit code (0, 2 or 3) and the ARM9's entry inside the ROM.
    const u8 unit = static_cast<u8>(header[0x12]);
    u32 arm9_offset;
    std::memcpy(&arm9_offset, header + 0x20, 4);
    if (id.empty() || unit > 3 || arm9_offset < 0x200) {
        return false;
    }
    title = HeaderTitle(header);
    u32 banner;
    std::memcpy(&banner, header + 0x68, 4);
    char16_t japanese[128];
    if (banner != 0 && file.seekg(banner + 0x240) && file.read(reinterpret_cast<char*>(japanese), sizeof(japanese))) {
        const std::string line = FirstLine(japanese, 128);
        if (!line.empty()) {
            title = line;
        }
    }
    return true;
}

} // namespace Reader::Emu
