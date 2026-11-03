// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// The bundled Shin Megami Tensei IV profile, run against made-up messages in fake memory: the
// same calls the game makes, in the same order, with no game data involved.

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "reader/core/engine.h"
#include "reader/core/text_codec.h"
#include "tests/core/reader/reader_test_util.h"

using Catch::Matchers::ContainsSubstring;
using Reader::LogLevel;

namespace {

constexpr u64 TitleId = 0x0004000000088A00ULL;

// Addresses the profile is built on.
constexpr u32 SetText = 0x00287468;
constexpr u32 SetTextExpanded = 0x00287564;
constexpr u32 ExpandedBuffer = 0x004DDF08;
constexpr u32 MbmWrapperReturn = 0x002972D8;
constexpr u32 DrawGlyph = 0x002AA878;

// Where the test puts things.
constexpr u32 WindowA = 0x08100000;
constexpr u32 WindowB = 0x08200000;
constexpr u32 RecordsA = 0x08C00000;
constexpr u32 RecordsB = 0x08D00000;
constexpr u32 Stack = 0x0FFFFF00;
constexpr u32 Caller = 0x00123456;

constexpr u32 StyleDialogue = 1;
constexpr u32 StyleChoices = 3;

// Shift-JIS, as the game stores text.
constexpr std::string_view Tesuto = "\x83\x65\x83\x58\x83\x67";        // テスト
constexpr std::string_view A = "\x82\xA0";                             // あ
constexpr std::string_view I = "\x82\xA2";                             // い
constexpr std::string_view U = "\x82\xA4";                             // う
constexpr std::string_view E = "\x82\xA6";                             // え
constexpr std::string_view O = "\x82\xA8";                             // お
constexpr std::string_view Kanji = "\x8A\xBF\x8E\x9A";                 // 漢字
constexpr std::string_view Kanji_Reading = "\x82\xA9\x82\xF1\x82\xB6"; // かんじ

/// Builds a message the way MSG2 files hold them.
struct Message {
    Message& Text(std::string_view sjis) {
        bytes.append(sjis);
        return *this;
    }
    Message& Code(u8 code, std::string_view args = {}) {
        bytes.push_back('\xF8');
        bytes.push_back(static_cast<char>(code));
        bytes.append(args);
        return *this;
    }
    Message& Speaker(std::string_view sjis) {
        Code(0x12).Text(sjis);
        bytes.append(2, '\0');
        return *this;
    }
    Message& Voice(u8 id) {
        const char args[8]{0, 0, 0, 0, static_cast<char>(id), 0, 0, 0};
        return Code(0x13, std::string_view{args, 8});
    }
    Message& Ruby(std::string_view base, std::string_view reading) {
        return Code(0x16).Text(base).Code(0x17).Text(reading).Code(0x18);
    }
    Message& Line() {
        return Code(0x01);
    }
    Message& Page() {
        return Code(0x02);
    }
    Message& End() {
        bytes.append("\xFF\xFF");
        return *this;
    }
    [[nodiscard]] u32 Units() const {
        return static_cast<u32>(bytes.size() / 2);
    }
    [[nodiscard]] std::string Id() const {
        return Reader::HashHex(
            std::span<const u8>{reinterpret_cast<const u8*>(bytes.data()), bytes.size()});
    }
    std::string bytes;
};

/// Speaker, an ignored code with an argument, then two voiced pages; the second starts at unit 25.
Message TwoPages() {
    Message m;
    m.Speaker(Tesuto).Code(0x7A, std::string_view{"\x01\x00", 2}).Voice(42);
    m.Text(A).Ruby(Kanji, Kanji_Reading).Text(I).Line().Text(U).Page();
    m.Voice(43).Text(E).Text(O).Line().End();
    return m;
}

struct Game : ReaderTest::DetachedEngine {
    Game() : DetachedEngine{TitleId} {
        memory.Write32(SetText, 0xE92D4FFF);
        memory.Write32(SetTextExpanded - 4, 0xEB013A25);
        memory.Write32(SetTextExpanded, 0xE1A08000);
        memory.Write32(DrawGlyph, 0xE92D03F0);
    }

