// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cpu/impl/dynarmic_reader.h>

#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <dynarmic/frontend/A32/a32_location_descriptor.h>
#include <dynarmic/frontend/A32/a32_types.h>
#include <dynarmic/ir/basic_block.h>
#include <dynarmic/ir/terminal.h>

#include <reader/core/emulator.h>

namespace ReaderHooks {

namespace {

using Reader::Emu::events;

// Every guest thread runs on a host thread of its own, and the calls below are emitted back to
// back for one instruction, so each thread stages the registers for itself.
thread_local Reader::GuestRegisters staged;

// Stores a pair of 32-bit values; indices from 16 on are the VFP registers.
void Unpack(uint64_t value, int index) {
    uint32_t *const slot = index < 16 ? &staged.r[index] : &staged.s[index - 16];
    slot[0] = static_cast<uint32_t>(value);
    slot[1] = static_cast<uint32_t>(value >> 32);
}

void StageLow(uint64_t r0r1, uint64_t r2r3, uint64_t r4r5) {
    Unpack(r0r1, 0);
    Unpack(r2r3, 2);
    Unpack(r4r5, 4);
}

void StageHigh(uint64_t r6r7, uint64_t r8r9, uint64_t r10r11) {
    Unpack(r6r7, 6);
    Unpack(r8r9, 8);
    Unpack(r10r11, 10);
}

// D<First>, D<First + 1> and D<First + 2>, which are also S<2 * First> and up.
template <int First>
void StageVfp(uint64_t a, uint64_t b, uint64_t c) {
    Unpack(a, 16 + 2 * First);
    Unpack(b, 16 + 2 * First + 2);
    Unpack(c, 16 + 2 * First + 4);
}

void StageVfpLast(uint64_t d15, uint64_t, uint64_t) {
    Unpack(d15, 16 + 30);
}

void Fire(uint64_t r12sp, uint64_t lr, uint64_t pc) {
    Unpack(r12sp, 12);
    staged.r[14] = static_cast<uint32_t>(lr);
    staged.r[15] = static_cast<uint32_t>(pc);
    if (events.hook != nullptr)
        events.hook(staged);
}

bool Hooked(uint32_t pc) {
    return events.is_hooked != nullptr && events.is_hooked(pc);
}

} // namespace

bool BeforeRead(uint32_t pc, Dynarmic::A32::IREmitter &ir) {
    if (!Hooked(pc)) [[likely]]
        return true;
    const Dynarmic::A32::LocationDescriptor block_start{ ir.block.Location() };
    if (block_start.PC() == pc)
        return true;
    ir.SetTerm(Dynarmic::IR::Term::LinkBlock{ ir.current_location });
    return false;
}

void BeforeTranslation(uint32_t pc, std::optional<uint32_t> arm_instruction, Dynarmic::A32::IREmitter &ir) {
    if (!Hooked(pc)) [[likely]]
        return;
    const bool conditional = arm_instruction ? (*arm_instruction >> 28) < 0xE : ir.current_location.IT().IsInITBlock();
    if (conditional) {
        if (events.hook_unusable != nullptr)
            events.hook_unusable(pc, "the instruction there is conditional. Put the hook on an unconditional instruction.");
        return;
    }
    // The register values are passed through the IR rather than read from the JIT state:
    // get/set elimination may keep the newest values out of the state within a block.
    using Dynarmic::A32::Reg;
    const auto pair = [&ir](Reg low, Reg high) {
        return ir.Pack2x32To1x64(ir.GetRegister(low), ir.GetRegister(high));
    };
    ir.CallHostFunction(&StageLow, pair(Reg::R0, Reg::R1), pair(Reg::R2, Reg::R3), pair(Reg::R4, Reg::R5));
    ir.CallHostFunction(&StageHigh, pair(Reg::R6, Reg::R7), pair(Reg::R8, Reg::R9), pair(Reg::R10, Reg::R11));
    using Dynarmic::A32::ExtReg;
    const auto d = [&ir](int index) {
        const auto reg = static_cast<ExtReg>(static_cast<int>(ExtReg::D0) + index);
        return Dynarmic::IR::U64{ ir.GetExtendedRegister(reg) };
    };
    ir.CallHostFunction(&StageVfp<0>, d(0), d(1), d(2));
    ir.CallHostFunction(&StageVfp<3>, d(3), d(4), d(5));
    ir.CallHostFunction(&StageVfp<6>, d(6), d(7), d(8));
    ir.CallHostFunction(&StageVfp<9>, d(9), d(10), d(11));
    ir.CallHostFunction(&StageVfp<12>, d(12), d(13), d(14));
    ir.CallHostFunction(&StageVfpLast, d(15), ir.Imm64(0), ir.Imm64(0));
    ir.CallHostFunction(&Fire, pair(Reg::R12, Reg::SP), ir.ZeroExtendWordToLong(ir.GetRegister(Reg::LR)), ir.Imm64(pc));
}

} // namespace ReaderHooks
