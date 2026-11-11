// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// The wide picture: for the games with a recipe, a PCSX2 patch (pnach text) that draws the game
// for a screen of any shape, made from the community's 16:9 patch (PCSX2/pcsx2_patches) by
// working out each of its numbers for that shape. The core hands the text to PCSX2
// (Patch::SetExtraPatchSource) with the shape the setting "ps2.wide" names. Nothing here knows
// PCSX2: tools/reader/test-wide.sh builds it alone and checks it against the 16:9 patches.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WidePatches {

/// How one number of a 16:9 patch changes with the screen's shape `a` (width / height).
enum class Law : uint8_t {
    Inverse, ///< as 1 / a: a width scale the game divides by (0.75 at 16:9 where 4:3 had 1)
    With,    ///< as a: a width, a field of view, the aspect itself
    Grow,    ///< as the 16:9 patch changed it from `original`, so many times again (needs testing)
    Fixed,   ///< the patch's own word at every shape (code the patch puts in)
    Only169, ///< the patch's own word at 16:9 only, left out at other shapes (an FMV fix made for 16:9)
};

/// Where the number is in the game's memory and in what form.
enum class Form : uint8_t {
    Word,  ///< a 32-bit float (or, with Fixed and Only169, any word)
    Upper, ///< the upper half of a float in an instruction's low 16 bits (lui reg, 0x3F40)
    Pair,  ///< lui at `address`, ori at `address2`: a whole float in two instructions
};

struct Entry {
    uint32_t address;
    Form form;
    Law law;
    uint32_t word16_9;     ///< the 16:9 patch's word (Pair: the float itself)
    uint32_t original = 0; ///< Grow: the game's own word there (Pair: its float)
    uint32_t address2 = 0; ///< Pair: the ori's address
    uint32_t ops = 0;      ///< Pair: the lui's and the ori's upper halves (0x3C013421: lui at; ori at, at)
    bool own = false;      ///< found here, not in the community's patch (its 16:9 word is ours)
};

struct Recipe {
    const char* serial; ///< SLPM-65790
    uint32_t crc;       ///< the program's CRC, as PCSX2 names its patches
    const char* title;
    const char* notes;  ///< what is known of it at shapes wider than 16:9
    /// Checked by the stretch test (tools/reader/wide-check.sh): the middle of the wide picture
    /// is the 4:3 picture, at these shapes, in these scenes. Empty: not yet.
    const char* checked;
    /// Which draws are the game's 2D, kept to their shape (GSSetReader2DRule's spec): "" the default.
    const char* two_d;
    std::vector<Entry> entries;
};

/// Every recipe there is.
const std::vector<Recipe>& Recipes();

/// The recipe for this game, or none.
const Recipe* Find(const std::string& serial, uint32_t crc);

/// The pnach text for the game at the shape `width`:`height` (as 32 and 9), or "" when it has
/// no recipe or the shape is not wider than 4:3. `trying`, for finding a recipe: words of one's
/// own ("ADDRESS=WORD,ADDRESS=WORD", hexadecimal) written after the recipe's, so they win; with
/// them a game without a recipe gets a group too (READER_WIDE_TRY in the cores' environment).
/// Only a recipe the stretch test passed is used, unless `unchecked` (READER_WIDE_UNCHECKED=1,
/// for checking one).
std::string Pnach(const std::string& serial, uint32_t crc, uint32_t width, uint32_t height, const std::string& trying = {},
                  bool unchecked = false);

/// The 2D rule for the game (its recipe's, or "" for the default; trying: READER_WIDE_2D).
std::string TwoD(const std::string& serial, uint32_t crc);

/// The words a recipe writes at a shape, address by address (for the tests and the pnach).
std::vector<std::pair<uint32_t, uint32_t>> Words(const Recipe& recipe, double aspect);

} // namespace WidePatches