    bool Load() {
        return engine.LoadProfile("0004000000088A00.lua",
                                  ReaderTest::BundledProfile("0004000000088A00"));
    }

    /// What the game does to show a message: SetText is entered, the text is expanded into the
    /// fixed buffer, and the window is laid out from it.
    void SetTextCall(u32 window, const Message& message, u32 style = StyleDialogue,
                     u32 return_address = Caller) {
        Fire(SetText, {{0, window}, {1, style}, {13, Stack}, {14, return_address}});
        memory.Unmap(ExpandedBuffer, 8192);
        memory.Write(ExpandedBuffer, std::string_view{message.bytes});
        Fire(SetTextExpanded, {{0, message.Units()}});
        // The rest of SetText: one record per unit, with the unit's code and what the font
        // says about its glyph. All characters here advance the pen by 16 pixels.
        const u32 records = window == WindowA ? RecordsA : RecordsB;
        memory.Unmap(records, 32 * 512);
        for (u32 unit = 0; unit < message.Units(); ++unit) {
            const u8 high = static_cast<u8>(message.bytes[unit * 2]);
            const u8 low = static_cast<u8>(message.bytes[unit * 2 + 1]);
            const u8 record[32]{low, high, 0, 0, 0, 0, 0, 0, 1, 14, 16, 21};
            memory.Write(records + unit * 32, record);
        }
        memory.Write32(window + 0x00, records);
        memory.Write32(window + 0x04, message.Units());
        memory.Write32(window + 0x0C, 0);
        memory.Write32(window + 0x34, 0);
        memory.Write(window + 0x44, std::string_view{"\x02", 1}); // waiting for the player
    }
    /// The window's draw routine drawing one character of the message.
    void DrawUnit(u32 window, u32 unit, float x, float y) {
        const u32 records = window == WindowA ? RecordsA : RecordsB;
        Fire(DrawGlyph, {{1, records + unit * 32 + 4}}, {{0, x}, {1, y}, {2, 14.0f}, {3, 21.0f}});
    }
    void ShowPageAt(u32 window, u32 unit) {
        memory.Write32(window + 0x0C, unit);
        engine.OnFrame();
    }
};

} // Anonymous namespace

TEST_CASE("SMT IV profile: loads for the build it was written for", "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    REQUIRE(game.engine.HookCount() == 3);
    REQUIRE(game.engine.IsHooked(SetText));
    REQUIRE(game.engine.IsHooked(SetTextExpanded));
    REQUIRE(game.engine.IsHooked(DrawGlyph));
}

TEST_CASE("SMT IV profile: refuses another build of the game", "[reader][profile]") {
    for (const u32 address : {SetText, SetTextExpanded - 4, SetTextExpanded, DrawGlyph}) {
        Game game;
        game.memory.Write32(address, 0xE1A00000);
        REQUIRE_FALSE(game.Load());
        REQUIRE_THAT(game.engine.LastError(), ContainsSubstring("another build of the game"));
        REQUIRE(game.engine.HookCount() == 0);
    }
    // Code that is not there at all (another title, or nothing mapped) is refused the same way.
    ReaderTest::DetachedEngine empty{TitleId};
    REQUIRE_FALSE(empty.engine.LoadProfile("0004000000088A00.lua",
                                           ReaderTest::BundledProfile("0004000000088A00")));
    REQUIRE_THAT(empty.engine.LastError(), ContainsSubstring("another build of the game"));
}

