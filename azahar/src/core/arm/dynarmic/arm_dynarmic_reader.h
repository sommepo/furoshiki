// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include "common/common_types.h"

namespace Dynarmic::A32 {
class IREmitter;
}

namespace Core::ReaderHooks {

/// True when the reader engine has a hook on the instruction at `pc`.
bool IsHooked(u32 pc);

/**
 * Called before the instruction at `pc` is read during translation. Returns false when the block
 * was ended so that a hooked instruction becomes the first instruction of its own block, which
 * makes a hook fire exactly once per execution of the instruction.
 */
bool BeforeRead(u32 pc, Dynarmic::A32::IREmitter& ir);

/**
 * Called before the hooked instruction at `pc` is translated; emits the call into the reader
 * engine. `arm_instruction` is the instruction word in ARM state and empty in Thumb state.
 *
 * A conditional instruction (an ARM instruction with a condition, a Thumb instruction inside an
 * IT block) cannot carry a hook and is reported to the engine instead: the translator only opens
 * a conditional block while the block is still empty, so with the hook call in front it would end
 * the block before the instruction and link it to itself, forever.
 */
void EmitHook(u32 pc, std::optional<u32> arm_instruction, Dynarmic::A32::IREmitter& ir);

} // namespace Core::ReaderHooks
