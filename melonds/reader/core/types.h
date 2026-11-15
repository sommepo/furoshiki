// SPDX-FileCopyrightText: melonDS Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The integer types the reader layer that every reader fork shares names (u8, u32, s64...):
// the same as melonDS's own (src/types.h), which may be included beside this.
#include <cstdint>
typedef std::uint8_t u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;
typedef std::uint64_t u64;
typedef std::int8_t s8;
typedef std::int16_t s16;
typedef std::int32_t s32;
typedef std::int64_t s64;
