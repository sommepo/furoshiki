// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// The reader's own code names its integer types as the emulator does. This header is all it
// needs of that, so that the reading logic also builds without the emulator (tests, tools).

#include <cstddef>
#include <cstdint>

// The same types as the emulator's common/Pcsx2Types.h, so that both can be included.
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
