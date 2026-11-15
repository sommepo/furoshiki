// SPDX-FileCopyrightText: melonDS Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// What melonDS's own files call at a "Reader fork" comment, and what the reader's side (the core's
// glue, reader/core_library/melonds_core.cpp, or the harness, reader/harness/harness.cpp) answers
// through a Sink. The DS's memory as the reader sees it and the game's ID are here too, the same
// for both.
//
// Hooks: the ARM9's interpreter (ARMv5::Execute) asks HookAt for each instruction while the
// reader has hooks; whether an address is hooked is asked of the sink once and kept (a byte for
// each halfword of main RAM, a map for the rest) until the hooks change. No JIT is built: a hook
// is never compiled away.

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "reader/core/types.h"

namespace Reader::Emu {

/// Where the reader's side hears of the game.
struct Sink {
    /// Whether the instruction at `pc` (even, a Thumb one too) is hooked.
    bool (*is_hooked)(u32 pc) = nullptr;
    /// A hooked instruction is about to run: r0 to r15, r15 its own address.
    void (*hook)(const u32 registers[16]) = nullptr;
    /// A frame was shown (after retro_run's frame).
    void (*frame)() = nullptr;
};

void SetSink(const Sink* sink);

/// Any hooks at all (the interpreter's only test while there are none).
extern std::atomic<bool> g_hooks_on;

/// The reader's hooks changed: what is known of each address is forgotten (on the game's
/// thread, at the next instruction). `any`: whether there are hooks now.
void HooksChanged(bool any);

/// The interpreter's call, for the instruction at `pc` with the registers as they are.
void HookAtSlow(u32 pc, const u32* registers, bool thumb);
inline void HookAt(u32 pc, const u32* registers, bool thumb) {
    if (g_hooks_on.load(std::memory_order_relaxed)) [[unlikely]] {
        HookAtSlow(pc, registers, thumb);
    }
}

/// After each frame (libretro.cpp's retro_run).
void OnFrame();

// ---- The DS's memory, as the ARM9 sees it ----

struct Region {
    u64 base;
    u64 size;
    bool writable;
    bool executable;
    u32 state; ///< 2 the game's, 3 video memory
};

/// Reads up to `size` bytes from `address`, stopping where nothing is (or where reading would
/// change something: the I/O registers).
std::size_t ReadMemory(u64 address, void* buffer, std::size_t size);
/// All or nothing; only the game's RAM (main RAM, the ARM9's TCMs, shared WRAM).
bool WriteMemory(u64 address, const void* buffer, std::size_t size);
std::vector<Region> MemoryRegions();

/// The cartridge's game code ("AWIJ"), or empty without a game.
std::string GameId();
/// Its title as the cartridge's banner gives it in Japanese (its first line), or its header's.
std::string GameTitle();
/// What a file is: the game code and title from its header and banner; false for no DS game.
bool IdentifyFile(const std::string& path, std::string& id, std::string& title);

} // namespace Reader::Emu