TEST_CASE("SMT IV profile: each page is reported when the game shows it", "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    const Message message = TwoPages();
    REQUIRE(message.Units() == 34);

    // Entering SetText alone shows nothing yet.
    game.Fire(SetText, {{0, WindowA}, {1, StyleDialogue}, {13, Stack}, {14, Caller}});
    REQUIRE(game.TakeEvents().empty());

    game.SetTextCall(WindowA, message);
    auto events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].channel == "dialogue");
    REQUIRE(events[0].speaker == "テスト");
    REQUIRE(events[0].text == "あ漢字い\nう");
    REQUIRE(events[0].ruby.size() == 1);
    REQUIRE(events[0].ruby[0].start == 1);
    REQUIRE(events[0].ruby[0].length == 2);
    REQUIRE(events[0].ruby[0].reading == "かんじ");
    REQUIRE(events[0].source_id == message.Id() + ".1");
    REQUIRE(events[0].meta_json ==
            R"({"context":"00123456","page":1,"pages":2,"style":1,"voice":42})");

    // Frames pass while the first page is on screen: nothing new.
    for (int i = 0; i < 5; ++i) {
        game.engine.OnFrame();
    }
    REQUIRE(game.TakeEvents().empty());

    // The player advances: the window's page start moves to the second page.
    game.ShowPageAt(WindowA, 25);
    events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].speaker == "テスト");
    REQUIRE(events[0].text == "えお"); // the trailing line break is not part of the text
    REQUIRE(events[0].ruby.empty());
    REQUIRE(events[0].source_id == message.Id() + ".2");
    REQUIRE(events[0].meta_json ==
            R"({"context":"00123456","page":2,"pages":2,"style":1,"voice":43})");

    // Staying there, or a page start that is no page boundary, reports nothing; a page is
    // reported once even if the window returns to it.
    game.engine.OnFrame();
    game.ShowPageAt(WindowA, 7);
    game.ShowPageAt(WindowA, 0);
    game.ShowPageAt(WindowA, 25);
    REQUIRE(game.TakeEvents().empty());
}

TEST_CASE("SMT IV profile: the caller is taken from the stack for MSG2 lookups",
          "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    game.memory.Write32(Stack + 0x14, 0x00345678);
    game.SetTextCall(WindowA, Message{}.Text(A).End(), StyleDialogue, MbmWrapperReturn);
    auto events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE_THAT(events[0].meta_json, ContainsSubstring(R"("context":"00345678")"));

    // If the stack cannot be read, the wrapper's own address is better than nothing.
    game.memory.Unmap(Stack + 0x14, 4);
    game.SetTextCall(WindowA, Message{}.Text(I).End(), StyleDialogue, MbmWrapperReturn);
    events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE_THAT(events[0].meta_json, ContainsSubstring(R"("context":"002972D8")"));
}

TEST_CASE("SMT IV profile: selection lists are choices", "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    game.SetTextCall(WindowA, Message{}.Text(A).Line().Text(I).End(), StyleChoices);
    const auto events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].channel == "choice");
    REQUIRE(events[0].speaker.empty());
    REQUIRE(events[0].text == "あ\nい");
    REQUIRE(events[0].meta_json == R"({"context":"00123456","page":1,"pages":1,"style":3})");
}

TEST_CASE("SMT IV profile: ids identify the text", "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    const Message first = Message{}.Text(A).End();
    const Message second = Message{}.Text(I).End();

    game.SetTextCall(WindowA, first);
    game.SetTextCall(WindowA, second);
    game.SetTextCall(WindowB, first);
    const auto events = game.TakeEvents();
    REQUIRE(events.size() == 3);
    // One page: no page suffix. The same message again, in whichever window: the same id.
    REQUIRE(events[0].source_id == first.Id());
    REQUIRE(events[1].source_id == second.Id());
    REQUIRE(events[2].source_id == first.Id());
    REQUIRE(events[0].source_id != events[1].source_id);
}

TEST_CASE("SMT IV profile: a leading line break is dropped and furigana still fits",
          "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    game.SetTextCall(WindowA, Message{}.Line().Ruby(Kanji, Kanji_Reading).Text(A).End());
    const auto events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].text == "漢字あ");
    REQUIRE(events[0].ruby.size() == 1);
    REQUIRE(events[0].ruby[0].start == 0);
    REQUIRE(events[0].ruby[0].length == 2);
}

