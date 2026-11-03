// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

namespace Dynarmic::A32 {
class IREmitter;
}

// Reader fork: text hooks at guest code addresses, as the Azahar Reader fork has them over the
// same dynarmic hooks. A translated block is ended before a hooked instruction, so that it is
// the first of its own block: a block is only ever entered at its start, so the hook sees the
// registers as the instruction will.
namespace ReaderHooks {

// Called before the instruction at `pc` is read; false ends the block there.
bool BeforeRead(uint32_t pc, Dynarmic::A32::IREmitter &ir);
// Called before it is translated: emits the call into the reader when it is hooked.
// `arm_instruction` is the instruction in ARM code, none in Thumb code.
void BeforeTranslation(uint32_t pc, std::optional<uint32_t> arm_instruction, Dynarmic::A32::IREmitter &ir);

} // namespace ReaderHooks
