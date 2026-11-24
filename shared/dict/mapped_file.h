// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Dict {

/**
 * A file read whole into the address space: mapped where the system can (POSIX, Windows), so
 * nothing is read until it is used and the system keeps in memory what is; read into memory
 * where it cannot (the PS Vita).
 */
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    /// Maps `path` (UTF-8). An empty file is open with no data. False when it cannot be read.
    bool Open(const std::string& path);
    void Close();

    [[nodiscard]] bool IsOpen() const {
        return open;
    }
    [[nodiscard]] const std::uint8_t* Data() const {
        return data;
    }
    [[nodiscard]] std::size_t Size() const {
        return size;
    }

private:
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    bool open = false;
    bool mapped = false;
    std::vector<std::uint8_t> copy; // where nothing could be mapped
#ifdef _WIN32
    void* mapping = nullptr;
#endif
};

} // namespace Dict
