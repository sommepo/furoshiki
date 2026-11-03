// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include <cstring>
#include <map>
#include <optional>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <QDir>
#include <QFile>
#include "reader/core/engine.h"
#include "reader/desktop/catalog.h"

// Touch play (READER.md, "Playing by touch"): a profile says which answer of a choice the game's
// cursor is on (reader.cursor), which a tap on an answer is turned into presses of the d-pad
// with. First the engine's side, then each visual novel's profile against made-up memory laid
// out as the game's (no game data: the texts are made up, the places are the profile's own).

using Catch::Matchers::ContainsSubstring;

namespace {

/// The engine, detached, with made-up main memory and a program of the given build.
struct MadeUpGame {
    std::string memory = std::string(32 * 1024 * 1024, '\0');
    Reader::Engine& engine = Reader::Engine::Instance();

    explicit MadeUpGame(const std::string& build = {}, const std::string& game_id = "MADE-00001") {
        engine.StartDetached(
            [this](u64 address, std::size_t size) {
                return address < memory.size() ? memory.substr(address, size) : std::string{};
            },
            game_id);
        if (!build.empty()) {
            engine.OnModuleLoaded("main", 0x100000, 0x400000, build);
        }
    }
    ~MadeUpGame() {
        engine.StopDetached();
    }

    bool Load(const std::string& source) {
        return engine.LoadProfile("made-up", source);
    }
    /// One of the bundled profiles, as it is in the repository.
    bool LoadBundled(const std::string& serial) {
        std::ifstream file(std::string(READER_BUNDLED_PROFILE_DIR) + "/" + serial + ".lua",
                           std::ios::binary);
        std::stringstream source;
        source << file.rdbuf();
        const bool loaded = engine.LoadProfile(serial, source.str());
        if (!loaded) {
            UNSCOPED_INFO(engine.LastError());
        }
        return loaded;
    }
    std::string Lua(const std::string& code) {
        std::string out;
        engine.Execute(code, out);
        return out;
    }
    void Frames(int count = 1) {
        for (int i = 0; i < count; ++i) {
            engine.OnFrame();
        }
    }
    void Put(u32 address, const std::string& bytes) {
        std::memcpy(memory.data() + address, bytes.data(), bytes.size());
    }
    template <typename T>
    void Set(u32 address, T value) {
        std::memcpy(memory.data() + address, &value, sizeof(value));
    }
    /// The cursor the profile reports: the answer, "-1" for none yet, "none" without a choice;
    /// with " sideways" when the answers stand side by side.
    std::string Cursor() const {
        const auto cursor = engine.Cursor();
        if (!cursor) {
            return "none";
        }
        return std::to_string(cursor->index) + (cursor->horizontal ? " sideways" : "");
    }
};

/// Shift-JIS of a few kana and signs, for made-up answers (the profiles decode with text.sjis).
std::string Sjis(const std::string& name) {
    if (name == "hai") {
        return "\x82\xcd\x82\xa2"; // はい
    }
    if (name == "iie") {
        return "\x82\xa2\x82\xa2\x82\xa6"; // いいえ
    }
    return "\x82\xa0\x81\x42"; // あ。
}

} // namespace

TEST_CASE("A profile says which answer of a choice the cursor is on", "[touch]") {
    MadeUpGame game;
    REQUIRE(game.Load("-- reader: no hooks\n"));
    CHECK(game.Cursor() == "none");
    CHECK(game.Lua("reader.cursor(2)").empty());
    CHECK(game.Cursor() == "2");
    game.Lua("reader.cursor(-1, 'horizontal')");
    CHECK(game.Cursor() == "-1 sideways");
    game.Lua("reader.cursor(nil)");
    CHECK(game.Cursor() == "none");
    CHECK_THAT(game.Lua("reader.cursor(-2)"), ContainsSubstring("an answer"));
    CHECK_THAT(game.Lua("reader.cursor(1, 'diagonal')"), ContainsSubstring("horizontal"));
    game.Lua("reader.cursor(0)");
    // A profile loaded anew, or none, says nothing of the last one's cursor.
    REQUIRE(game.Load("-- reader: no hooks\n"));
    CHECK(game.Cursor() == "none");
    game.Lua("reader.cursor(1)");
    game.engine.UnloadProfile();
    CHECK(game.Cursor() == "none");
}

