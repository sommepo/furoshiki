// SPDX-FileCopyrightText: melonDS Reader fork contributors (from the Azahar Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once


// The console's processor as a profile's hook sees it: its registers, what a profile calls
// them (`cpu.NAME`), and where a hook may go. This file is the fork's own (the engine that
// uses it is shared: the repository's shared/); each fork has one,
// with the same names.
//
// The DS's ARM9 (an ARM946E-S, ARMv5TE), as melonDS's interpreter hands its registers over at a
// hooked instruction (reader/core/emulator.h). Addresses are 32-bit; it has no floating point.

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include "reader/core/types.h"

namespace Reader {

/// Guest register snapshot taken at a hooked instruction. r[13] is SP, r[14] is LR and r[15] is
/// the address of the hooked instruction itself (not the ARM "PC + 8" value).
struct GuestRegisters {
    std::array<u32, 16> r{};
};

namespace Registers {

/// A register's value as a profile gets it: a whole number, or a floating-point one.
struct Value {
    bool is_float{};
    u64 integer{};
    double number{};
};

/// Parses a register number: decimal digits only, at most `max`.
inline bool Number(std::string_view text, unsigned max, unsigned& out) {
    if (text.empty() || text.size() > 2) {
        return false;
    }
    unsigned value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    out = value;
    return value <= max && (text.size() == 1 || text[0] != '0');
}

/// The register a profile calls `name` (cpu.r0 to r15, cpu.sp, cpu.lr, cpu.pc, cpu.s0 to s31,
/// cpu.d0 to d15), or nothing.
inline std::optional<Value> Read(const GuestRegisters& regs, std::string_view name) {
    unsigned n = 0;
    if (name == "sp") {
        return Value{false, regs.r[13], 0};
    }
    if (name == "lr") {
        return Value{false, regs.r[14], 0};
    }
    if (name == "pc") {
        return Value{false, regs.r[15], 0};
    }
    if (name.substr(0, 1) == "r" && Number(name.substr(1), 15, n)) {
        return Value{false, regs.r[n], 0};
    }
    return std::nullopt;
}

/// The address of the instruction a hook fired at.
inline u64 ProgramCounter(const GuestRegisters& regs) {
    return regs.r[15];
}

/// The highest address of the game's memory a profile may name (mem.*, reader.hook): the 3DS's
/// addresses are 32-bit.
inline constexpr u64 HighestAddress = 0xFFFFFFFFULL;

/// An address as the engine's messages write it: eight digits.
inline std::string AddressText(u64 address) {
    char text[24];
    std::snprintf(text, sizeof(text), "%08llX", static_cast<unsigned long long>(address));
    return text;
}

/// Where a hook asked for at `given` (reader.hook's address) goes: the address of the
/// instruction, or nothing with `error` set when no hook can go there. `word` reads the code.
/// An odd address is the usual way to say "Thumb code"; the instruction is at the even one.
inline std::optional<u64> HookAddress(u64 given, const std::function<std::optional<u32>(u64)>& word,
                                      std::string& error) {
    const bool thumb = (given & 1) != 0;
    const u64 address = given & ~1ULL;
    const auto code = word(address & ~3ULL);
    char text[128];
    if (!code) {
        std::snprintf(text, sizeof(text), "address %08llX is not mapped in the game process",
                      static_cast<unsigned long long>(address));
        error = text;
        return std::nullopt;
    }
    // The ARM9's interpreter carries a hook at any instruction, a conditional one too
    // (reader/core/emulator.h); only an address with nothing behind it is refused.
    (void)thumb;
    return address;
}

} // namespace Registers

} // namespace Reader