TEST_CASE("SMT IV profile: a message it cannot parse is skipped, not guessed",
          "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());

    game.SetTextCall(WindowA, Message{}.Text(A).Code(0x99).Text(I).End());
    REQUIRE(game.TakeEvents().empty());
    REQUIRE(game.CountLogs(LogLevel::Warning, "unknown control code F899") == 1);

    Message open;
    open.Text(A).Text(I);
    game.SetTextCall(WindowA, open);
    REQUIRE(game.TakeEvents().empty());
    REQUIRE(game.CountLogs(LogLevel::Warning, "unterminated message") == 1);

    // Only control codes: nothing to read.
    game.SetTextCall(WindowA, Message{}.Voice(1).Line().End());
    REQUIRE(game.TakeEvents().empty());

    // None of this counts as a failing hook, and the next message is reported as usual.
    REQUIRE(game.CountLogs(LogLevel::Error, "") == 0);
    game.SetTextCall(WindowA, Message{}.Text(U).End());
    REQUIRE(game.TakeEvents().size() == 1);
}

TEST_CASE("SMT IV profile: a closed or reused window is no longer followed", "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    const Message message = TwoPages();

    SECTION("closed") {
        game.SetTextCall(WindowA, message);
        game.memory.Write32(WindowA + 0x34, 0x10);
        game.engine.OnFrame();
        game.memory.Write32(WindowA + 0x34, 0);
        game.ShowPageAt(WindowA, 25);
        REQUIRE(game.TakeEvents().size() == 1); // the first page only
    }
    SECTION("holds something else") {
        game.SetTextCall(WindowA, message);
        game.memory.Write32(WindowA + 0x04, 12);
        game.engine.OnFrame();
        game.memory.Write32(WindowA + 0x04, message.Units());
        game.ShowPageAt(WindowA, 25);
        REQUIRE(game.TakeEvents().size() == 1);
    }
    SECTION("gone from memory") {
        game.SetTextCall(WindowA, message);
        game.memory.Unmap(WindowA, 0x40);
        game.engine.OnFrame();
        REQUIRE(game.TakeEvents().size() == 1);
        REQUIRE(game.CountLogs(LogLevel::Error, "") == 0);
    }
    SECTION("two windows are followed independently") {
        game.SetTextCall(WindowA, message);
        game.SetTextCall(WindowB, message);
        REQUIRE(game.TakeEvents().size() == 2);
        game.ShowPageAt(WindowB, 25);
        auto events = game.TakeEvents();
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].text == "えお");
        game.ShowPageAt(WindowA, 25);
        REQUIRE(game.TakeEvents().size() == 1);
    }
}

TEST_CASE("SMT IV profile: nothing on screen is reported again after a state load",
          "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    const Message message = TwoPages();

    game.SetTextCall(WindowA, message);
    REQUIRE(game.TakeEvents().size() == 1);
    game.engine.OnStateLoaded();
    game.ShowPageAt(WindowA, 25);
    REQUIRE(game.TakeEvents().empty());

    // A SetText call that was under way when the state was loaded is forgotten as well.
    game.Fire(SetText, {{0, WindowA}, {1, StyleDialogue}, {13, Stack}, {14, Caller}});
    game.engine.OnStateLoaded();
    game.Fire(SetTextExpanded, {{0, message.Units()}});
    REQUIRE(game.TakeEvents().empty());

    // The game setting text again works as before.
    game.SetTextCall(WindowA, message);
    REQUIRE(game.TakeEvents().size() == 1);
}

