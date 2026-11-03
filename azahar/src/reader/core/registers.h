// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// The console's processor as a profile's hook sees it: its registers, what a profile calls
// them (`cpu.NAME`), and where a hook may go. This file is the fork's own (the engine that
// uses it is shared: the repository's shared/); each fork has one,
// with the same names.
//
// The 3DS's ARM11 cores, as dynarmic hands their registers over at a hooked instruction
// (src/core/arm/dynarmic/arm_dynarmic_reader.cpp). Addresses are 32-bit.

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
/// the address of the hooked instruction itself (not the ARM "PC + 8" value). s holds the VFP
/// registers S0-S31 as raw bits; D<n> is s[2n] (low half) and s[2n + 1].
struct GuestRegisters {
    std::array<u32, 16> r{};
    std::array<u32, 32> s{};
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
    if (name.substr(0, 1) == "s" && Number(name.substr(1), 31, n)) {
        float value;
        std::memcpy(&value, &regs.s[n], sizeof(value));
        return Value{true, 0, value};
    }
    if (name.substr(0, 1) == "d" && Number(name.substr(1), 15, n)) {
        const u64 bits =
            static_cast<u64>(regs.s[2 * n]) | (static_cast<u64>(regs.s[2 * n + 1]) << 32);
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return Value{true, 0, value};
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
    // Checked again when the code is translated (Core::ReaderHooks::EmitHook, which reports
    // it with Engine::ReportUnusableHook), which also covers Thumb code and code that changes
    // later; this is the early, readable error.
    if (!thumb && (*code >> 28) < 0xE) {
        std::snprintf(text, sizeof(text),
                      "the instruction at %08llX (%08X) is conditional; put the hook on an "
                      "unconditional instruction",
                      static_cast<unsigned long long>(address), static_cast<unsigned>(*code));
        error = text;
        return std::nullopt;
    }
    return address;
}

} // namespace Registers

} // namespace Reader