TEST_CASE("Never7: the cursor is on no answer until the d-pad is pressed, then on the one it says",
          "[touch][profiles]") {
    MadeUpGame game{"9D260CAF", "SLPS-25256"};
    REQUIRE(game.LoadBundled("SLPS-25256"));
    constexpr u32 answer_text = 0x5cbe57, answer_size = 66, answer_count = 0x398630;
    constexpr u32 choice_box = 0x397f4c, cursor = 0x53d896;
    game.Frames(2);
    CHECK(game.Cursor() == "none");
    // A choice of two comes up, nothing lit yet (0 kept from no earlier choice).
    game.Put(answer_text, Sjis("hai"));
    game.Put(answer_text + answer_size, Sjis("iie"));
    game.Set<u32>(answer_count, 2);
    game.Set<u32>(choice_box, 240);
    game.Frames();
    CHECK(game.Cursor() == "-1");
    game.Set<u16>(cursor, 1); // the first press of the d-pad: the first answer
    game.Frames();
    CHECK(game.Cursor() == "0");
    game.Set<u16>(cursor, 2);
    game.Frames();
    CHECK(game.Cursor() == "1");
    game.Set<u16>(cursor, 7); // not an answer of this choice
    game.Frames();
    CHECK(game.Cursor() == "-1");
    // Picked: the box goes, the number stays in memory.
    game.Set<u16>(cursor, 2);
    game.Set<u32>(choice_box, 0);
    game.Frames();
    CHECK(game.Cursor() == "none");
    // The next choice begins on no answer whatever the last one left (the game sets 0).
    game.Set<u16>(cursor, 0);
    game.Put(answer_text + 2 * answer_size, Sjis("a"));
    game.Set<u32>(answer_count, 3);
    game.Set<u32>(choice_box, 96);
    game.Frames();
    CHECK(game.Cursor() == "-1");
}

namespace {
/// Ever17 and Remember11: one engine, the same rules at each program's places.
void KidChoiceCursor(const std::string& serial, const std::string& build, u32 answers, u32 cursor) {
    MadeUpGame game{build, serial};
    REQUIRE(game.LoadBundled(serial));
    const u32 answer_text = answers + 8, answer_size = 64;
    game.Frames(2);
    CHECK(game.Cursor() == "none");
    game.Set<u8>(cursor, 0xff); // on no answer until the d-pad is first pressed
    game.Put(answer_text, Sjis("hai"));
    game.Put(answer_text + answer_size, Sjis("iie"));
    game.Put(answer_text + 2 * answer_size, Sjis("a"));
    game.Set<u32>(answers, 3);
    game.Frames();
    CHECK(game.Cursor() == "-1");
    game.Set<u8>(cursor, 0);
    game.Frames();
    CHECK(game.Cursor() == "0");
    game.Set<u8>(cursor, 2);
    game.Frames();
    CHECK(game.Cursor() == "2");
    game.Set<u8>(cursor, 3); // past the last answer: not one of them
    game.Frames();
    CHECK(game.Cursor() == "-1");
    game.Set<u8>(cursor, 1);
    game.Set<u32>(answers, 0); // picked: the choice is gone, the number stays
    game.Frames();
    CHECK(game.Cursor() == "none");
}
} // namespace

TEST_CASE("Ever17: the cursor is a byte beside the choice, 0xff until the d-pad is pressed",
          "[touch][profiles]") {
    KidChoiceCursor("SLPM-65421", "C577D399", 0xc2b648, 0xc2b572);
}

TEST_CASE("Remember11: the cursor is a byte beside the choice, 0xff until the d-pad is pressed",
          "[touch][profiles]") {
    KidChoiceCursor("SLPM-65550", "B33522CD", 0xb17ea0, 0xb17dca);
}

