// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// The console's processor as a profile's hook sees it: its registers, what a profile calls
// them (`cpu.NAME`), and where a hook may go. This file is the fork's own (the engine that
// uses it is shared: the repository's shared/); each fork has one, with the same names.
//
// This emulator runs no hooks (its profiles read memory from their frame callbacks), so these
// are the registers the engine was written with: MIPS ones, as the PS2's Emotion Engine has
// (in their low 32 bits), kept so that the engine builds the same here.

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include "reader/core/types.h"

namespace Reader {

/// Guest register snapshot taken at a hooked instruction: MIPS registers. pc is the address of
/// the hooked instruction itself. r[4]-r[7] are a0-a3, the first four arguments of a call;
/// r[29] is sp, r[31] ra. f holds the bits of the floating-point registers.
struct GuestRegisters {
    std::array<u32, 32> r{};
    u32 pc{};
    u32 hi{};
    u32 lo{};
    std::array<u32, 32> f{};
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

/// The register a profile calls `name` (cpu.a0, cpu.r4, cpu.f12, cpu.pc), or nothing.
inline std::optional<Value> Read(const GuestRegisters& regs, std::string_view name) {
    // The registers by their usual names, as a disassembler prints them.
    static constexpr std::array<std::string_view, 32> Names{
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
        "t3",   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
        "s6",   "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
    unsigned n = 0;
    if (name == "pc") {
        return Value{false, regs.pc, 0};
    }
    if (name == "hi") {
        return Value{false, regs.hi, 0};
    }
    if (name == "lo") {
        return Value{false, regs.lo, 0};
    }
    if (name.substr(0, 1) == "r" && Number(name.substr(1), 31, n)) {
        return Value{false, regs.r[n], 0};
    }
    if (name.substr(0, 1) == "f" && Number(name.substr(1), 31, n)) {
        float value;
        std::memcpy(&value, &regs.f[n], sizeof(value));
        return Value{true, 0, value};
    }
    for (std::size_t i = 0; i < Names.size(); ++i) {
        if (name == Names[i]) {
            return Value{false, regs.r[i], 0};
        }
    }
    return std::nullopt;
}

/// The address of the instruction a hook fired at.
inline u64 ProgramCounter(const GuestRegisters& regs) {
    return regs.pc;
}

/// The highest address of the game's memory a profile may name (mem.*, reader.hook): the
/// whole 64-bit range, where Lua's negative numbers are the top of it.
inline constexpr u64 HighestAddress = ~0ULL;

/// An address as the engine's messages write it.
inline std::string AddressText(u64 address) {
    char text[24];
    std::snprintf(text, sizeof(text), "%llX", static_cast<unsigned long long>(address));
    return text;
}

/// A message about an address (printf's %llX for it). This header is built as C++17 too.
inline std::string Describe(const char* format, u64 address) {
    char text[96];
    std::snprintf(text, sizeof(text), format, static_cast<unsigned long long>(address));
    return text;
}

/// Where a hook asked for at `given` (reader.hook's address) goes: the address of the
/// instruction, or nothing with `error` set when no hook can go there. `word` reads the code.
inline std::optional<u64> HookAddress(u64 given, const std::function<std::optional<u32>(u64)>& word,
                                      std::string& error) {
    if ((given & 3) != 0) {
        error = Describe("address %llX is not a multiple of four", given);
        return std::nullopt;
    }
    if (!word(given)) {
        error = Describe("address %llX is not mapped in the game", given);
        return std::nullopt;
    }
    return given;
}

} // namespace Registers

} // namespace Reader
