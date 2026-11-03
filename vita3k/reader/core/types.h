// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The integer types the reader layer that every reader fork shares names (u8, u32, s64...):
// inside the reader's namespace here, as Vita3K writes uint32_t and has no such names.
#include <cstdint>

namespace Reader {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
} // namespace Reader