TEST_CASE("CLANNAD: the cursor is reported while a choice waits, 0xffff being none",
          "[touch][profiles]") {
    MadeUpGame game{"7800DC84", "SLPM-66302"};
    REQUIRE(game.LoadBundled("SLPM-66302"));
    constexpr u32 state = 0x3775b8, count = 0x3775b0, answers = 0x377290, cursor = 0x60cdda;
    game.Frames(2);
    CHECK(game.Cursor() == "none");
    game.Put(answers, Sjis("hai"));
    game.Put(answers + 100, Sjis("iie"));
    game.Set<u16>(count, 2);
    game.Set<u16>(cursor, 0xffff);
    game.Frames();
    CHECK(game.Cursor() == "none"); // the answers are there, but no choice waits yet
    game.Set<u32>(state, 0x10);
    game.Frames();
    CHECK(game.Cursor() == "-1");
    game.Set<u16>(cursor, 0);
    game.Frames();
    CHECK(game.Cursor() == "0");
    game.Set<u16>(cursor, 1);
    game.Frames();
    CHECK(game.Cursor() == "1");
    game.Set<u32>(state, 0x501); // picked: the script goes on, the number stays
    game.Frames();
    CHECK(game.Cursor() == "none");
}

TEST_CASE("Kanon: the cursor is reported while a choice waits, the first answer lit at once",
          "[touch][profiles]") {
    MadeUpGame game{"050D0F4F", "SLPS-20121"};
    REQUIRE(game.LoadBundled("SLPS-20121"));
    constexpr u32 choosing = 0x2331d8, cursor = 0x2331ec, count = 0x233248, answers = 0x23324c;
    game.Frames(2);
    CHECK(game.Cursor() == "none");
    game.Put(answers, Sjis("hai"));
    game.Put(answers + 50, Sjis("iie"));
    game.Set<u32>(count, 2);
    game.Frames(6);
    CHECK(game.Cursor() == "none"); // the answers stay after a choice: none waits yet
    game.Set<u32>(choosing, 1);
    game.Frames(6);
    CHECK(game.Cursor() == "0");
    game.Set<u32>(cursor, 1);
    game.Frames();
    CHECK(game.Cursor() == "1");
    game.Set<u32>(choosing, 0); // picked
    game.Frames();
    CHECK(game.Cursor() == "none");
}

TEST_CASE("Kanon: a message is read with its speaker once it waits, the game's own kanji by its table",
          "[profiles]") {
    MadeUpGame game{"050D0F4F", "SLPS-20121"};
    std::vector<Reader::TextEvent> events;
    game.engine.SetEventCallback([&events](const Reader::TextEvent& event) { events.push_back(event); });
    // A made-up table: the real one is made from the game and not in Git.
    game.engine.SetDataProvider([](const std::string& name) -> std::optional<std::string> {
        if (name == "charset") {
            return std::string("9950 \xe5\xad\x97\n"); // 字
        }
        return std::nullopt;
    });
    REQUIRE(game.LoadBundled("SLPS-20121"));
    constexpr u32 text = 0x234494, state = 0x232e8c;
    // あ「あ$n　<the game's own 0x9950>。」: a speaker, a row that ends short (the script's own
    // break: a line break), the next row set in under the bracket.
    game.Put(text, std::string("\x82\xa0\x81\x75\x82\xa0$n\x81\x40\x99\x50\x81\x42\x81\x76", 18));
    game.Set<u32>(state, 3);
    game.Frames(6);
    CHECK(events.empty()); // still being typed
    game.Set<u32>(state, 4);
    game.Frames(6);
    game.engine.SetEventCallback(nullptr);
    game.engine.SetDataProvider(nullptr);
    REQUIRE(events.size() == 1);
    CHECK(events[0].speaker == "\xe3\x81\x82"); // あ
    CHECK(events[0].text == "\xe3\x80\x8c\xe3\x81\x82\n\xe5\xad\x97\xe3\x80\x82\xe3\x80\x8d"); // 「あ⏎字。」
}

