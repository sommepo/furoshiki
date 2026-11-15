// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "reader/core/engine.h"

// A game read by a hook description (reader.hooks, docs/core-hooks.md): a made-up game whose
// text is in made-up memory, read at a hooked address through a pointer and by looking at an
// address every frame, with the filters a description may name.
namespace {

struct Game {
    std::string memory = std::string(0x4000, '\0');
    std::vector<Reader::TextEvent> events;

    Game() {
        auto& engine = Reader::Engine::Instance();
        engine.SetEventCallback([this](const Reader::TextEvent& event) { events.push_back(event); });
        engine.StartDetached(
            [this](u64 address, std::size_t size) {
                return address < memory.size() ? memory.substr(address, size) : std::string{};
            },
            "MADE-00002");
    }
    ~Game() {
        Reader::Engine::Instance().StopDetached();
        Reader::Engine::Instance().SetEventCallback(nullptr);
    }
    void Put(std::size_t at, const std::string& bytes) {
        std::memcpy(memory.data() + at, bytes.data(), bytes.size() + 1);
    }
    void Pointer(std::size_t at, u32 value) {
        std::memcpy(memory.data() + at, &value, sizeof(value));
    }
    static void Frames(int count) {
        for (int i = 0; i < count; ++i) {
            Reader::Engine::Instance().OnFrame();
        }
    }
    static std::string Run(const std::string& code) {
        std::string out;
        REQUIRE(Reader::Engine::Instance().Execute(code, out));
        return out;
    }
};

} // namespace

