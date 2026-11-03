// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What a loaded core is, without any Qt: the desktop program's loader (core_library.*) and the
// Android app's (android/app/src/main/cpp) fill it, and CoreSession runs it in both.

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include "reader_core.h"

namespace Home {

/// A loaded core: its functions and what it says of its console.
struct Core {
    std::string path;
    const reader_core_api* api{};
    const reader_core_info* info{};
};

/// What the core says it can do beyond the first version of the interface (READER_CORE_HAS_*):
/// 0 for a core built before reader_core_info had it. CheckCore has made sure that every function
/// a feature needs is there.
[[nodiscard]] inline uint32_t Features(const reader_core_info& info) {
    return info.struct_size >= offsetof(reader_core_info, features) + sizeof(info.features) ? info.features : 0;
}

/// Whether the core's table has `field` filled (a core built before it was added has not).
#define READER_CORE_HAS_FIELD(table, field)                                                                 \
    ((table).struct_size >= offsetof(std::remove_cvref_t<decltype(table)>, field) + sizeof((table).field) && \
     (table).field != nullptr)

/// A line of the session's for the program's log (a reader_core_log_level, any thread): each
/// program says where it goes.
void CoreLog(uint32_t level, const std::string& text);

} // namespace Home