namespace {

/// FINAL FANTASY X INTERNATIONAL's tables as its profile reads them, made up: the code table
/// (Shift-JIS of each code from 0x30), the widths, a scene font of one picture (all blank) and
/// one text window at the given place.
struct MadeUpFfx {
    MadeUpGame game{"658597E2", "SLPS-25088"};
    std::vector<Reader::TextEvent> events;
    static constexpr u32 code_table = 0x3235e8, widths = 0x68e070, scene_font = 0x58f4b4;
    static constexpr u32 windows = 0x667140, cursor = 0x5803c0, font = 0xb00000, text = 0xb10000;

    MadeUpFfx() {
        game.engine.SetEventCallback([this](const Reader::TextEvent& event) { events.push_back(event); });
        // A made-up table: the real one is made from the game and not in Git. 5d470f75 is the
        // hash of a picture of 252 blank pixels.
        game.engine.SetDataProvider([](const std::string& name) -> std::optional<std::string> {
            if (name == "charset") {
                return std::string("5d470f75 \xe5\xad\x97\n"); // 字
            }
            return std::nullopt;
        });
        const auto code = [this](u32 c, const char* sjis) { Put(code_table + 2 * (c - 0x30), sjis); };
        code(0x30, "\x82\x4f"); // ０
        code(0x3b, "\x81\x49"); // ！
        code(0x60, "\x82\xa0"); // あ
        code(0x61, "\x82\xa2"); // い
        for (u32 c : {0x30u, 0x3bu, 0x60u, 0x61u}) {
            game.Set<u8>(widths + c, c == 0x3b ? 8 : 12);
        }
        // FTCX: one picture of 14 by 18 in a texture 128 wide, all blank.
        Put(font, "FTCX");
        game.Set<u32>(font + 0x10, 1);
        game.Set<u16>(font + 0x14, 14);
        game.Set<u16>(font + 0x16, 18);
        game.Set<u32>(font + 0x20, 0x40);
        game.Set<u32>(font + 0x24, 128 * 18 / 2);
        game.Set<u16>(font + 0x28, 128);
        game.Set<u16>(font + 0x2a, 18);
        game.Set<u32>(scene_font, font);
    }
    ~MadeUpFfx() {
        game.engine.SetEventCallback(nullptr);
        game.engine.SetDataProvider(nullptr);
    }
    void Put(u32 address, const std::string& bytes) {
        game.Put(address, bytes);
    }
    /// Window 0 up with the text, its rectangle and how many characters are typed out.
    void Window(const std::string& bytes, u16 x0, u16 y0, u16 x1, u16 y1, u32 typed) {
        Put(text, bytes + std::string(1, '\0'));
        game.Set<u32>(windows, 1);
        game.Set<u16>(windows + 0x30, x0);
        game.Set<u16>(windows + 0x32, y0);
        game.Set<u16>(windows + 0x34, x1);
        game.Set<u16>(windows + 0x36, y1);
        game.Set<u32>(windows + 0x4c, text);
        game.Set<u32>(windows + 0x50, text);
        game.Set<u32>(windows + 0x54, text + static_cast<u32>(bytes.size()));
        game.Set<u32>(windows + 0x8c, typed);
    }
};

} // namespace

TEST_CASE("FINAL FANTASY X: speech is read with its speaker once typed out, the scene's kanji by "
          "their pictures", "[profiles]") {
    MadeUpFfx ffx;
    REQUIRE(ffx.game.LoadBundled("SLPS-25088"));
    // あい⏎あ<the scene's first kanji>！: the speaker's name on the first row.
    const std::string speech("\x60\x61\x03\x60\x2a\x30\x3b", 7);
    ffx.Window(speech, 100, 300, 300, 338, 3);
    ffx.game.Frames(10);
    CHECK(ffx.events.empty()); // still being typed
    ffx.game.Set<u32>(MadeUpFfx::windows + 0x8c, 5);
    ffx.game.Frames(6);
    REQUIRE(ffx.events.size() == 1);
    CHECK(ffx.events[0].speaker == "\xe3\x81\x82\xe3\x81\x84");                 // あい
    CHECK(ffx.events[0].text == "\xe3\x81\x82\xe5\xad\x97\xef\xbc\x81");        // あ字！
    // The same text is not reported again while it stands.
    ffx.game.Frames(10);
    CHECK(ffx.events.size() == 1);
}

