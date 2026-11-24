// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/mapped_file.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__vita__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Dict {

MappedFile::~MappedFile() {
    Close();
}

MappedFile::MappedFile(MappedFile&& other) noexcept {
    *this = std::move(other);
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        Close();
        data = std::exchange(other.data, nullptr);
        size = std::exchange(other.size, 0);
        open = std::exchange(other.open, false);
        mapped = std::exchange(other.mapped, false);
        copy = std::move(other.copy);
        if (!mapped && !copy.empty()) {
            data = copy.data();
        }
#ifdef _WIN32
        mapping = std::exchange(other.mapping, nullptr);
#endif
    }
    return *this;
}

void MappedFile::Close() {
    if (mapped && data != nullptr) {
#if defined(_WIN32)
        UnmapViewOfFile(data);
        if (mapping != nullptr) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
#elif !defined(__vita__)
        munmap(const_cast<std::uint8_t*>(data), size);
#endif
    }
    copy.clear();
    copy.shrink_to_fit();
    data = nullptr;
    size = 0;
    open = false;
    mapped = false;
}

bool MappedFile::Open(const std::string& path) {
    Close();
#if defined(_WIN32)
    std::wstring wide;
    try {
        wide = std::filesystem::path(std::u8string(path.begin(), path.end())).wstring();
    } catch (...) {
        return false;
    }
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 ||
        static_cast<std::uint64_t>(length.QuadPart) > std::numeric_limits<std::size_t>::max()) {
        CloseHandle(file);
        return false;
    }
    size = static_cast<std::size_t>(length.QuadPart);
    if (size == 0) {
        CloseHandle(file);
        open = true;
        return true;
    }
    mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (mapping == nullptr) {
        size = 0;
        return false;
    }
    data = static_cast<const std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (data == nullptr) {
        CloseHandle(mapping);
        mapping = nullptr;
        size = 0;
        return false;
    }
    mapped = true;
    open = true;
    return true;
#elif !defined(__vita__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct stat info {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > std::numeric_limits<std::size_t>::max()) {
        ::close(fd);
        return false;
    }
    size = static_cast<std::size_t>(info.st_size);
    if (size == 0) {
        ::close(fd);
        open = true;
        return true;
    }
    void* address = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (address == MAP_FAILED) {
        size = 0;
        return false;
    }
    data = static_cast<const std::uint8_t*>(address);
    mapped = true;
    open = true;
    return true;
#else
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const std::streampos pos = in.tellg();
    if (pos < 0 || static_cast<std::uint64_t>(pos) > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    size = static_cast<std::size_t>(pos);
    copy.resize(size);
    in.seekg(0);
    if (size > 0 && !in.read(reinterpret_cast<char*>(copy.data()), static_cast<std::streamsize>(size))) {
        copy.clear();
        size = 0;
        return false;
    }
    data = copy.data();
    open = true;
    return true;
#endif
}

} // namespace Dict
