// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The program's registers.h: the console's processor as a profile's hook sees it. A fork's
// registers.h describes its one processor at compile time; here the running core describes
// its own (reader_core_cpu, cores/reader_core.h), set by Home::ApplyConsole before its game
// starts. A hook's registers are the values the core hands over at that hook, read by the
// names it gave.

#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include "reader/core/types.h"
#include "reader_core.h"

namespace Reader {

/// The registers at a hooked instruction: the core's snapshot, valid during the hook only (the
/// engine's `cpu` refuses to be read after it).
struct GuestRegisters {
    const reader_core_registers* values{};
};

namespace Registers {

/// A register's value as a profile gets it: a whole number, or a floating-point one.
struct Value {
    bool is_float{};
    u64 integer{};
    double number{};
};

/// The running console's processor. Set before its game starts; never while one runs.
inline reader_core_cpu Cpu{"none", 0, nullptr, 0, 4, 0, 0xFFFFFFFFULL};

/// The highest address of the game's memory a profile may name (mem.*, reader.hook).
inline u64 HighestAddress = 0xFFFFFFFFULL;

/// Takes the running console's processor (Home::ApplyConsole).
inline void SetCpu(const reader_core_cpu& cpu) {
    Cpu = cpu;
    if (Cpu.hook_alignment == 0) {
        Cpu.hook_alignment = 1;
    }
    HighestAddress = cpu.highest_address != 0 ? cpu.highest_address : 0xFFFFFFFFULL;
}

/// The register a profile calls `name` (cpu.a0, cpu.x0, cpu.pc), or nothing.
inline std::optional<Value> Read(const GuestRegisters& regs, std::string_view name) {
    if (regs.values == nullptr) {
        return std::nullopt;
    }
    if (name == "pc") {
        return Value{false, regs.values->pc, 0};
    }
    for (u32 i = 0; i < Cpu.name_count; ++i) {
        const reader_core_register_name& known = Cpu.names[i];
        if (known.name == nullptr || name != known.name || known.index >= regs.values->count) {
            continue;
        }
        const u64 bits = regs.values->values[known.index];
        switch (known.kind) {
        case READER_CORE_REGISTER_FLOAT32: {
            const u32 low = static_cast<u32>(bits);
            float value;
            std::memcpy(&value, &low, sizeof(value));
            return Value{true, 0, value};
        }
        case READER_CORE_REGISTER_FLOAT64: {
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            return Value{true, 0, value};
        }
        default:
            return Value{false, known.bits == 32 ? (bits & 0xFFFFFFFFULL) : bits, 0};
        }
    }
    return std::nullopt;
}

/// The address of the instruction a hook fired at.
inline u64 ProgramCounter(const GuestRegisters& regs) {
    return regs.values != nullptr ? regs.values->pc : 0;
}

/// An address as the engine's messages write it.
inline std::string AddressText(u64 address) {
    char text[24];
    std::snprintf(text, sizeof(text), "%llX", static_cast<unsigned long long>(address));
    return text;
}

/// A message about an address (printf's %llX for it).
inline std::string Describe(const char* format, u64 address) {
    char text[96];
    std::snprintf(text, sizeof(text), format, static_cast<unsigned long long>(address));
    return text;
}

/// Where a hook asked for at `given` (reader.hook's address) goes, by the processor's rules:
/// its instructions' alignment (on ARM an odd address is a Thumb instruction's, two apart).
/// Nothing with `error` set when no hook can go there. `word` reads the code.
inline std::optional<u64> HookAddress(u64 given, const std::function<std::optional<u32>(u64)>& word,
                                      std::string& error) {
    u64 address = given;
    u64 alignment = Cpu.hook_alignment;
    if (Cpu.thumb_odd != 0 && (given & 1) != 0) {
        address = given & ~1ULL;
        alignment = 2;
    }
    if (address % alignment != 0) {
        error = Describe("address %llX is not a multiple of ", given) + std::to_string(alignment);
        return std::nullopt;
    }
    if (!word(address)) {
        error = Describe("address %llX is not mapped in the game", given);
        return std::nullopt;
    }
    return address;
}

} // namespace Registers

} // namespace Reader