TEST_CASE("FINAL FANTASY X: a choice's answers are a choice, the cursor where the game says",
          "[touch][profiles]") {
    MadeUpFfx ffx;
    REQUIRE(ffx.game.LoadBundled("SLPS-25088"));
    // A notice (0x09 0x30: no speaker) with two answers: あ！⏎{answer 0}い⏎{answer 1}あい
    const std::string notice("\x09\x30\x60\x3b\x03\x10\x30\x61\x03\x10\x31\x60\x61", 13);
    ffx.game.Set<u32>(MadeUpFfx::cursor, 1);
    ffx.Window(notice, 100, 200, 300, 258, 5);
    ffx.game.Frames(6);
    REQUIRE(ffx.events.size() == 2);
    CHECK(ffx.events[0].speaker.empty());
    CHECK(ffx.events[0].text == "\xe3\x81\x82\xef\xbc\x81"); // あ！
    CHECK(ffx.events[1].channel == "choice");
    CHECK(ffx.events[1].text == "\xe3\x81\x84\n\xe3\x81\x82\xe3\x81\x84"); // い⏎あい
    CHECK(ffx.game.Cursor() == "1");
    ffx.game.Set<u32>(MadeUpFfx::cursor, 0);
    ffx.game.Frames(1);
    CHECK(ffx.game.Cursor() == "0");
    ffx.game.Set<u32>(MadeUpFfx::windows, 0); // the window closes
    ffx.game.Frames(1);
    CHECK(ffx.game.Cursor() == "none");
}

TEST_CASE("The visual novels' scripts say they are played by presses, and the other games' nothing",
          "[touch][profiles]") {
    const QStringList novels{
        QStringLiteral("SLPS-25256"), // Never7
        QStringLiteral("SLPM-65421"), // Ever17
        QStringLiteral("SLPM-65550"), // Remember11
        QStringLiteral("SLPM-66302"), // CLANNAD
        QStringLiteral("SLPS-25918"), // アマガミ
        QStringLiteral("SLPM-66913"), // ひぐらしのなく頃に祭
        QStringLiteral("SLPM-65320"), // EVE burst error PLUS
        QStringLiteral("SLPS-20121"), // Kanon
    };
    const QDir folder(QStringLiteral(READER_BUNDLED_PROFILE_DIR));
    int seen = 0;
    for (const QString& name : folder.entryList({QStringLiteral("*.lua")}, QDir::Files)) {
        QFile file(folder.filePath(name));
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QString id = name.left(name.size() - 4);
        const auto info = ReaderApp::Catalog::Describe(id, QString::fromUtf8(file.readAll()), false);
        INFO(id.toStdString());
        CHECK(info.touch == (novels.contains(id) ? QStringLiteral("press") : QString()));
        seen += novels.contains(id) ? 1 : 0;
    }
    CHECK(seen == novels.size());
}

namespace {
/// アマガミ's memory as its script reads it (SLPS-25918.lua's comments): the scene's text values,
/// the draw list's pool and its items. Characters are Shift-JIS codes.
struct MadeUpAmagami : MadeUpGame {
    static constexpr u32 Scripts = 0x3a6bd8, Pool = 0x3dc000, Things = 0x554000;
    static constexpr u32 DrawnList = 0x2847c8, TextKind = 0x300, TextType = 0x010a994c;
    u32 next_entry = Pool, next_thing = Things;
    std::vector<std::pair<u32, u32>> placed; ///< pool entries made, to clear

    MadeUpAmagami() : MadeUpGame("D72C9E2A", "SLPS-25918") {}

