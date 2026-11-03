// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PCSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "reader/core/engine.h"

// Progress set by hand (reader.progress): a made-up game whose "routes" are 16-bit cells of
// made-up memory, as CLANNAD's profile has them.
namespace {
constexpr char Profile[] = R"lua(
-- reader: no hooks
local CELLS = 0x1000
reader.progress{
    title = "Routes",
    note = "Save the system data.",
    items = { { id = "a", label = "Route A", group = "Part 1" }, { id = "b", label = "Route B" } },
    done = function(id)
        local at = id == "a" and 0 or 2
        return mem.u16(CELLS + at) == 1
    end,
    mark = function(id, on)
        local at = id == "a" and 0 or 2
        mem.w16(CELLS + at, on and 1 or 0)
        if id == "b" and on then mem.w16(CELLS, 1) end -- B brings A with it
    end,
}
-- Written outside a mark: refused.
written_at_load = pcall(mem.w16, CELLS, 7)
)lua";
} // namespace

TEST_CASE("a profile's progress is read and marked through its memory", "[progress]") {
    std::string memory(0x2000, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached(
        [&memory](u64 address, std::size_t size) {
            return address < memory.size() ? memory.substr(address, size) : std::string{};
        },
        "MADE-00001",
        [&memory](u64 address, const std::string& bytes) {
            if (address + bytes.size() > memory.size()) {
                return false;
            }
            std::memcpy(memory.data() + address, bytes.data(), bytes.size());
            return true;
        });
    REQUIRE(engine.LoadProfile("made-up", Profile));
    std::string out;
    REQUIRE(engine.Execute("written_at_load", out));
    CHECK(out == "false");
    CHECK(memory[0x1000] == 0);

    auto list = engine.Progress();
    REQUIRE(list);
    CHECK(list->title == "Routes");
    CHECK(list->note == "Save the system data.");
    REQUIRE(list->items.size() == 2);
    CHECK(list->items[0].id == "a");
    CHECK(list->items[0].group == "Part 1");
    CHECK(list->items[1].label == "Route B");
    CHECK_FALSE(list->items[0].done);

    std::string error;
    REQUIRE(engine.MarkProgress("b", true, error));
    list = engine.Progress();
    CHECK(list->items[0].done);
    CHECK(list->items[1].done);
    CHECK(memory[0x1002] == 1);

    REQUIRE(engine.MarkProgress("a", false, error));
    CHECK_FALSE(engine.Progress()->items[0].done);
    // Still refused once marking is over.
    REQUIRE(engine.Execute("(pcall(mem.w16, 0x1000, 7))", out));
    CHECK(out == "false");
    CHECK(memory[0x1000] == 0);
    engine.StopDetached();
}

TEST_CASE("a profile that offers nothing has no progress", "[progress]") {
    std::string memory(0x100, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached([&memory](u64 address, std::size_t size) { return memory.substr(address, size); });
    REQUIRE(engine.LoadProfile("plain", "-- reader: no hooks\n"));
    CHECK_FALSE(engine.Progress());
    std::string error;
    CHECK_FALSE(engine.MarkProgress("a", true, error));
    CHECK_FALSE(error.empty());
    engine.StopDetached();
}

// What the player may do in the game (reader.action): a made-up game whose chapters open with
// a byte of made-up memory, as Phoenix Wright's profile opens its episodes.
TEST_CASE("a profile's actions run when chosen, and may write the game's memory from then on",
          "[progress]") {
    std::string memory(0x2000, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached(
        [&memory](u64 address, std::size_t size) {
            return address < memory.size() ? memory.substr(address, size) : std::string{};
        },
        "MADE-00001",
        [&memory](u64 address, const std::string& bytes) {
            if (address + bytes.size() > memory.size()) {
                return false;
            }
            std::memcpy(memory.data() + address, bytes.data(), bytes.size());
            return true;
        });
    REQUIRE(engine.LoadProfile("made-up", R"lua(
-- reader: no hooks
local keep_open = false
reader.action("Open every chapter", function()
    keep_open = true
    mem.w8(0x1000, 9)
    return "Every chapter can now be chosen."
end)
reader.action("Say nothing", function() end)
reader.action("Broken", function() error("no such chapter") end)
-- The game puts its own value back now and then: the profile keeps it open, frame by frame.
reader.on_frame(function()
    if keep_open and mem.u8(0x1000) ~= 9 then mem.w8(0x1000, 9) end
end)
)lua"));
    CHECK(engine.Actions() ==
          std::vector<std::string>{"Open every chapter", "Say nothing", "Broken"});
    std::string out;
    // Nothing is written before the player asked for something.
    REQUIRE(engine.Execute("(pcall(mem.w8, 0x1000, 1))", out));
    CHECK(out == "false");
    CHECK(memory[0x1000] == 0);

    CHECK(engine.RunAction(0) == std::optional<std::string>{"Every chapter can now be chosen."});
    CHECK(memory[0x1000] == 9);
    memory[0x1000] = 1;
    engine.OnFrame();
    CHECK(memory[0x1000] == 9);
    CHECK_FALSE(engine.RunAction(1).has_value());
    // An error is what the player is told, and the profile goes on.
    CHECK(engine.RunAction(2).value_or("").find("no such chapter") != std::string::npos);
    CHECK_FALSE(engine.RunAction(3).has_value());

    REQUIRE(engine.Execute("select(2, pcall(reader.action, '', function() end))", out));
    CHECK(out.find("label") != std::string::npos);
    REQUIRE(engine.Execute("select(2, pcall(reader.action, 'two\\nlines', function() end))", out));
    CHECK(out.find("one line") != std::string::npos);

    // A profile's actions, and what they allowed, go with it.
    engine.UnloadProfile();
    CHECK(engine.Actions().empty());
    CHECK_FALSE(engine.RunAction(0).has_value());
    REQUIRE(engine.LoadProfile("again", "-- reader: no hooks\n"));
    REQUIRE(engine.Execute("(pcall(mem.w8, 0x1000, 1))", out));
    CHECK(out == "false");
    engine.StopDetached();
}

// Lua's count hook, which stops a profile that runs too long, never runs inside the string
// library's pattern matching: a pattern that backtracks without end would hold the game's
// thread (and the engine's lock) for seconds. The match counts its own steps instead
// (reader/lua/lstrlib.c) and gives up with an error.
TEST_CASE("a pattern too complex to match is an error, and a quick one", "[progress]") {
    std::string memory(0x100, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached([&memory](u64 address, std::size_t size) { return memory.substr(address, size); });
    REQUIRE(engine.LoadProfile("made-up", "-- reader: no hooks\n"));
    std::string out;
    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(engine.Execute("string.rep('a', 400):find('.-.-.-x')", out));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK(out.find("pattern too complex") != std::string::npos);
    // Patterns that match in the ordinary way, over much longer text, still do; and the
    // profile goes on.
    REQUIRE(engine.Execute("#string.rep('a', 1000000):match('^(a*)$')", out));
    CHECK(out == "1000000");
    REQUIRE(engine.Execute("select(2, string.rep('ab', 100000):gsub('b', 'c'))", out));
    CHECK(out == "100000");
    engine.StopDetached();
}

TEST_CASE("a layout says which of the console's screens its text is on", "[progress]") {
    std::string memory(0x100, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached([&memory](u64 address, std::size_t size) { return memory.substr(address, size); });
    std::vector<Reader::TextLayout> layouts;
    engine.SetLayoutCallback([&layouts](const Reader::TextLayout& layout) { layouts.push_back(layout); });
    REQUIRE(engine.LoadProfile("made-up", "-- reader: no hooks\n"));
    std::string out;
    REQUIRE(engine.Execute(R"(reader.emit{text = "あ", window = "w"}
        reader.layout("w", {glyphs = {{1, 2, 3, 4}}}))", out));
    REQUIRE(layouts.size() == 1);
    CHECK(layouts[0].screen == 0);
    CHECK(layouts[0].space_width == Reader::ConsoleTraits::ScreenWidth);
    CHECK(layouts[0].ToJson().find("\"screen\"") == std::string::npos);
    const bool fine = engine.Execute(R"(reader.emit{text = "い", window = "w"}
        reader.layout("w", {screen = "bottom", glyphs = {{1, 2, 3, 4}}}))", out);
    if (Reader::ConsoleTraits::LowerScreenWidth > 0) {
        REQUIRE(fine);
        REQUIRE(layouts.size() == 2);
        CHECK(layouts[1].screen == 1);
        CHECK(layouts[1].space_width == Reader::ConsoleTraits::LowerScreenWidth);
        CHECK(layouts[1].ToJson().find("\"screen\":1") != std::string::npos);
    } else {
        CHECK_FALSE(fine);
        CHECK(out.find("one screen") != std::string::npos);
    }
    engine.SetLayoutCallback(nullptr);
    engine.StopDetached();
}

TEST_CASE("what a profile's finalizers do as it goes is not kept for the next one", "[engine]") {
    std::string memory(0x100, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached([&memory](u64 address, std::size_t size) {
        return address < memory.size() ? memory.substr(address, size) : std::string{};
    });
    REQUIRE(engine.LoadProfile("first", R"lua(
-- reader: no hooks
kept = setmetatable({}, {__gc = function()
    reader.on_frame(function() end)
    reader.action("left behind", function() end)
    reader.emit{text = "from a finalizer"}
end})
)lua"));
    REQUIRE(engine.LoadProfile("second", R"lua(
-- reader: no hooks
frames = 0
reader.on_frame(function() frames = frames + 1 end)
)lua"));
    engine.OnFrame();
    std::string out;
    REQUIRE(engine.Execute("frames", out));
    CHECK(out == "1");
    CHECK(engine.Actions().empty());
    // An error that is not text is said all the same.
    CHECK_FALSE(engine.LoadProfile("third", "error(42)"));
    CHECK(engine.LastError().find("42") != std::string::npos);
    engine.StopDetached();
}

TEST_CASE("bad input to the text functions and emit is refused or read as it is", "[engine]") {
    std::string memory(0x100, '\0');
    auto& engine = Reader::Engine::Instance();
    engine.StartDetached([&memory](u64 address, std::size_t size) {
        return address < memory.size() ? memory.substr(address, size) : std::string{};
    });
    std::string out;
    // A malformed 0x8E or 0x8F of EUC-JP is one bad character; what follows stays itself.
    REQUIRE(engine.Execute(R"(text.eucjp("\x8E\x41\xA4\xA2"))", out));
    CHECK(out == "\xEF\xBF\xBD" "A" "\xE3\x81\x82"); // a bad byte, A, then あ
    REQUIRE(engine.Execute(R"(text.eucjp("\x8F\x41\x42\x43"))", out));
    CHECK(out == "\xEF\xBF\xBD" "ABC");
    REQUIRE(engine.Execute(R"(text.eucjp("\x8E\xB1"))", out));
    CHECK(out == "\xEF\xBD\xB1"); // ｱ
    // A ruby's place is a whole number that fits.
    REQUIRE(engine.Execute("(pcall(reader.emit, {text = 'abc', ruby = {{1.5, 1, 'x'}}}))", out));
    CHECK(out == "false");
    REQUIRE(engine.Execute("(pcall(reader.emit, {text = 'abc', ruby = {{4294967297, 1, 'x'}}}))", out));
    CHECK(out == "false");
    REQUIRE(engine.Execute("(pcall(reader.emit, {text = 'abc', ruby = {{1, 1, 'x'}}}))", out));
    CHECK(out == "true");
    engine.StopDetached();
}
