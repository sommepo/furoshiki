// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include "wide_patches.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace WidePatches {

namespace {

constexpr double Aspect4_3 = 4.0 / 3.0;
constexpr double Aspect16_9 = 16.0 / 9.0;

float FloatOf(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t BitsOf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// The value of a number at the shape `aspect`, from its 16:9 value.
double ValueAt(Law law, double at16_9, double original, double aspect) {
    switch (law) {
    case Law::Inverse:
        return at16_9 * Aspect16_9 / aspect;
    case Law::With:
        return at16_9 * aspect / Aspect16_9;
    case Law::Grow: {
        // So many times the 16:9 patch's change as the shape is so many times 16:9's step from 4:3.
        const double times = std::log(aspect / Aspect4_3) / std::log(Aspect16_9 / Aspect4_3);
        return original * std::pow(at16_9 / original, times);
    }
    default:
        return at16_9;
    }
}

uint32_t Upper(uint32_t bits) {
    return (bits + 0x8000u) >> 16; // the nearest float a lui can make
}

using E = Entry;
constexpr Form W = Form::Word;
constexpr Form U = Form::Upper;
constexpr Form P = Form::Pair;
constexpr Law Inv = Law::Inverse;
constexpr Law With = Law::With;
constexpr Law Grow = Law::Grow;
constexpr Law Fix = Law::Fixed;
constexpr Law Only = Law::Only169;

/// A Pair's entry: lui at `lui`, ori at `ori`, on the registers `ops` names (0x3C013421: lui
/// at; ori at, at), the float `value` at 16:9.
E PairOf(uint32_t lui, uint32_t ori, Law law, uint32_t value, uint32_t ops, uint32_t original = 0) {
    E entry{lui, P, law, value, original, ori};
    entry.ops = ops;
    return entry;
}

} // namespace

const std::vector<Recipe>& Recipes() {
    static const std::vector<Recipe> recipes = {
        // Arapapa's 16:9 patch; Kakuremino (tools/aspect.py) found the scale is (4/3) / aspect.
        {"SLPM-67002", 0xCED0851A, "METAL GEAR SOLID 2 SUBSTANCE",
         "At 32:9 the codec's left face is missing from its box (right at 21:9; not found why). The weapon and item menus "
         "were seen at their own shape but not measured. Not checked: the pause screen, first person, alert HUD, boss "
         "and O2 gauges, title and save screens, films, Snake Tales. With Kakuremino's add-on (picture.aspect stretch) "
         "this recipe stays off and the add-on's own HUD fit is used. Its 2D is FST without a depth test, its life gauge "
         "untextured triangles (prims=all); the radar's lines have a frame of their own (also=64C0), the codec's faces "
         "are 3D in frames of their own (move=79A0|6660).",
         "32:9 and 21:9: a bridge cutscene with subtitles, two moments on the Tanker's aft deck with the radar and "
         "weapon box, a codec call, the Plant's dock (SSIM 0.79-0.99): the middle is the 4:3 picture, the 2D at its own "
         "shape, the radar's map in its box",
         "prims=all,fst,frames=0|1000,also=64C0,move=79A0|6660",
         {{0x0011FEE0, U, Inv, 0x3C013F40}}},
        // synce's 16:9 patch: the camera's x scale (1.0 in the game), read only by the camera function
        // at 0x127E58, which builds the frustum from the same matrix (culling widens too). Its 2D is
        // every FST primitive, depth-tested, into the two shown buffers (0 and 0x1000); 0x2000 is the
        // blur buffer, which the HUD's textures sit beside.
        {"SLPM-65790", 0x0481AD8A, "METAL GEAR SOLID 3 SNAKE EATER",
         "Not seen: cutscenes and their bars, water (the US patch's per-stage water fix is not ported), the item and weapon "
         "menus held, first person, the radar and alert HUD, other stages. At 32:9 and 1x resolution the small HUD text is "
         "blocky (a higher internal resolution helps).",
         "32:9 and 21:9: the codec after the landing, two places in Dremuchij South in play with the HUD, the survival "
         "viewer (SSIM 0.90-0.96); the HUD, codec and menu at their own shape",
         "depth,fst,frames=0|1000",
         {{0x0020257C, W, Inv, 0x3F400000}}},
        // synce's 16:9 patch: the main camera's aspect (a camera at 0x2F5030: +4 the aspect 1.165, +0xC
        // the vertical field of view, +0x18 512x448), read by the projection at 0x2B96F8, which widens the
        // view horizontally only. Its 2D: FST sprites, depth-tested (the message window, speaker, text).
        {"SLPM-65462", 0x0E762E8D, "真・女神転生III NOCTURNE マニアクス",
         "Not checked: battles (a second camera at 0x2F53D0 may be theirs). The main menu's 3D tunnel stays stretched "
         "(drawn another way, not found). At 1x resolution the squeezed text is aliased.",
         "32:9 and 21:9: the Yoyogi Park station field with the HUD (two views), the opening's voice scene with its "
         "message window, the name entry, the field map's letters (SSIM 0.72-0.98): the middle is the 4:3 picture, "
         "the 2D at its own shape",
         "depth,fst,each",
         {{0x002F5034, W, With, 0x3FC65461}}},
        // The same camera struct at 0x324260 (projection 0x2E1388). Its HUD, place names and the films'
        // subtitles are depth-tested FST draws.
        {"SLPM-65597", 0x44865CE1, "DIGITAL DEVIL SAGA アバタール・チューナー",
         "Not checked: battles, a field message window. Films and the title's background are pictures and stay "
         "stretched (their subtitles keep their shape).",
         "32:9 and 21:9: the base (作戦室) with the HUD and minimap (SSIM 0.91-0.96), the opening film's subtitles "
         "and bars: the middle is the 4:3 picture, the 2D at its own shape",
         "prims=all,depth=flat,fst,each",
         {{0x00324264, W, With, 0x3FC65461}}},
        // kerostar's 16:9 patch, without Arapapa's font fix (it drew the font narrower so that it kept
        // its shape when stretched; with the GS's 2D squeeze it narrowed the text twice).
        // 0x81B5E0 is the room and field camera's x factor (a block at 0x81B5C4: 512x448, 30 degrees);
        // 0x81A8D0 a second camera's (640x448, 40 degrees; likely the world map); the pair the battle
        // camera's (1.078, stored at +0x1B0 of its object). Its 2D: textured FST sprites, depth-tested;
        // the message window is 94 % of the width (fills=tex), the clears untextured.
        {"SLPM-65555", 0xB64E9939, "ドラゴンクエストV 天空の花嫁",
         "Not checked: the second camera (0x81A8D0) and the battle camera (no battle reached), films, fades, the "
         "birth scene's iris, culling at the edges. At 1x resolution the squeezed text is aliased.",
         "32:9 and 21:9: the birth room with a message, the ship's cabin, the cabin with the command menu and status, "
         "the deck, the name entry (SSIM 0.74-0.97): the middle is the 4:3 picture, the 2D at its own shape",
         "depth,fills=tex",
         {{0x0081B5E0, W, Inv, 0x3F400000},
          {0x0081A8D0, W, Inv, 0x3F400000},
          PairOf(0x0057BD14, 0x0057BD1C, Inv, 0x3F4EF9DA, 0x3C033463)}},
        // The camera's horizontal factor is data, 0.8 at 0x313F80 (the 16:9 patch has it as an
        // "alternate value"), read by the field camera; its code twin at 0x1A5854 is another
        // camera's. The 16:9 patch's other numbers (0x313F84, the vertical factor it lowers to look
        // like the HD remaster; 500 at 0x11973C and 512 at 0x176124, sizes of something on the
        // 512x416 screen) are left out: the stretch test passes without them.
        {"SLPS-25088", 0x658597E2, "ファイナルファンタジーX インターナショナル",
         "Still stretched: the title screen (one full-screen picture, drawn like the 3D's backdrop) and the opening's "
         "staff credits; films. Not reached: battles, the main menu, the sphere grid, the world map (their values are "
         "the 16:9 patch's, not checked). Its 2D is never depth-tested and always FST; its character models are FST "
         "too but depth-tested, so depth must stay out of its rule. At 32:9 and 1x resolution the text is badly aliased.",
         "32:9 and 21:9: the ruins by the fire, Zanarkand at night, the pier, a subtitle in the opening, a fan's speech "
         "over 3D, the field with the crowd, the new game's notice windows, the name entry (SSIM 0.86-0.97): the middle "
         "is the 4:3 picture, subtitles, windows and the menu at their own shape",
         "each,fst",
         {{0x00313F80, W, Inv, 0x3F19999A, 0, 0, 0, true},
          PairOf(0x001A5854, 0x001A5858, Inv, 0x3F19999A, 0x3C013421),
          {0x00262E40, U, Inv, 0x3C013F40},
          {0x0026479C, U, Inv, 0x3C013F40}}},
        // From nemesis2000's 16:9 patch (NTSC-K by Arapapa), what the 3D needs: the projection's horizontal
        // factor 0.88 (data), which 0x178810 multiplies by 512 into 0x10DE614 only when a camera is set up
        // (so that word too, as a savestate keeps the old one), and the fog layer's half-width at the far
        // plane (512.0, a lui in both branches of 0x187BD0; the patch has only the first, the car park
        // takes the second). Left out: its film fix (16:9's own crop), lens flares, the loading screen's
        // footprints and the item views (not reached). Its 2D is never depth-tested and its 3D always is;
        // it streams textures through its three buffers (nocopy), and its menu is 93 % of the width while
        // its tiled overlay, fog and frame copies are the whole width (full=98).
        {"SLPM-65051", 0xE36E16C9, "サイレントヒル2",
         "Not checked: films (stretched), lens flares, the loading screen, the item views' 3D models, the radio and map. "
         "At 32:9 the fixed cameras see past some walls (grey beyond the restroom's mirror wall).",
         "32:9 and 21:9: the restroom (the mirror, its far end), the car park in play, the letter scene and a subtitle, "
         "the item menu (SSIM 0.75-0.93): the middle is the 4:3 picture, fog across the width, subtitles and menu at "
         "their own shape",
         "prims=all,nocopy,full=98",
         {{0x002CA060, W, Inv, 0x3F28F5C3},
          {0x010DE614, W, Inv, 0x43A8F5C3, 0, 0, 0, true},
          {0x00187C44, U, With, 0x3C034455},
          {0x00187C88, U, With, 0x3C034455, 0, 0, 0, true}}},
        // From nemesis2000's 16:9 patches (by Arapapa and pavachan), the projection's aspect alone (4/3
        // in the game, read through $gp by the camera's field-of-view setter, which sets the view window
        // x = aspect * tan(fov/2)). Their HUD, film, art and zoom fixes are left out: with the GS's 2D
        // squeeze they would narrow twice. The 2D of both games is triangle fans (PRIM 5), depth-tested
        // like the 3D, which is strips; message windows are nearly as wide as the picture (full=cover),
        // full-screen stills are pillarboxed (fills=tex). A field camera sets its window when a room is
        // entered, so a savestate made at 4:3 keeps the narrow view until the next room.
        {"SLPM-66445", 0x05C3D28F, "ペルソナ3",
         "Not checked: battles, Persona art, the field menu. fills=tex would squeeze a textured full-screen effect "
         "over 3D, if there is one. Films are pillarboxed at their own shape (seen at 32:9).",
         "32:9 and 21:9: the dorm hallway with a message and portrait, the dorm hall, the school's entrance hall in free "
         "movement with the HUD, the name entry (SSIM 0.94-0.98): the middle is the 4:3 picture, the 2D at its own shape",
         "prim=5,depth,full=cover,fills=tex",
         {{0x007B179C, W, With, 0x3FE38E38}}},
        {"SLPM-66978", 0x4E63E63C, "ペルソナ4",
         "Not checked: battles, Persona art, the field menu. fills=tex would squeeze a textured full-screen effect "
         "over 3D, if there is one. Films are pillarboxed at their own shape (seen at 32:9).",
         "32:9 and 21:9: the Velvet Room with a message and portrait, the arrival street with the date, the Dojima house "
         "in free movement with the HUD (two views), the name entry, the title (SSIM 0.94-0.99): the middle is the 4:3 "
         "picture, the 2D at its own shape",
         "prim=5,depth,full=cover,fills=tex",
         {{0x0076F61C, W, With, 0x3FE38E38}}},
    };
    return recipes;
}

const Recipe* Find(const std::string& serial, uint32_t crc) {
    for (const Recipe& recipe : Recipes()) {
        if (serial == recipe.serial && crc == recipe.crc) {
            return &recipe;
        }
    }
    return nullptr;
}

std::string TwoD(const std::string& serial, uint32_t crc) {
    const Recipe* recipe = Find(serial, crc);
    return recipe != nullptr ? recipe->two_d : "";
}

std::vector<std::pair<uint32_t, uint32_t>> Words(const Recipe& recipe, double aspect) {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    const bool at16_9 = std::fabs(aspect - Aspect16_9) < 1e-4;
    for (const Entry& entry : recipe.entries) {
        if (entry.law == Law::Only169 && !at16_9) {
            continue;
        }
        const bool as_is = entry.law == Law::Fixed || entry.law == Law::Only169;
        switch (entry.form) {
        case Form::Word: {
            const uint32_t word = as_is ? entry.word16_9
                                        : BitsOf(static_cast<float>(ValueAt(entry.law, FloatOf(entry.word16_9),
                                                                            FloatOf(entry.original), aspect)));
            out.emplace_back(entry.address, word);
            break;
        }
        case Form::Upper: {
            uint32_t half = entry.word16_9 & 0xFFFFu;
            if (!as_is) {
                const double value = ValueAt(entry.law, FloatOf(half << 16), FloatOf((entry.original & 0xFFFFu) << 16), aspect);
                half = Upper(BitsOf(static_cast<float>(value)));
            }
            out.emplace_back(entry.address, (entry.word16_9 & 0xFFFF0000u) | (half & 0xFFFFu));
            break;
        }
        case Form::Pair: {
            const uint32_t bits = as_is ? entry.word16_9
                                        : BitsOf(static_cast<float>(ValueAt(entry.law, FloatOf(entry.word16_9),
                                                                            FloatOf(entry.original), aspect)));
            out.emplace_back(entry.address, (entry.ops & 0xFFFF0000u) | (bits >> 16));
            out.emplace_back(entry.address2, (entry.ops << 16) | (bits & 0xFFFFu));
            break;
        }
        }
    }
    return out;
}

std::string Pnach(const std::string& serial, uint32_t crc, uint32_t width, uint32_t height, const std::string& trying,
                  bool unchecked) {
    const Recipe* recipe = Find(serial, crc);
    if (recipe != nullptr && !unchecked && recipe->checked[0] == '\0') {
        recipe = nullptr; // not yet through the stretch test: it may stretch, or write where it should not
    }
    if ((recipe == nullptr && trying.empty()) || width == 0 || height == 0) {
        return {};
    }
    const double aspect = static_cast<double>(width) / height;
    if (aspect <= Aspect4_3 + 1e-4) {
        return {};
    }
    char line[96];
    std::string text = std::string("gametitle=") + (recipe != nullptr ? recipe->title : serial.c_str()) + " (" + serial + ")\n\n";
    std::snprintf(line, sizeof(line), "[Furoshiki wide %u:%u]\ngsaspectratio=%u:%u\n", width, height, width, height);
    text += line;
    text += "author=Furoshiki, from the community's 16:9 patch (PCSX2/pcsx2_patches)\n";
    if (recipe != nullptr) {
        for (const auto& [address, word] : Words(*recipe, aspect)) {
            std::snprintf(line, sizeof(line), "patch=1,EE,%08X,word,%08X\n", address, word);
            text += line;
        }
    }
    // Words being tried, after the recipe's (a later write to the same place wins).
    size_t at = 0;
    while (at < trying.size()) {
        size_t end = trying.find(',', at);
        if (end == std::string::npos) {
            end = trying.size();
        }
        const std::string item = trying.substr(at, end - at);
        const size_t equals = item.find('=');
        if (equals != std::string::npos) {
            const unsigned long address = std::strtoul(item.substr(0, equals).c_str(), nullptr, 16);
            const unsigned long word = std::strtoul(item.substr(equals + 1).c_str(), nullptr, 16);
            std::snprintf(line, sizeof(line), "patch=1,EE,%08lX,word,%08lX\n", address, word);
            text += line;
        }
        at = end + 1;
    }
    return text;
}

} // namespace WidePatches