    /// A text value at `at` with its characters at `chars` (16 bits each, a code's bytes
    /// swapped, as the game keeps them).
    void Text(u32 at, u32 chars, const std::vector<u16>& codes) {
        Set<u32>(at, static_cast<u32>(codes.size()));
        Set<u32>(at + 4, chars);
        Set<u32>(at + 8, 0);
        Set<u32>(at + 12, TextKind);
        Set<u32>(at + 16, TextType);
        for (std::size_t i = 0; i < codes.size(); ++i) {
            Set<u16>(chars + static_cast<u32>(i) * 2, codes[i]);
        }
    }
    void Entry(u32 thing, u32 size) {
        Set<u32>(next_entry, DrawnList);
        Set<u32>(next_entry + 12, thing);
        Set<u32>(next_entry + 16, size);
        next_entry += 0x20;
    }
    /// A character's picture (24 by 14 in a field) at x, y of the 640 by 448 picture.
    void Glyph(int x, int y, int sheet) {
        const u32 thing = next_thing;
        next_thing += 0x60;
        Set<u16>(thing + 0x40, static_cast<u16>(sheet * 16));
        Set<u16>(thing + 0x42, 0);
        Set<u16>(thing + 0x48, static_cast<u16>((x + 1728) * 16));
        Set<u16>(thing + 0x4a, static_cast<u16>((y / 2 + 1936) * 16));
        Set<u16>(thing + 0x58, static_cast<u16>((x + 24 + 1728) * 16));
        Set<u16>(thing + 0x5a, static_cast<u16>((y / 2 + 14 + 1936) * 16));
        Entry(thing, 0x60);
    }
    /// The bar that lights an answer: 560 by 36, its top at `top`.
    u32 Bar(int top) {
        const u32 thing = next_thing;
        next_thing += 0x50;
        Set<u16>(thing + 0x40, static_cast<u16>((40 + 1728) * 16));
        Set<u16>(thing + 0x42, static_cast<u16>((top / 2 + 1936) * 16));
        Set<u16>(thing + 0x48, static_cast<u16>((600 + 1728) * 16));
        Set<u16>(thing + 0x4a, static_cast<u16>((top / 2 + 18 + 1936) * 16));
        Entry(thing, 0x50);
        return thing;
    }
};
} // namespace

TEST_CASE("アマガミ: the answer lit is the one under the bar the game draws", "[touch][profiles]") {
    MadeUpAmagami game;
    REQUIRE(game.LoadBundled("SLPS-25918"));
    // A choice of three answers, each a row that begins with its mark ・ (made-up text: あい,
    // いう, うえ). The answers' values lie one after the other downwards from the first, which
    // the scene's script points at.
    constexpr u16 mark = 0x8145, a = 0x82a0, i = 0x82a2, u = 0x82a4, e = 0x82a6;
    const std::vector<std::vector<u16>> answers{{mark, a, i}, {mark, i, u}, {mark, u, e}};
    constexpr u32 first = 0x700000;
    for (std::size_t k = 0; k < answers.size(); ++k) {
        game.Text(first - static_cast<u32>(k) * 40, 0x710000 + static_cast<u32>(k) * 0x100, answers[k]);
    }
    game.Set<u32>(MadeUpAmagami::Scripts, first + 12);
    // Their pictures: rows 44 apart from 296, characters 30 apart, one sheet place a character.
    const std::map<u16, int> sheet{{mark, 1}, {a, 2}, {i, 3}, {u, 4}, {e, 5}};
    for (std::size_t row = 0; row < answers.size(); ++row) {
        for (std::size_t k = 0; k < answers[row].size(); ++k) {
            game.Glyph(60 + static_cast<int>(k) * 30, 296 + static_cast<int>(row) * 44, sheet.at(answers[row][k]));
        }
    }
    game.Frames(80);
    REQUIRE(game.engine.ChoiceShown());
    CHECK(game.Cursor() == "-1"); // no bar until the d-pad is pressed
    const u32 bar = game.Bar(290);
    game.Frames(8);
    CHECK(game.Cursor() == "0");
    game.Set<u16>(bar + 0x42, static_cast<u16>((378 / 2 + 1936) * 16)); // moved to the third
    game.Set<u16>(bar + 0x4a, static_cast<u16>((378 / 2 + 18 + 1936) * 16));
    game.Frames(8);
    CHECK(game.Cursor() == "2");
    // A bar of another size (a box, the SELECT label) is not the cursor.
    game.Set<u16>(bar + 0x48, static_cast<u16>((172 + 1728) * 16));
    game.Frames(8);
    CHECK(game.Cursor() == "-1");
}
