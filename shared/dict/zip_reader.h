// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "dict/mapped_file.h"

namespace Dict {

/**
 * A zip archive read from a mapped file: its directory, and any entry's bytes. Reading is
 * safe from several threads at once (each read inflates on its own). Stored and deflated
 * entries are read, and ZIP64 archives; nothing else a dictionary is made with.
 */
class ZipArchive {
public:
    struct Entry {
        std::string name; ///< as the archive has it (UTF-8 in every dictionary seen)
        std::uint64_t header_offset = 0;
        std::uint64_t compressed_size = 0;
        std::uint64_t size = 0;
        std::uint16_t method = 0;
    };

    /// Opens `path` and reads its directory. On failure, `error` says why.
    bool Open(const std::string& path, std::string& error);

    [[nodiscard]] const std::vector<Entry>& Entries() const {
        return entries;
    }
    /// The entry called `name`, or nullptr.
    [[nodiscard]] const Entry* Find(const std::string& name) const;
    /// The bytes of `entry` into `out`. False when they cannot be read (a damaged archive).
    bool Read(const Entry& entry, std::string& out) const;

private:
    MappedFile file;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::size_t> by_name;
};

} // namespace Dict