TEST_CASE("the filters' regular expressions are ECMAScript's common part", "[engine][hooks]") {
    Game game;
    CHECK(Game::Run(R"T(text.resub("a%K b%P c", "%[KP]", ""))T") == "a b c");
    CHECK(Game::Run(R"T(text.resub("{漢字}[かんじ]を読む", "\\{(.*?)\\}\\[(.*?)\\]", "$1"))T") == "漢字を読む");
    CHECK(Game::Run(R"T(text.resub("%O123text%S045%A1", "%O\\d{3}|%S\\d{3}|%A\\d", ""))T") == "text");
    CHECK(Game::Run(R"T(text.resub("一\\n\\n二", "(\\\\n)+", " "))T") == "一 二");
    CHECK(Game::Run(R"T(text.resub("@name「はい」\\d", "\\\\d$|^\\@[a-z]+|#.*?#|\\$", ""))T") == "「はい」");
    CHECK(Game::Run(R"T(text.resub("#Ruby[本,ほん]を#Color[2]読む", "#Ruby\\[(.*?),(.*?)\\]", "$1"))T") == "本を#Color[2]読む");
    CHECK(Game::Run(R"T(text.resub("aXbxc", "x", "-", "i"))T") == "a-b-c");
    CHECK(Game::Run(R"T(text.resub("一\n二", "^", ">", "m"))T") == ">一\n>二");
    CHECK(Game::Run(R"T(text.resub("abcabc", "(?:ab)+?c", "<$&>"))T") == "<abc><abc>");
    CHECK(Game::Run(R"T(text.resub("aaa", "a*?", "-"))T") == "-a-a-a-");
    CHECK(Game::Run(R"T(text.resub("foo1 bar", "[a-z]+(?=\\d)", "X"))T") == "X1 bar");
    CHECK(Game::Run(R"T(text.resub("foo1 bar", "[a-z]+(?!\\d|[a-z])", "X"))T") == "foo1 X");
    CHECK(Game::Run(R"T(text.resub("abab-ab", "(ab)\\1", "$1$1!"))T") == "abab!-ab");
    CHECK(Game::Run(R"T(text.resub("　あ　", "^\\s+|\\s+$", ""))T") == "あ");
    CHECK(Game::Run(R"T(text.resub("x\\u3042y", "\\\\u3042", "あ"))T") == "xあy");
    CHECK(Game::Run(R"T(text.resub("あい", "\\u3042|\\x41", "A"))T") == "Aい");
    CHECK(Game::Run(R"T(text.resub("price $5", "\\$(\\d)", "$$$1$9"))T") == "price $5$9");
    CHECK(Game::Run(R"T(text.resub("(a)(b)", "\\((\\w)\\)", "[$1]"))T") == "[a][b]");
    CHECK(Game::Run(R"T(text.resub("aaa", "(a*)*b", "x"))T") == "aaa"); // ends, and does not match
    CHECK(Game::Run(R"T(text.resub("ab{2}c abbc", "ab{2}c", "x"))T") == "ab{2}c x");
    CHECK(Game::Run(R"T(text.resub("a{b", "a{b", "x"))T") == "x"); // a brace that is no count is itself
    CHECK(Game::Run(R"T(text.retest("会話です", "[^\\x00-\\x7F]"))T") == "true");
    CHECK(Game::Run(R"T(text.retest("abc", "^b"))T") == "false");
    // What is not known here is said, not guessed.
    CHECK(Game::Run(R"T(text.recheck("(?<=a)b"))T").find("not known") != std::string::npos);
    CHECK(Game::Run(R"T(text.recheck("\\p{L}"))T").find("not known") != std::string::npos);
    CHECK(Game::Run(R"T(text.recheck("(a"))T").find("without") != std::string::npos);
    CHECK(Game::Run(R"T(text.recheck("a+b"))T") == "nil");
}

TEST_CASE("a hook description reads a made-up game at a hooked address", "[engine][hooks]") {
    Game game;
    // The hook is at address 0, which every console's `pc` names in a test's registers; the text's
    // pointer is kept at 0x40.
    auto& engine = Reader::Engine::Instance();
    REQUIRE(engine.LoadProfile("MADE-00002.lua", R"lua(
-- reader: by hook
reader.hooks{
    source = "a test",
    { name = "lines", at = 0, from = "pc", follow = 0x40, text = "sjis", whole = true,
      filters = { {"cutb", "\x81\x99"}, {"cut", "%K"}, {"re", "\\{(.*?)\\}\\[(.*?)\\]", "$1"}, {"swap", "＊Ａ", "岡崎"} } },
    { name = "wide", at = 0, from = "pc", follow = 0x48, add = 4, text = "utf16" },
    { name = "broken", at = 0, from = "pc", text = "sjis", filters = { {"re", "(?<=a)b", ""} } },
    { name = "letters", at = 0, from = "pc", follow = 0x50, text = "utf16", char = true },
}
)lua"));
    CHECK(engine.HookCount() == 1); // three hooks at the one address; the broken one left out
    Reader::GuestRegisters regs{};
    // 「{漢字}[かんじ]%K＊Ａ」 in Shift-JIS, and its pointer.
    game.Put(0x100, "\x81\x75{\x8A\xBF\x8E\x9A}[\x82\xA9\x82\xF1\x82\xB6]%K\x81\x99\x81\x96\x82\x60\x81\x76");
    game.Pointer(0x40, 0x100);
    // "ab" in UTF-16 after four bytes of something else.
    game.Put(0x200, std::string("\x09\x00\x00\x00" "a\x00" "b\x00\x00", 9));
    game.Pointer(0x48, 0x200);
    game.Pointer(0x50, 0x3042); // あ
    engine.Dispatch(regs);
    engine.Dispatch(regs); // the same whole line again is the same line
    game.Pointer(0x50, 0x3044); // い
    engine.Dispatch(regs);
    CHECK(game.events.empty()); // gathered until the hook has been quiet a while
    Game::Frames(8);
    REQUIRE(game.events.size() == 3);
    CHECK(game.events[0].text == "「漢字岡崎」");
    CHECK(game.events[0].meta_json == R"({"hook":"lines"})");
    CHECK(game.events[1].text == "ab"); // the same piece again is not said twice
    CHECK(game.events[2].text == "ああい");
    // A new line.
    game.Put(0x100, "\x82\xA0"); // あ
    engine.Dispatch(regs);
    Game::Frames(8);
    CHECK(game.events.size() >= 4);
    CHECK(game.events[3].text == "あ");
    const std::string seen = Game::Run("reader.hooks_seen()");
    CHECK(seen.find("lines\t4\t2\tあ") != std::string::npos);
    // Only the chosen hooks of a description run.
    REQUIRE(engine.LoadProfile("MADE-00002.lua", R"lua(
reader.hooks{ only = {"wide"},
    { name = "lines", at = 0, from = "pc", follow = 0x40, whole = true },
    { name = "wide", at = 0, from = "pc", follow = 0x48, add = 4, text = "utf16" },
}
)lua"));
    game.events.clear();
    engine.Dispatch(regs);
    Game::Frames(8);
    REQUIRE(game.events.size() == 1);
    CHECK(game.events[0].text == "ab");
}

TEST_CASE("a hook description reads memory that is looked at every frame", "[engine][hooks]") {
    Game game;
    auto& engine = Reader::Engine::Instance();
    REQUIRE(engine.LoadProfile("MADE-00002.lua", R"lua(
-- reader: by hook
-- reader: no hooks
reader.hooks{
    { name = "window", read = 0x300, text = "utf8", whole = true,
      filters = { {"japanese"}, {"once", 2}, {"upto", ");"}, {"trim"}, {"deny", "^※"} } },
}
)lua"));
    CHECK(engine.HookCount() == 0);
    game.Put(0x300, "loading.bin");
    Game::Frames(12);
    CHECK(game.events.empty()); // a file's name is not the game's text
    game.Put(0x300, "　今日は); x");
    Game::Frames(12);
    REQUIRE(game.events.size() == 1);
    CHECK(game.events[0].text == "今日は");
    game.Put(0x300, "※注");
    Game::Frames(12);
    game.Put(0x300, "　今日は); x"); // one of the last two: not said again
    Game::Frames(12);
    CHECK(game.events.size() == 1);
    game.Put(0x300, "明日は");
    Game::Frames(12);
    REQUIRE(game.events.size() == 2);
    CHECK(game.events[1].text == "明日は");
}
