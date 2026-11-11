// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// The wide picture's recipes (wide_patches.cpp), without PCSX2: tools/reader/test-wide.sh.
//   wide_patches_test [PATCHES_FOLDER]
// With a folder of the community's pnach files (PCSX2's patches.zip unpacked), each recipe's
// 16:9 words are also checked against its patch's, line by line.

#include "../wide_patches.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

std::map<uint32_t, uint32_t> WordsAt(const WidePatches::Recipe& recipe, double aspect) {
    std::map<uint32_t, uint32_t> out;
    for (const auto& [address, word] : WidePatches::Words(recipe, aspect)) {
        out[address] = word;
    }
    return out;
}

float FloatOf(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string Hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", value);
    return text;
}

/// The words a community pnach's "[Widescreen 16:9]" group writes, address by address.
std::map<uint32_t, uint32_t> PatchWords(const std::string& path) {
    std::map<uint32_t, uint32_t> out;
    std::ifstream file(path);
    std::string line;
    bool in_group = false;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] == '[') {
            in_group = line.rfind("[Widescreen 16:9]", 0) == 0;
            continue;
        }
        if (!in_group || line.rfind("patch=", 0) != 0) {
            continue;
        }
        // patch=1,EE,ADDRESS,word|extended,VALUE
        std::stringstream parts(line.substr(6));
        std::string place, cpu, address, type, value;
        std::getline(parts, place, ',');
        std::getline(parts, cpu, ',');
        std::getline(parts, address, ',');
        std::getline(parts, type, ',');
        std::getline(parts, value, ',');
        uint32_t at = static_cast<uint32_t>(std::stoul(address, nullptr, 16));
        at &= 0x0FFFFFFF; // extended's 0x2xxxxxxx is a 32-bit write; word's 0x2xxxxxxx main memory uncached
        out[at] = static_cast<uint32_t>(std::stoul(value.substr(0, 8), nullptr, 16));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    using namespace WidePatches;
    // Every recipe at 16:9 is the community's patch, word for word (as the recipe has it).
    for (const Recipe& recipe : Recipes()) {
        const auto at16_9 = WordsAt(recipe, 16.0 / 9.0);
        for (const Entry& entry : recipe.entries) {
            if (entry.form == Form::Pair) {
                Check(at16_9.at(entry.address) >> 16 == entry.ops >> 16 &&
                          ((at16_9.at(entry.address) & 0xFFFF) << 16 | (at16_9.at(entry.address2) & 0xFFFF)) == entry.word16_9,
                      std::string(recipe.serial) + " pair " + Hex(entry.address) + " at 16:9");
            } else {
                Check(at16_9.count(entry.address) && at16_9.at(entry.address) == entry.word16_9,
                      std::string(recipe.serial) + " " + Hex(entry.address) + " at 16:9: " +
                          (at16_9.count(entry.address) ? Hex(at16_9.at(entry.address)) : "none") + " not " + Hex(entry.word16_9));
            }
        }
        // Wider: what is 16:9's own is left out, the rest is there.
        const auto at32_9 = WordsAt(recipe, 32.0 / 9.0);
        for (const Entry& entry : recipe.entries) {
            Check(at32_9.count(entry.address) == (entry.law == Law::Only169 ? 0u : 1u),
                  std::string(recipe.serial) + " " + Hex(entry.address) + " at 32:9 present");
        }
        // A game's own value comes back at 4:3 where it is known.
        const auto at4_3 = WordsAt(recipe, 4.0 / 3.0);
        for (const Entry& entry : recipe.entries) {
            if (entry.law == Law::Grow && entry.form == Form::Word) {
                Check(at4_3.at(entry.address) == entry.original, std::string(recipe.serial) + " " + Hex(entry.address) + " at 4:3");
            }
            if (entry.law == Law::Grow && entry.form == Form::Upper) {
                Check(at4_3.at(entry.address) == entry.original, std::string(recipe.serial) + " " + Hex(entry.address) + " at 4:3");
            }
        }
    }

    // Values worked out by hand: MGS3's scale is 1.0 in the game, 0.375 at 32:9 (tried in the
    // game); MGS2's lui is 0x3F80 in the game, Kakuremino's 0x3EC0 at 32:9; Nocturne's aspect
    // 1.165 in the game, twice the 16:9 patch's at 32:9.
    const Recipe* mgs3 = Find("SLPM-65790", 0x0481AD8A);
    const Recipe* mgs2 = Find("SLPM-67002", 0xCED0851A);
    const Recipe* nocturne = Find("SLPM-65462", 0x0E762E8D);
    const Recipe* ffx = Find("SLPS-25088", 0x658597E2);
    Check(mgs3 && WordsAt(*mgs3, 32.0 / 9.0).at(0x0020257C) == 0x3EC00000, "MGS3 at 32:9");
    Check(mgs3 && WordsAt(*mgs3, 4.0 / 3.0).at(0x0020257C) == 0x3F800000, "MGS3 at 4:3 is the game's");
    Check(mgs2 && WordsAt(*mgs2, 32.0 / 9.0).at(0x0011FEE0) == 0x3C013EC0, "MGS2 at 32:9 is Kakuremino's");
    Check(mgs2 && WordsAt(*mgs2, 4.0 / 3.0).at(0x0011FEE0) == 0x3C013F80, "MGS2 at 4:3 is the game's");
    Check(nocturne && std::fabs(FloatOf(WordsAt(*nocturne, 32.0 / 9.0).at(0x002F5034)) - 2 * FloatOf(0x3FC65461)) < 1e-5,
          "Nocturne at 32:9");
    // (The 16:9 patch's 1.5494 is the game's 1.165 times 1.33, not quite 4/3: within 0.5 %.)
    Check(nocturne && std::fabs(FloatOf(WordsAt(*nocturne, 4.0 / 3.0).at(0x002F5034)) / FloatOf(0x3F951EB8) - 1) < 0.005,
          "Nocturne at 4:3 is the game's");
    // FFX's camera, x: 0.8 in the game (read there), 0.3 at 32:9 (the stretch test's value).
    Check(ffx && WordsAt(*ffx, 32.0 / 9.0).at(0x00313F80) == 0x3E99999A, "FFX camera at 32:9");
    Check(ffx && WordsAt(*ffx, 4.0 / 3.0).at(0x00313F80) == 0x3F4CCCCD, "FFX camera at 4:3 is the game's");
    // Its other camera, in code: the same.
    if (ffx) {
        const auto words = WordsAt(*ffx, 32.0 / 9.0);
        const uint32_t bits = (words.at(0x001A5854) & 0xFFFF) << 16 | (words.at(0x001A5858) & 0xFFFF);
        Check(std::fabs(FloatOf(bits) - 0.3f) < 1e-6, "FFX x field of view at 32:9");
        const auto own = WordsAt(*ffx, 4.0 / 3.0);
        const uint32_t own_bits = (own.at(0x001A5854) & 0xFFFF) << 16 | (own.at(0x001A5858) & 0xFFFF);
        Check(std::fabs(FloatOf(own_bits) - 0.8f) < 1e-6, "FFX x field of view at 4:3 is the game's");
    }

    // Only a checked recipe is used, unless asked for.
    Check(!Pnach("SLPM-65790", 0x0481AD8A, 32, 9).empty(), "MGS3 is checked");
    Check(TwoD("SLPM-65790", 0x0481AD8A) == "depth,fst,frames=0|1000", "MGS3's 2D rule");
    Check(!Pnach("SLPS-25088", 0x658597E2, 32, 9).empty(), "a checked recipe is used");
    // The text: a group of its own with the shape, nothing for 4:3 or a game without a recipe.
    const std::string text = Pnach("SLPM-65790", 0x0481AD8A, 32, 9, {}, true);
    Check(text.find("[Furoshiki wide 32:9]") != std::string::npos, "group name");
    Check(text.find("gsaspectratio=32:9") != std::string::npos, "aspect line");
    Check(text.find("patch=1,EE,0020257C,word,3EC00000") != std::string::npos, "patch line");
    Check(Pnach("SLPM-65790", 0x0481AD8A, 4, 3, {}, true).empty(), "nothing at 4:3");
    Check(Pnach("SLPM-65790", 0x12345678, 32, 9, {}, true).empty(), "nothing for another program");
    Check(Pnach("SLPS-25256", 0x9D260CAF, 32, 9, {}, true).empty(), "nothing for a game without a recipe");
    // Words being tried: after the recipe's, and a group even for a game without one.
    Check(Pnach("SLPS-25256", 0x9D260CAF, 32, 9, "313F80=3E99999A").find("patch=1,EE,00313F80,word,3E99999A") != std::string::npos,
          "words being tried");

    // Against the community's files, when given.
    if (argc > 1) {
        int compared = 0;
        for (const Recipe& recipe : Recipes()) {
            char name[64];
            std::snprintf(name, sizeof(name), "/%s_%08X.pnach", recipe.serial, recipe.crc);
            const auto patch = PatchWords(std::string(argv[1]) + name);
            Check(!patch.empty(), std::string("community patch for ") + recipe.serial);
            const auto ours = WordsAt(recipe, 16.0 / 9.0);
            std::map<uint32_t, bool> own;
            for (const Entry& entry : recipe.entries) {
                own[entry.address] = entry.own;
            }
            for (const auto& [address, word] : ours) {
                if (own[address]) {
                    continue; // found here: the patch does not have it
                }
                Check(patch.count(address) && patch.at(address) == word,
                      std::string(recipe.serial) + " " + Hex(address) + " " + Hex(word) + " vs the patch's " +
                          (patch.count(address) ? Hex(patch.at(address)) : "none"));
                ++compared;
            }
        }
        std::printf("%d words compared with the community's patches\n", compared);
    }

    std::printf("%s (%d failures)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
