// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Memcard/MemoryCardFolder.h"
#include "common/FileSystem.h"

#include "reader/core/memory_card.h"

namespace Reader {

/// Formats a blank 8 MB card file as the console does: superblock, indirect FAT list, FAT and
/// an empty root folder, each page with its ECC (the layout of the file system documented in
/// pcsx2/Reference/PS2-MemoryCardFileSystem.htm; the values those of a card the console
/// formatted). Only for a file that was just made blank.
bool FormatCard(const std::string& path) {
    constexpr u32 PageSize = 512;
    constexpr u32 RawPage = 528;
    constexpr u32 Clusters = 8192;
    constexpr u32 FatClusters = 32;  // clusters 9 to 40
    constexpr u32 AllocOffset = 41;
    constexpr u32 AllocEnd = 8135;   // clusters to allocate, from AllocOffset
    std::vector<u8> card(static_cast<std::size_t>(Clusters) * 2 * RawPage, 0xFF);
    // Writes one page's data (512 bytes) and its spare area: ECC over each 128 bytes, then zeros.
    const auto put_page = [&card](u32 page, const u8* data) {
        u8* at = card.data() + static_cast<std::size_t>(page) * RawPage;
        std::memcpy(at, data, PageSize);
        u8 spare[16]{};
        for (int i = 0; i < 4; ++i) {
            FolderMemoryCard::CalculateECC(spare + i * 3, data + i * 128);
        }
        std::memcpy(at + PageSize, spare, sizeof(spare));
    };
    const auto put_cluster = [&put_page](u32 cluster, const std::vector<u8>& data) {
        put_page(cluster * 2, data.data());
        put_page(cluster * 2 + 1, data.data() + PageSize);
    };
    const auto set32 = [](std::vector<u8>& data, std::size_t at, u32 value) {
        std::memcpy(data.data() + at, &value, 4);
    };
    const auto set16 = [](std::vector<u8>& data, std::size_t at, u16 value) {
        std::memcpy(data.data() + at, &value, 2);
    };

    std::vector<u8> super(PageSize, 0xFF);
    static constexpr char magic[] = "Sony PS2 Memory Card Format 1.2.0.0";
    std::memset(super.data(), 0, 0x28);
    std::memcpy(super.data(), magic, sizeof(magic) - 1);
    set16(super, 0x28, PageSize);
    set16(super, 0x2A, 2);    // pages a cluster
    set16(super, 0x2C, 16);   // pages a block
    set16(super, 0x2E, 0xFF00);
    set32(super, 0x30, Clusters);
    set32(super, 0x34, AllocOffset);
    set32(super, 0x38, AllocEnd);
    set32(super, 0x3C, 0);    // the root folder's cluster
    set32(super, 0x40, 1023); // backup blocks
    set32(super, 0x44, 1022);
    std::memset(super.data() + 0x48, 0, 0x08);
    std::memset(super.data() + 0x50, 0, 0x80);
    set32(super, 0x50, 8);    // the indirect FAT list's cluster
    std::memset(super.data() + 0xD0, 0xFF, 0x80); // no bad blocks
    super[0x150] = 2;         // a PS2 card
    super[0x151] = 0x2B;
    static constexpr u8 tail[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                                  0x08, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x41, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(super.data() + 0x152, tail, sizeof(tail));
    put_page(0, super.data());

    std::vector<u8> ifc(PageSize * 2, 0xFF);
    for (u32 i = 0; i < FatClusters; ++i) {
        set32(ifc, i * 4, 9 + i);
    }
    put_cluster(8, ifc);

    // The FAT: the root folder's one cluster ends its chain; every other cluster is free.
    for (u32 c = 0; c < FatClusters; ++c) {
        std::vector<u8> fat(PageSize * 2, 0xFF);
        for (u32 i = 0; i < 256; ++i) {
            const u32 entry = c * 256 + i;
            if (entry < AllocEnd) {
                set32(fat, i * 4, entry == 0 ? 0xFFFFFFFFu : 0x7FFFFFFFu);
            }
        }
        put_cluster(9 + c, fat);
    }

    // The root folder: "." and "..", made now.
    std::vector<u8> root(PageSize * 2, 0x00);
    const std::time_t now = std::time(nullptr);
    std::tm when{};
#ifdef _WIN32
    gmtime_s(&when, &now);
#else
    gmtime_r(&now, &when);
#endif
    const auto stamp = [&](std::size_t at) {
        root[at] = 0;
        root[at + 1] = static_cast<u8>(when.tm_sec);
        root[at + 2] = static_cast<u8>(when.tm_min);
        root[at + 3] = static_cast<u8>(when.tm_hour);
        root[at + 4] = static_cast<u8>(when.tm_mday);
        root[at + 5] = static_cast<u8>(when.tm_mon + 1);
        set16(root, at + 6, static_cast<u16>(when.tm_year + 1900));
    };
    set16(root, 0x000, 0x8427);
    set32(root, 0x004, 2);
    stamp(0x008);
    stamp(0x018);
    root[0x040] = '.';
    set16(root, 0x200, 0xA426);
    stamp(0x208);
    stamp(0x218);
    root[0x240] = '.';
    root[0x241] = '.';
    put_cluster(AllocOffset, root);

    auto fp = FileSystem::OpenManagedCFile(path.c_str(), "r+b");
    if (!fp) {
        return false;
    }
    return std::fwrite(card.data(), card.size(), 1, fp.get()) == 1;
}

} // namespace Reader
