// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/zip_reader.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace Dict {

namespace {

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t U64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(U32(p)) | (static_cast<std::uint64_t>(U32(p + 4)) << 32);
}

constexpr std::uint32_t EndSignature = 0x06054b50;
constexpr std::uint32_t End64Signature = 0x06064b50;
constexpr std::uint32_t End64LocatorSignature = 0x07064b50;
constexpr std::uint32_t CentralSignature = 0x02014b50;
constexpr std::uint32_t LocalSignature = 0x04034b50;

} // namespace

bool ZipArchive::Open(const std::string& path, std::string& error) {
    entries.clear();
    by_name.clear();
    if (!file.Open(path)) {
        error = "Couldn't open the file";
        return false;
    }
    const std::uint8_t* base = file.Data();
    const std::uint64_t size = file.Size();
    if (size < 22) {
        error = "This file isn't a zip file";
        return false;
    }
    // The end record is the last thing in the file, before a comment of at most 64 kB.
    std::uint64_t end = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t lowest = size > 22 + 65535 ? size - 22 - 65535 : 0;
    for (std::uint64_t at = size - 22 + 1; at-- > lowest;) {
        if (U32(base + at) == EndSignature && at + 22 + U16(base + at + 20) == size) {
            end = at;
            break;
        }
    }
    if (end == std::numeric_limits<std::uint64_t>::max()) {
        error = "This file isn't a complete zip file (it may not have finished downloading)";
        return false;
    }
    std::uint64_t count = U16(base + end + 10);
    std::uint64_t directory_size = U32(base + end + 12);
    std::uint64_t directory = U32(base + end + 16);
    if (end >= 20 && U32(base + end - 20) == End64LocatorSignature) {
        const std::uint64_t end64 = U64(base + end - 20 + 8);
        if (end64 <= size && size - end64 >= 56 && U32(base + end64) == End64Signature) {
            count = U64(base + end64 + 32);
            directory_size = U64(base + end64 + 40);
            directory = U64(base + end64 + 48);
        }
    }
    if (directory > size || directory_size > size - directory) {
        error = "This zip file is damaged";
        return false;
    }
    entries.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(count, 1u << 22)));
    std::uint64_t at = directory;
    const std::uint64_t stop = directory + directory_size;
    while (stop - at >= 46 && U32(base + at) == CentralSignature) {
        const std::uint8_t* h = base + at;
        Entry entry;
        entry.method = U16(h + 10);
        entry.compressed_size = U32(h + 20);
        entry.size = U32(h + 24);
        const std::uint16_t name_length = U16(h + 28);
        const std::uint16_t extra_length = U16(h + 30);
        const std::uint16_t comment_length = U16(h + 32);
        entry.header_offset = U32(h + 42);
        const std::uint64_t header_rest = static_cast<std::uint64_t>(name_length) + extra_length + comment_length;
        if (header_rest > stop - at - 46) {
            break;
        }
        entry.name.assign(reinterpret_cast<const char*>(h + 46), name_length);
        // ZIP64: the sizes and the offset that did not fit are in an extra field, in this order.
        const std::uint8_t* extra = h + 46 + name_length;
        for (std::uint32_t e = 0; e + 4 <= extra_length;) {
            const std::uint16_t id = U16(extra + e);
            const std::uint16_t length = U16(extra + e + 2);
            if (length > extra_length - e - 4u) {
                break;
            }
            if (id == 0x0001) {
                const std::uint8_t* v = extra + e + 4;
                const std::uint8_t* v_end = v + length;
                if (entry.size == 0xFFFFFFFF && v + 8 <= v_end) {
                    entry.size = U64(v);
                    v += 8;
                }
                if (entry.compressed_size == 0xFFFFFFFF && v + 8 <= v_end) {
                    entry.compressed_size = U64(v);
                    v += 8;
                }
                if (entry.header_offset == 0xFFFFFFFF && v + 8 <= v_end) {
                    entry.header_offset = U64(v);
                }
            }
            e += 4u + length;
        }
        if (!entry.name.empty() && entry.name.back() != '/') {
            entries.push_back(std::move(entry));
        }
        at += 46u + header_rest;
    }
    if (entries.empty() && count > 0) {
        error = "This zip file is damaged";
        return false;
    }
    by_name.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        by_name[entries[i].name] = i; // a name given twice: the last one
    }
    return true;
}

const ZipArchive::Entry* ZipArchive::Find(const std::string& name) const {
    const auto found = by_name.find(name);
    return found != by_name.end() ? &entries[found->second] : nullptr;
}

bool ZipArchive::Read(const Entry& entry, std::string& out) const {
    out.clear();
    const std::uint64_t size = file.Size();
    if (entry.header_offset > size || size - entry.header_offset < 30 ||
        U32(file.Data() + entry.header_offset) != LocalSignature) {
        return false;
    }
    const std::uint8_t* h = file.Data() + entry.header_offset;
    const std::uint64_t local_rest = static_cast<std::uint64_t>(U16(h + 26)) + U16(h + 28);
    if (local_rest > size - entry.header_offset - 30) {
        return false;
    }
    const std::uint64_t data_at = entry.header_offset + 30 + local_rest;
    if (entry.compressed_size > size - data_at) {
        return false;
    }
    constexpr std::uint64_t MaxEntryBytes = 2ull * 1024 * 1024 * 1024;
    const std::uint64_t max_allowed =
        std::min<std::uint64_t>(MaxEntryBytes, std::numeric_limits<std::size_t>::max());
    const std::uint8_t* data = file.Data() + data_at;
    if (entry.method == 0) {
        if (entry.compressed_size > max_allowed) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(data), static_cast<std::size_t>(entry.compressed_size));
        return true;
    }
    if (entry.method != 8 || entry.size > max_allowed) {
        return false;
    }
    out.resize(static_cast<std::size_t>(entry.size));
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        out.clear();
        return false;
    }
    // zlib counts in 32 bits: a large entry goes through in pieces.
    std::uint64_t in_left = entry.compressed_size;
    std::uint64_t out_left = entry.size;
    std::uint64_t produced = 0;
    stream.next_in = const_cast<Bytef*>(data);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    constexpr std::uint64_t Piece = 1u << 30;
    Bytef trailing = 0;
    bool extra_byte = false;
    int status = Z_OK;
    while (status == Z_OK) {
        if (stream.avail_in == 0 && in_left > 0) {
            stream.avail_in = static_cast<uInt>(std::min(in_left, Piece));
            in_left -= stream.avail_in;
        }
        if (stream.avail_out == 0) {
            if (out_left > 0) {
                stream.avail_out = static_cast<uInt>(std::min(out_left, Piece));
                out_left -= stream.avail_out;
            } else {
                // Allow deflate to consume the final end-of-block bits and flag any overflow byte.
                stream.next_out = &trailing;
                stream.avail_out = 1;
                extra_byte = true;
            }
        }
        const uInt out_before = stream.avail_out;
        status = inflate(&stream, Z_NO_FLUSH);
        const uInt made = out_before - stream.avail_out;
        if (extra_byte && made > 0) {
            status = Z_DATA_ERROR;
            break;
        }
        produced += made;
        if (status == Z_BUF_ERROR && stream.avail_in == 0 && in_left == 0) {
            break;
        }
    }
    const bool whole = status == Z_STREAM_END && produced == entry.size;
    inflateEnd(&stream);
    if (!whole) {
        out.clear();
    }
    return whole;
}

} // namespace Dict
