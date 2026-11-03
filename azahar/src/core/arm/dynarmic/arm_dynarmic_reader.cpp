// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <dynarmic/frontend/A32/a32_ir_emitter.h>
#include <dynarmic/frontend/A32/a32_location_descriptor.h>
#include <dynarmic/frontend/A32/a32_types.h>
#include <dynarmic/ir/basic_block.h>
#include <dynarmic/ir/terminal.h>
#include "core/arm/dynarmic/arm_dynarmic_reader.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"

namespace Core::ReaderHooks {

namespace {

// All emulated cores run on the emulation thread, one at a time, so a single staging area is
// enough: the three calls below are emitted back to back for one instruction.
Reader::GuestRegisters staged;

/// Stores a pair of 32-bit values; indices from 16 on are the VFP registers.
void Unpack(u64 value, int index) {
    u32* const slot = index < 16 ? &staged.r[index] : &staged.s[index - 16];
    slot[0] = static_cast<u32>(value);
    slot[1] = static_cast<u32>(value >> 32);
}

void StageLow(u64 r0r1, u64 r2r3, u64 r4r5) {
    Unpack(r0r1, 0);
    Unpack(r2r3, 2);
    Unpack(r4r5, 4);
}

void StageHigh(u64 r6r7, u64 r8r9, u64 r10r11) {
    Unpack(r6r7, 6);
    Unpack(r8r9, 8);
    Unpack(r10r11, 10);
}

/// D<First>, D<First + 1> and D<First + 2>, which are also S<2 * First> and up.
template <int First>
void StageVfp(u64 a, u64 b, u64 c) {
    Unpack(a, 16 + 2 * First);
    Unpack(b, 16 + 2 * First + 2);
    Unpack(c, 16 + 2 * First + 4);
}

void StageVfpLast(u64 d15, u64, u64) {
    Unpack(d15, 16 + 30);
}

void Fire(u64 r12sp, u64 lr, u64 pc) {
    Unpack(r12sp, 12);
    staged.r[14] = static_cast<u32>(lr);
    staged.r[15] = static_cast<u32>(pc);
    Reader::Emu::DispatchHook(staged);
}

} // Anonymous namespace

bool IsHooked(u32 pc) {
    return Reader::Emu::IsHooked(pc);
}

bool BeforeRead(u32 pc, Dynarmic::A32::IREmitter& ir) {
    if (!Reader::Emu::IsHooked(pc)) [[likely]] {
        return true;
    }
    const Dynarmic::A32::LocationDescriptor block_start{ir.block.Location()};
    if (block_start.PC() == pc) {
        return true;
    }
    ir.SetTerm(Dynarmic::IR::Term::LinkBlock{ir.current_location});
    return false;
}

void EmitHook(u32 pc, std::optional<u32> arm_instruction, Dynarmic::A32::IREmitter& ir) {
    const bool conditional =
        arm_instruction ? (*arm_instruction >> 28) < 0xE : ir.current_location.IT().IsInITBlock();
    if (conditional) {
        Reader::Emu::ReportUnusableHook(
            pc, "the instruction there is conditional. Put the hook on an unconditional "
                "instruction.");
        return;
    }
    // The register values are passed through the IR rather than read from the JIT state:
    // get/set elimination may keep the newest values out of the state within a block.
    using Dynarmic::A32::Reg;
    const auto pair = [&ir](Reg low, Reg high) {
        return ir.Pack2x32To1x64(ir.GetRegister(low), ir.GetRegister(high));
    };
    ir.CallHostFunction(&StageLow, pair(Reg::R0, Reg::R1), pair(Reg::R2, Reg::R3),
                        pair(Reg::R4, Reg::R5));
    ir.CallHostFunction(&StageHigh, pair(Reg::R6, Reg::R7), pair(Reg::R8, Reg::R9),
                        pair(Reg::R10, Reg::R11));
    using Dynarmic::A32::ExtReg;
    const auto d = [&ir](int index) {
        const auto reg = static_cast<ExtReg>(static_cast<int>(ExtReg::D0) + index);
        return Dynarmic::IR::U64{ir.GetExtendedRegister(reg)};
    };
    ir.CallHostFunction(&StageVfp<0>, d(0), d(1), d(2));
    ir.CallHostFunction(&StageVfp<3>, d(3), d(4), d(5));
    ir.CallHostFunction(&StageVfp<6>, d(6), d(7), d(8));
    ir.CallHostFunction(&StageVfp<9>, d(9), d(10), d(11));
    ir.CallHostFunction(&StageVfp<12>, d(12), d(13), d(14));
    ir.CallHostFunction(&StageVfpLast, d(15), ir.Imm64(0), ir.Imm64(0));
    ir.CallHostFunction(&Fire, pair(Reg::R12, Reg::SP),
                        ir.ZeroExtendWordToLong(ir.GetRegister(Reg::LR)), ir.Imm64(pc));
}

} // namespace Core::ReaderHooks
