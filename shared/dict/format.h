// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The engine's files, as written to disk and read back mapped (docs/dictionary-engine.md).
// Every number is little-endian, as on every machine the apps run on.
//
// A dictionary is a folder, written once:
//   info.json       title, revision, author, description, frequency mode, scope, styles, counts
//   strings.bin     every short text once: a varint length, then its UTF-8 bytes; at 0, ""
//   entries.bin     Header, then one EntryRecord per term, in the banks' order
//   defs.idx        Header, then one DefinitionRecord per distinct definition
//   defs.bin        the definitions (zstd frames, or as they were when that is no larger)
//   zdict.bin       the zstd dictionary the frames were made with (when there is one)
//   keys.bin        Header, then KeyRecords for every expression and reading, by hash
//   metas.bin       Header, then one MetaRecord per frequency, pitch or other meta row
//   meta_keys.bin   Header, then KeyRecords for the metas' expressions, by hash
//   media.idx       Header, then MediaRecords, by path
//   media.pack      the pictures' bytes as they were in the zip
//
// The library is a folder of such folders, with:
//   library.json    the dictionaries in their order, which are on, and the next id
//   terms.idx       an index of every dictionary's keys.bin together (IndexHeader...)
//   metas.idx       the same of their meta_keys.bin
//   index.stamp     which dictionaries the two indexes were made of

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace Dict::Format {

static_assert(std::endian::native == std::endian::little, "the engine's files are little-endian");

constexpr std::uint32_t Version = 1;

struct Header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t count;
};
static_assert(sizeof(Header) == 16);

constexpr char EntriesMagic[8] = {'F', 'R', 'S', 'K', 'E', 'N', 'T', 'R'};
constexpr char DefinitionsMagic[8] = {'F', 'R', 'S', 'K', 'D', 'E', 'F', 'S'};
constexpr char KeysMagic[8] = {'F', 'R', 'S', 'K', 'K', 'E', 'Y', 'S'};
constexpr char MetasMagic[8] = {'F', 'R', 'S', 'K', 'M', 'E', 'T', 'A'};
constexpr char MediaMagic[8] = {'F', 'R', 'S', 'K', 'M', 'D', 'I', 'A'};
constexpr char IndexMagic[8] = {'F', 'R', 'S', 'K', 'L', 'I', 'D', 'X'};

/// A term. Texts are offsets into strings.bin.
struct EntryRecord {
    std::uint32_t expression;
    std::uint32_t reading; ///< "" when it is the expression
    std::uint32_t definition_tags;
    std::uint32_t rules;
    std::uint32_t term_tags;
    std::int32_t score;
    std::int64_t sequence;
    std::uint32_t definition; ///< its DefinitionRecord
    std::uint32_t reserved;
};
static_assert(sizeof(EntryRecord) == 40);

/// Where a definition (a glossary, as JSON) is in defs.bin.
struct DefinitionRecord {
    std::uint64_t offset;
    std::uint32_t stored_size;
    std::uint32_t size; ///< its size as JSON; the top bit says it is a zstd frame
};
static_assert(sizeof(DefinitionRecord) == 16);
constexpr std::uint32_t Compressed = 0x80000000u;

/// A term's expression or reading, or a meta's expression, by its hash.
struct KeyRecord {
    std::uint64_t hash;
    std::uint32_t item; ///< an EntryRecord or a MetaRecord
    std::uint32_t reserved;
};
static_assert(sizeof(KeyRecord) == 16);

struct MetaRecord {
    std::uint32_t expression;
    std::uint32_t mode; ///< "freq", "pitch", "ipa"...
    std::uint32_t data; ///< its value as JSON
};
static_assert(sizeof(MetaRecord) == 12);

/// A character of a kanji dictionary (kanji_bank): its readings, tags, meanings (a JSON array)
/// and stats (a JSON object), all offsets into strings.bin. kanji.bin and kanji_keys.bin (its
/// character's hash); kanji_metas.bin and kanji_meta_keys.bin hold a kanji_meta_bank's rows as
/// MetaRecords. A dictionary written before kanji were read has none of these files.
struct KanjiRecord {
    std::uint32_t character;
    std::uint32_t onyomi;
    std::uint32_t kunyomi;
    std::uint32_t tags;
    std::uint32_t meanings;
    std::uint32_t stats;
};
static_assert(sizeof(KanjiRecord) == 24);
constexpr char KanjiMagic[8] = {'F', 'R', 'S', 'K', 'K', 'N', 'J', 'I'};

struct MediaRecord {
    std::uint32_t path;
    std::uint32_t size;
    std::uint64_t offset;
};
static_assert(sizeof(MediaRecord) == 16);

/// The library's index of keys: an open-addressed table of hashes, each with its postings.
struct IndexHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t buckets; ///< a power of two
    std::uint64_t postings;
};
static_assert(sizeof(IndexHeader) == 24);

struct IndexBucket {
    std::uint64_t hash;
    std::uint32_t start; ///< its first Posting
    std::uint32_t count; ///< 0: an empty bucket
};
static_assert(sizeof(IndexBucket) == 16);

struct Posting {
    std::uint32_t dictionary; ///< the dictionary's id
    std::uint32_t item;
};
static_assert(sizeof(Posting) == 8);

/// The hash of a key, the same on every machine (the files keep it).
inline std::uint64_t Mix(std::uint64_t x) {
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93ull;
    x ^= x >> 32;
    x *= 0xd6e8feb86659fd93ull;
    x ^= x >> 32;
    return x;
}

inline std::uint64_t Hash(const void* data, std::size_t size, std::uint64_t seed = 0) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed ^ (static_cast<std::uint64_t>(size) * 0x9e3779b97f4a7c15ull);
    while (size >= 8) {
        std::uint64_t word;
        std::memcpy(&word, p, 8);
        h = (h ^ Mix(word)) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 29;
        p += 8;
        size -= 8;
    }
    std::uint64_t tail = 0;
    if (size > 0) {
        std::memcpy(&tail, p, size);
    }
    h = (h ^ Mix(tail ^ (static_cast<std::uint64_t>(size) << 56))) * 0x9e3779b97f4a7c15ull;
    return Mix(h);
}

inline std::uint64_t Hash(std::string_view text, std::uint64_t seed = 0) {
    return Hash(text.data(), text.size(), seed);
}

/// A string of strings.bin at `offset`.
inline std::string_view StringAt(const std::uint8_t* pool, std::size_t pool_size, std::uint32_t offset) {
    if (offset >= pool_size) {
        return {};
    }
    std::uint32_t length = 0;
    int shift = 0;
    std::size_t at = offset;
    bool terminated = false;
    while (at < pool_size) {
        const std::uint8_t byte = pool[at++];
        if (shift > 28 || (shift == 28 && (byte & 0x70) != 0)) {
            return {};
        }
        length |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            terminated = true;
            break;
        }
        shift += 7;
    }
    if (!terminated || length > pool_size - at) {
        return {};
    }
    return std::string_view(reinterpret_cast<const char*>(pool + at), length);
}

} // namespace Dict::Format