TEST_CASE("SMT IV profile: the place of every character on screen is reported",
          "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());
    const Message message = TwoPages();
    game.SetTextCall(WindowA, message);
    auto events = game.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == "135266304"); // the window's address
    REQUIRE(game.TakeLayouts().empty());      // nothing drawn yet

    // The game draws the first page: あ, the furigana かんじ above 漢字, then 漢字い, and う on
    // the second line. Units: あ 12, 漢 14, 字 15, か ん じ 17-19, い 21, う 23.
    const auto draw_first_page = [&] {
        game.DrawUnit(WindowA, 12, 40, 191);
        game.DrawUnit(WindowA, 17, 58, 184);
        game.DrawUnit(WindowA, 18, 66, 184);
        game.DrawUnit(WindowA, 19, 74, 184);
        game.DrawUnit(WindowA, 14, 56, 191);
        game.DrawUnit(WindowA, 15, 72, 191);
        game.DrawUnit(WindowA, 21, 88, 191);
        game.DrawUnit(WindowA, 23, 40, 217);
    };
    draw_first_page();
    game.engine.OnFrame();
    auto layouts = game.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    // One entry per code point of "あ漢字い\nう": the cell each character sits in (pen
    // position, advance, glyph height), none for the line break, and the band of the screen
    // the two lines occupy.
    REQUIRE(
        layouts[0] ==
        R"({"window":"135266304","seq":1,"space":[400,240],"box":[0,179,400,65],"glyphs":)"
        R"([[40,191,16,21],[56,191,16,21],[72,191,16,21],[88,191,16,21],null,[40,217,16,21]]})");

    // The same picture on later frames is not reported again.
    for (int i = 0; i < 3; ++i) {
        draw_first_page();
        game.engine.OnFrame();
    }
    REQUIRE(game.TakeLayouts().empty());

    // The second page: its own event, then its own layout, counted from its own text.
    game.ShowPageAt(WindowA, 25);
    REQUIRE(game.TakeEvents().size() == 1);
    game.DrawUnit(WindowA, 30, 40, 191);
    game.DrawUnit(WindowA, 31, 56, 191);
    game.engine.OnFrame();
    layouts = game.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"135266304","seq":2,"space":[400,240],"box":[0,179,400,39],)"
                          R"("glyphs":[[40,191,16,21],[56,191,16,21]]})");

    // Not drawn for a few frames: no longer on screen. Drawn again: back.
    for (int i = 0; i < 5; ++i) {
        game.engine.OnFrame();
    }
    layouts = game.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"135266304","seq":2,"closed":true})");
    game.DrawUnit(WindowA, 30, 40, 191);
    game.DrawUnit(WindowA, 31, 56, 191);
    game.engine.OnFrame();
    REQUIRE(game.TakeLayouts().size() == 1);

    // The window closes.
    game.memory.Write32(WindowA + 0x34, 0x10);
    game.engine.OnFrame();
    layouts = game.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"135266304","seq":2,"closed":true})");
}

TEST_CASE("SMT IV profile: layouts while text is typed out, and for choices", "[reader][profile]") {
    Game game;
    REQUIRE(game.Load());

    // Typing: characters appear one per frame, reports are spaced out, and the finished line
    // is reported at once.
    const Message line = Message{}.Text(A).Text(I).Text(U).Text(E).Text(O).End();
    game.SetTextCall(WindowA, line);
    game.memory.Write(WindowA + 0x44, std::string_view{"\x01", 1});
    std::size_t reports = 0;
    for (u32 revealed = 1; revealed <= 5; ++revealed) {
        for (u32 unit = 0; unit < revealed; ++unit) {
            game.DrawUnit(WindowA, unit, 40.0f + 16.0f * static_cast<float>(unit), 191);
        }
        game.engine.OnFrame();
        reports += game.TakeLayouts().size();
    }
    REQUIRE(reports == 1); // the first character; the rest waits
    game.memory.Write(WindowA + 0x44, std::string_view{"\x02", 1});
    for (u32 unit = 0; unit < 5; ++unit) {
        game.DrawUnit(WindowA, unit, 40.0f + 16.0f * static_cast<float>(unit), 191);
    }
    game.engine.OnFrame();
    auto layouts = game.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE_THAT(layouts[0], ContainsSubstring(R"([104,191,16,21]]})"));

    // A selection list is on the bottom screen.
    game.SetTextCall(WindowB, Message{}.Text(A).Line().Text(I).End(), StyleChoices);
    game.DrawUnit(WindowB, 0, 24, 40);
    game.DrawUnit(WindowB, 2, 24, 66);
    game.engine.OnFrame();
    layouts = game.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"136314880","seq":2,"screen":1,"space":[320,240],"box":[0,28,320,65],)"
                          R"("glyphs":[[24,40,16,21],null,[24,66,16,21]]})");

    // Characters that are not part of a tracked message are ignored.
    game.Fire(DrawGlyph, {{1, 0x08E00004}}, {{0, 1.0f}, {1, 1.0f}, {2, 1.0f}, {3, 1.0f}});
    game.engine.OnFrame();
    REQUIRE(game.CountLogs(LogLevel::Error, "") == 0);
}
