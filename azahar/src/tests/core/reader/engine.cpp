// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// The text engine and the Lua API that profiles are written against, without an emulated system.

#include <span>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "reader/core/engine.h"
#include "reader/core/text_codec.h"
#include "tests/core/reader/reader_test_util.h"

using Catch::Matchers::ContainsSubstring;
using Reader::LogLevel;
using ReaderTest::DetachedEngine;

namespace {

constexpr u32 Code = 0x00100000; // some mapped "code"
constexpr u32 Data = 0x08000000;

std::string Hash(const std::string& bytes) {
    return Reader::HashHex(
        std::span<const u8>{reinterpret_cast<const u8*>(bytes.data()), bytes.size()});
}

/// An engine with a few mapped code words and an empty profile to run Lua in.
struct Loaded : DetachedEngine {
    explicit Loaded(const std::string& source = "", u64 title_id = 0) : DetachedEngine{title_id} {
        for (u32 offset = 0; offset < 0x400; offset += 4) {
            memory.Write32(Code + offset, 0xE1A00000);
        }
        loaded = engine.LoadProfile("test.lua", source);
    }
    bool loaded;
};

} // Anonymous namespace

TEST_CASE("Profiles have no file, process or debug access", "[reader][lua]") {
    Loaded t;
    REQUIRE(t.loaded);
    for (const char* name : {"io", "os", "package", "debug", "require", "dofile", "loadfile"}) {
        INFO(name);
        REQUIRE(t.Lua(name) == "nil");
    }
    // The safe libraries are there.
    REQUIRE(t.Lua("type(string.format), type(table.concat), type(math.floor), type(utf8.char), "
                  "type(coroutine.wrap)") == "function\tfunction\tfunction\tfunction\tfunction");

    // Source text can be loaded, with or without an environment; compiled chunks cannot.
    REQUIRE(t.Lua("load('return 1 + 1')()") == "2");
    REQUIRE(t.Lua("load('return x', 'chunk', 't', {x = 7})()") == "7");
    REQUIRE(t.Lua("load('return x', 'chunk', 'b', {x = 7})()") == "7");
    REQUIRE_THAT(t.Lua("load(string.dump(function() return 1 end))"),
                 ContainsSubstring("binary chunk"));
    REQUIRE_THAT(t.Lua("load(string.dump(function() return 1 end), 'c', 'b')"),
                 ContainsSubstring("binary chunk"));

    // print has no console to write to and goes to the log instead.
    t.Lua("print('value', 12)");
    REQUIRE(t.CountLogs(LogLevel::Info, "value 12") == 1);
}

TEST_CASE("The title ID is available to a profile", "[reader][lua]") {
    Loaded t{"", 0x0004000000088A00ULL};
    REQUIRE(t.Lua("reader.title_id") == "0004000000088A00");
}

TEST_CASE("A profile that fails to load leaves nothing behind", "[reader][lua]") {
    DetachedEngine t;
    t.memory.Write32(Code, 0xE1A00000);

    REQUIRE_FALSE(t.engine.LoadProfile("bad.lua", "this is not Lua"));
    REQUIRE_THAT(t.engine.LastError(), ContainsSubstring("bad.lua:1:"));
    REQUIRE_FALSE(t.engine.HasProfile());

    REQUIRE_FALSE(t.engine.LoadProfile(
        "bad.lua", "reader.hook(0x00100000, function() end)\nerror('stop here')"));
    REQUIRE_THAT(t.engine.LastError(), ContainsSubstring("stop here"));
    REQUIRE_FALSE(t.engine.HasProfile());
    REQUIRE(t.engine.HookCount() == 0);
    REQUIRE_FALSE(t.engine.IsHooked(Code));

    // Hooking an address the game has not mapped is a mistake in the profile.
    REQUIRE_FALSE(t.engine.LoadProfile("bad.lua", "reader.hook(0x00200000, function() end)"));
    REQUIRE_THAT(t.engine.LastError(), ContainsSubstring("00200000 is not mapped"));

    // So is hooking a conditional instruction (here: addeq), which could not be honoured. In
    // Thumb code, marked by an odd address, that is only known once the code is translated.
    t.memory.Write32(Code + 4, 0x02823001);
    REQUIRE_FALSE(t.engine.LoadProfile("bad.lua", "reader.hook(0x00100004, function() end)"));
    REQUIRE_THAT(t.engine.LastError(),
                 ContainsSubstring("instruction at 00100004 (02823001) is conditional"));
    REQUIRE(t.engine.LoadProfile("thumb.lua", "reader.hook(0x00100005, function() end)"));

    // A good profile still loads afterwards.
    REQUIRE(t.engine.LoadProfile("good.lua", "reader.hook(0x00100000, function() end)"));
    REQUIRE(t.engine.HookCount() == 1);
    REQUIRE(t.engine.IsHooked(Code));
}

TEST_CASE("Hooks receive the registers and survive errors", "[reader][lua]") {
    Loaded t{R"lua(
        seen = {}
        reader.hook(0x00100000, function(cpu)
            seen[#seen + 1] = string.format("%d %d %08X %08X %08X", cpu.r0, cpu.r12, cpu.sp, cpu.lr, cpu.pc)
        end)
        -- An odd address means Thumb code; the instruction is at the even address.
        reader.hook(0x00100011, function(cpu) thumb = cpu.pc end)

        calls = 0
        fail = true
        reader.hook(0x00100020, function()
            calls = calls + 1
            if fail then error("boom") end
        end)
    )lua"};
    REQUIRE(t.loaded);
    REQUIRE(t.engine.HookCount() == 3);

    t.Fire(Code, {{0, 7}, {12, 0xFFFFFFFF}, {13, 0x0FFFFF00}, {14, 0x00123456}});
    REQUIRE(t.Lua("seen[1]") == "7 4294967295 0FFFFF00 00123456 00100000");
    t.Fire(Code + 0x10);
    REQUIRE(t.Lua("thumb") == "1048592");
    t.Fire(Code + 0x40); // not hooked
    REQUIRE(t.Lua("#seen") == "1");

    // Four failures, a success, four more failures: still running.
    for (int i = 0; i < 4; ++i) {
        t.Fire(Code + 0x20);
    }
    t.Lua("fail = false");
    t.Fire(Code + 0x20);
    t.Lua("fail = true");
    for (int i = 0; i < 4; ++i) {
        t.Fire(Code + 0x20);
    }
    REQUIRE(t.Lua("calls") == "9");
    REQUIRE(t.CountLogs(LogLevel::Error, "disabled") == 0);
    REQUIRE(t.CountLogs(LogLevel::Error, "boom") == 8);
    // The error says where it happened.
    REQUIRE(t.CountLogs(LogLevel::Error, "test.lua:13: boom") == 8);

    // The fifth failure in a row switches the hook off; the others keep working.
    t.Fire(Code + 0x20);
    REQUIRE(t.Lua("calls") == "10");
    REQUIRE(t.CountLogs(LogLevel::Error, "hook at 00100020 disabled after 5 consecutive errors") ==
            1);
    t.Lua("fail = false");
    t.Fire(Code + 0x20);
    REQUIRE(t.Lua("calls") == "10");
    t.Fire(Code, {{0, 8}});
    REQUIRE(t.Lua("#seen") == "2");
}

TEST_CASE("Hooks see the floating-point registers, while the handler runs", "[reader][lua]") {
    Loaded t{R"lua(
        reader.hook(0x00100000, function(cpu)
            out = string.format("%.2f %.1f %g", cpu.s0, cpu.s31, cpu.d1)
            others = table.concat({ tostring(cpu.s32), tostring(cpu.r16), tostring(cpu.d16),
                                    tostring(cpu.r01), tostring(cpu.x), tostring(cpu[1]) }, " ")
            kept = cpu
        end)
    )lua"};
    REQUIRE(t.loaded);
    Reader::GuestRegisters regs;
    regs.r[15] = Code;
    regs.s[0] = 0x40490FDB;  // 3.14159274 as a float
    regs.s[31] = 0xC1200000; // -10.0
    regs.s[2] = 0;           // d1 = 2.5 as a double: low half ...
    regs.s[3] = 0x40040000;  // ... and high half
    t.engine.Dispatch(regs);
    REQUIRE(t.Lua("out") == "3.14 -10.0 2.5");
    REQUIRE(t.Lua("others") == "nil nil nil nil nil nil");
    // Keeping the register object for later is a mistake that is reported, not stale data.
    REQUIRE_THAT(t.Lua("kept.r0"), ContainsSubstring("only valid inside its hook handler"));
    t.engine.Dispatch(regs);
    REQUIRE(t.Lua("out") == "3.14 -10.0 2.5");
}

TEST_CASE("A handler may add hooks while it runs", "[reader][lua]") {
    Loaded t{R"lua(
        first, later = 0, 0
        reader.hook(0x00100000, function()
            first = first + 1
            if first == 1 then
                -- Enough new entries to make the engine's tables grow under the running handler.
                for address = 0x00100004, 0x001003FC, 4 do
                    reader.hook(address, function() later = later + 1 end)
                end
                reader.hook(0x00100000, function() later = later + 100 end)
            end
        end)
    )lua"};
    REQUIRE(t.loaded);
    t.Fire(Code);
    // The handler added on the same address runs from this very call on.
    REQUIRE(t.Lua("first, later") == "1\t100");
    t.Fire(Code + 0x3FC);
    t.Fire(Code);
    REQUIRE(t.Lua("first, later") == "2\t201");
}

TEST_CASE("A runaway callback is stopped", "[reader][lua]") {
    Loaded t{R"lua(
        reader.hook(0x00100000, function() while true do end end)
        -- Catching the error must not keep the callback alive.
        reader.hook(0x00100004, function()
            while true do pcall(function() while true do end end) end
        end)
        reader.hook(0x00100008, function()
            local co = coroutine.create(function() while true do end end)
            while true do coroutine.resume(co) end
        end)
        reader.hook(0x0010000C, function()
            while true do xpcall(function() while true do end end, function(e) return e end) end
        end)
        reader.hook(0x00100010, function() ok = true end)
        reader.on_frame(function() while true do end end)
    )lua"};
    REQUIRE(t.loaded);
    for (u32 offset = 0; offset < 0x10; offset += 4) {
        t.Fire(Code + offset);
    }
    REQUIRE(t.CountLogs(LogLevel::Error, "hook handler failed") == 4);
    REQUIRE(t.CountLogs(LogLevel::Error, "instruction budget") == 4);
    t.engine.OnFrame();
    REQUIRE(t.CountLogs(LogLevel::Error, "on_frame handler failed") == 1);

    // The next callback starts with a full budget.
    t.Fire(Code + 0x10);
    REQUIRE(t.Lua("ok") == "true");
    // pcall and friends still behave normally.
    REQUIRE(t.Lua("pcall(error, 'x')") == "false\tx");
    REQUIRE(t.Lua("select(2, xpcall(error, function(e) return e .. '!' end, 'x'))") == "x!");
    REQUIRE(t.Lua("coroutine.resume(coroutine.create(function(a) return a + 1 end), 1)") ==
            "true\t2");
}

TEST_CASE("A profile that never finishes loading is stopped", "[reader][lua]") {
    DetachedEngine t;
    REQUIRE_FALSE(t.engine.LoadProfile("loop.lua", "while true do end"));
    REQUIRE_THAT(t.engine.LastError(), ContainsSubstring("instruction budget"));
    REQUIRE_FALSE(t.engine.HasProfile());
}

TEST_CASE("A profile cannot use up the system's memory", "[reader][lua]") {
    DetachedEngine t;
    REQUIRE_FALSE(t.engine.LoadProfile(
        "hungry.lua",
        "local t = {} for i = 1, 100000 do t[i] = string.rep('x', 1 << 20) .. i end"));
    REQUIRE_THAT(t.engine.LastError(), ContainsSubstring("not enough memory"));
    REQUIRE_FALSE(t.engine.LoadProfile("hungry.lua", "local s = string.rep('x', 1 << 30)"));
    REQUIRE_THAT(t.engine.LastError(), ContainsSubstring("not enough memory"));

    // Reaching the limit inside a handler is an error of that handler, nothing more.
    t.memory.Write32(Code, 0xE1A00000);
    REQUIRE(t.engine.LoadProfile("hungry.lua", R"lua(
        hoard = {}
        reader.hook(0x00100000, function()
            while true do hoard[#hoard + 1] = string.rep('x', 1 << 20) .. #hoard end
        end)
    )lua"));
    t.Fire(Code);
    REQUIRE(t.CountLogs(LogLevel::Error, "hook handler failed: not enough memory") == 1);
    // Still at the limit: calling in again fails cleanly too.
    t.Fire(Code);
    REQUIRE(t.CountLogs(LogLevel::Error, "hook handler failed: not enough memory") == 2);
    REQUIRE(t.Lua("hoard = nil collectgarbage() return 1 + 1") == "2");
}

TEST_CASE("Guest memory is read safely", "[reader][lua]") {
    Loaded t;
    const u8 bytes[]{0x01, 0x02, 0x03, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 'A',  'B', 0x00,
                     'C',  0x42, 0x30, 0x44, 0x30, 0x00, 0x00, 0x46, 0x30, 0x7F};
    t.memory.Write(Data, bytes);

    REQUIRE(t.Lua("mem.u8(0x08000000), mem.u16(0x08000000), mem.u32(0x08000000)") ==
            "1\t513\t67305985");
    REQUIRE(t.Lua("mem.u32(0x08000004), mem.s32(0x08000004)") == "4294967295\t-1");
    // Unmapped, or mapped only in part.
    REQUIRE(
        t.Lua(
            "mem.u8(0x07FFFFFF), mem.u32(0x07FFFFFE), mem.u32(0x08000012), mem.s32(0x09000000)") ==
        "nil\tnil\tnil\tnil");
    REQUIRE(t.Lua("mem.u8(0x08000014)") == "127");
    REQUIRE(
        t.Lua("#mem.read(0x08000000, 4), #mem.read(0x08000000, 100), #mem.read(0x09000000, 4)") ==
        "4\t21\t0");
    REQUIRE(t.Lua("mem.read(0x08000008, 2)") == "AB");
    REQUIRE(t.Lua("#mem.read(0x08000000, 0)") == "0");

    REQUIRE(t.Lua("mem.cstr(0x08000008)") == "AB");
    REQUIRE(t.Lua("mem.cstr(0x08000008, 1)") == "A");
    REQUIRE(t.Lua("mem.cstr(0x0800000A)") == "");
    REQUIRE(t.Lua("#mem.cstr(0x0800000B)") == "5"); // runs to the next zero byte
    REQUIRE(t.Lua("text.utf16le(mem.wstr(0x0800000C))") == "あい");
    REQUIRE(t.Lua("#mem.wstr(0x0800000C, 2)") == "2");
    // No terminator before the end of the mapping: what is there, whole units only.
    REQUIRE(t.Lua("#mem.wstr(0x08000012)") == "2");
    REQUIRE(t.Lua("#mem.cstr(0x09000000)") == "0");

    REQUIRE_THAT(t.Lua("mem.u32(-1)"), ContainsSubstring("address out of range"));
    REQUIRE_THAT(t.Lua("mem.u32(0x100000000)"), ContainsSubstring("address out of range"));
    REQUIRE_THAT(t.Lua("mem.read(0x08000000, -1)"), ContainsSubstring("size out of range"));
    REQUIRE_THAT(t.Lua("mem.read(0x08000000, 1 << 21)"), ContainsSubstring("size out of range"));
    REQUIRE_THAT(t.Lua("mem.u32('x')"), ContainsSubstring("number expected"));

    // A read near the top of the address space does not wrap around.
    t.memory.Write(0xFFFFFFFE, std::string_view{"\x11\x22", 2});
    REQUIRE(t.Lua("mem.u16(0xFFFFFFFE), mem.u32(0xFFFFFFFE), #mem.cstr(0xFFFFFFFE)") ==
            "8721\tnil\t2");
}

TEST_CASE("Text helpers decode what games store", "[reader][lua]") {
    Loaded t;
    REQUIRE(t.Lua(R"(text.sjis("\x82\xA0\x8A\xBF\x8E\x9A"))") == "あ漢字");
    REQUIRE(t.Lua(R"(text.utf16le("\x42\x30\x41\x00"))") == "あA");
    REQUIRE(t.Lua(R"(text.utf16be("\x30\x42\x00\x41"))") == "あA");
    REQUIRE(t.Lua("text.len('あ漢字a')") == "4");
    REQUIRE(t.Lua("text.hash('')") == "cbf29ce484222325");
    REQUIRE(t.Lua("text.hash('a') ~= text.hash('b')") == "true");

    REQUIRE(t.Lua(R"(
        local b = reader.builder()
        b:add("あ"):add(nil):add("")
        b:add_ruby("漢字", "かんじ")
        b:add_ruby("仮", "")
        b:add("い")
        local r = b:ruby()
        return b:text(), b:length(), #r, r[1][1], r[1][2], r[1][3]
    )") == "あ漢字仮い\t5\t1\t1\t2\tかんじ");

    REQUIRE(t.Lua(R"(
        local out = {}
        out[#out + 1] = reader.changed("a", 1)
        out[#out + 1] = reader.changed("a", 1)
        out[#out + 1] = reader.changed("a", 2)
        out[#out + 1] = reader.changed("b", 2)
        reader.forget("a")
        out[#out + 1] = reader.changed("a", 2)
        out[#out + 1] = reader.changed("b", 2)
        reader.forget()
        out[#out + 1] = reader.changed("b", 2)
        for i, v in ipairs(out) do out[i] = v and "T" or "F" end
        return table.concat(out)
    )") == "TFTTTFT");
}

TEST_CASE("Emitted text becomes a well-formed event", "[reader][lua]") {
    Loaded t;

    t.Lua("reader.emit{ text = 'あい' }");
    auto events = t.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].sequence == 1);
    REQUIRE(events[0].frame == 0);
    REQUIRE(events[0].channel == "dialogue");
    REQUIRE(events[0].speaker.empty());
    REQUIRE(events[0].text == "あい");
    REQUIRE(events[0].ruby.empty());
    REQUIRE(events[0].meta_json.empty());
    // Without an id, the same line gets the same id every time, and another speaker another.
    REQUIRE(events[0].source_id == Hash(std::string{"dialogue\0\0あい", 16}));
    t.Lua("reader.emit{ text = 'あい' } reader.emit{ text = 'あい', speaker = '甲' }");
    events = t.TakeEvents();
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].sequence == 2);
    REQUIRE(events[0].source_id == Hash(std::string{"dialogue\0\0あい", 16}));
    REQUIRE(events[1].source_id != events[0].source_id);

    t.engine.OnFrame();
    t.engine.OnFrame();
    REQUIRE(t.Lua("reader.frame()") == "2");
    t.Lua(R"lua(reader.emit{
        text = "ab\nc", speaker = "S", channel = "choice", id = "x1",
        ruby = { {0, 1, "A"} },
        meta = { n = 1, f = 1.5, s = 'q"', b = true, t = {}, [1] = "no name" },
    })lua");
    events = t.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].frame == 2);
    REQUIRE(events[0].ToJson() ==
            R"({"seq":4,"frame":2,"audio":0,"channel":"choice","speaker":"S","text":"ab\nc",)"
            R"("ruby":[[0,1,"A"]],"id":"x1","meta":{"b":true,"f":1.5,"n":1,"s":"q\""}})");

    // Nothing to show: no event, and no sequence number used up.
    t.Lua("reader.emit{} reader.emit{ text = '' }");
    REQUIRE(t.TakeEvents().empty());
    // Numbers are fine where strings are expected; bytes that are not UTF-8 are replaced.
    t.Lua(R"(reader.emit{ text = 5, speaker = "\xFFz", id = "\xFF" })");
    events = t.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].sequence == 5);
    REQUIRE(events[0].text == "5");
    REQUIRE(events[0].speaker == "\xEF\xBF\xBDz");
    REQUIRE(events[0].source_id == "\xEF\xBF\xBD");

    // Mistakes in the profile are errors at the call, not broken events.
    REQUIRE_THAT(t.Lua("reader.emit('x')"), ContainsSubstring("table expected"));
    REQUIRE_THAT(t.Lua("reader.emit{ text = {} }"), ContainsSubstring("'text' must be a string"));
    REQUIRE_THAT(t.Lua("reader.emit{ text = 'a', ruby = 1 }"),
                 ContainsSubstring("'ruby' must be a table"));
    REQUIRE_THAT(t.Lua("reader.emit{ text = 'a', ruby = { 1 } }"),
                 ContainsSubstring("ruby[1] must be"));
    REQUIRE_THAT(t.Lua("reader.emit{ text = 'a', ruby = { {-1, 1, 'x'} } }"),
                 ContainsSubstring("ruby[1] must be"));
    REQUIRE_THAT(t.Lua("reader.emit{ text = 'a', meta = 1 }"),
                 ContainsSubstring("'meta' must be a table"));
    REQUIRE(t.TakeEvents().empty());
}

TEST_CASE("Ruby spans stay inside the text, in order, without overlap", "[reader][lua]") {
    Loaded t;
    t.Lua(R"lua(reader.emit{ text = "あいう", ruby = {
        {2, 1, "c"}, {0, 2, "ab"}, {1, 1, "overlaps"}, {2, 5, "too long"}, {3, 1, "outside"},
    } })lua");
    const auto events = t.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].ToJson() ==
            R"({"seq":1,"frame":0,"audio":0,"channel":"dialogue","speaker":"","text":"あいう",)"
            R"("ruby":[[0,2,"ab"],[2,1,"c"]],"id":")" +
                events[0].source_id + R"(","meta":{}})");
    REQUIRE(t.CountLogs(LogLevel::Warning, "ruby span") == 3);
}

TEST_CASE("A profile reports where the text is on screen", "[reader][lua]") {
    Loaded t;

    // A layout belongs to the text emitted for the same window.
    REQUIRE(t.Lua("reader.layout('w', { glyphs = {} })") == "false");
    REQUIRE(t.TakeLayouts().empty());

    t.Lua("reader.emit{ text = 'あ\\nいう', window = 'w' }");
    auto events = t.TakeEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == "w");
    REQUIRE_THAT(events[0].ToJson(), ContainsSubstring(R"("meta":{},"window":"w"})"));

    REQUIRE(t.Lua(R"lua(reader.layout("w", {
        screen = "top",
        box = { 0, 176, 400, 64 },
        glyphs = { {40, 191, 14, 21}, false, {40.5, 217.25, 15, 21}, {56, 217, 15, 21} },
    }))lua") == "true");
    auto layouts = t.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"w","seq":1,"space":[400,240],"box":[0,176,400,64],)"
                          R"("glyphs":[[40,191,14,21],null,[40.5,217.25,15,21],[56,217,15,21]]})");

    // Always one entry per code point: missing ones are empty, extra ones dropped, and
    // rectangles that make no sense are left out.
    t.Lua(R"lua(reader.layout("w", { screen = "bottom", glyphs = { {1, 2, 3, 4} } }))lua");
    t.Lua(R"lua(reader.layout("w", { glyphs = {
        {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {1, 2, 3, 4}, {9, 9, 9, 9}, {9, 9, 9, 9},
    } }))lua");
    t.Lua(R"lua(reader.layout("w", { box = { 0, 0, 1e9, 1 }, glyphs = {
        {0/0, 2, 3, 4}, {1, 2, -3, 4}, {1, 2, 3}, "x",
    } }))lua");
    layouts = t.TakeLayouts();
    REQUIRE(layouts.size() == 3);
    REQUIRE(layouts[0] ==
            R"({"window":"w","seq":1,"screen":1,"space":[320,240],"glyphs":[[1,2,3,4],null,null,null]})");
    REQUIRE(layouts[1] == R"({"window":"w","seq":1,"space":[400,240],)"
                          R"("glyphs":[[1,2,3,4],[1,2,3,4],[1,2,3,4],[1,2,3,4]]})");
    REQUIRE(layouts[2] == R"({"window":"w","seq":1,"space":[400,240],"glyphs":[null,null,null,null]})");

    // Closing is reported once, and only for something that was shown.
    t.Lua("reader.layout('w', nil) reader.layout('w', nil) reader.layout('other', nil)");
    layouts = t.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"w","seq":1,"closed":true})");

    // Numbers work as window keys, and new text takes over the window.
    t.Lua("reader.emit{ text = 'ab', window = 0x08272100 }");
    t.Lua("reader.layout(0x08272100, { glyphs = { {1, 1, 1, 1}, {2, 2, 2, 2} } })");
    t.Lua("reader.emit{ text = 'c', window = 0x08272100 }");
    t.Lua("reader.layout(0x08272100, { glyphs = { {3, 3, 3, 3} } })");
    layouts = t.TakeLayouts();
    REQUIRE(layouts.size() == 2);
    REQUIRE(layouts[0] ==
            R"({"window":"136782080","seq":2,"space":[400,240],"glyphs":[[1,1,1,1],[2,2,2,2]]})");
    REQUIRE(layouts[1] == R"({"window":"136782080","seq":3,"space":[400,240],"glyphs":[[3,3,3,3]]})");

    // A state load or the end of the profile takes what is on screen with it. After a state
    // load the text is gone too: a window's layout counts again once it has new text.
    t.engine.OnStateLoaded();
    layouts = t.TakeLayouts();
    REQUIRE(layouts.size() == 1);
    REQUIRE(layouts[0] == R"({"window":"136782080","seq":3,"closed":true})");
    t.Lua("reader.layout(0x08272100, { glyphs = { {3, 3, 3, 3} } })");
    REQUIRE(t.TakeLayouts().empty());
    t.Lua("reader.emit{ text = 'd', window = 0x08272100 }");
    t.Lua("reader.layout(0x08272100, { glyphs = { {3, 3, 3, 3} } })");
    t.engine.UnloadProfile();
    layouts = t.TakeLayouts();
    REQUIRE(layouts.size() == 2);
    REQUIRE(layouts[1] == R"({"window":"136782080","seq":4,"closed":true})");

    // Mistakes in the call are errors.
    Loaded again;
    again.Lua("reader.emit{ text = 'a', window = 'w' }");
    REQUIRE_THAT(again.Lua("reader.layout()"), ContainsSubstring("window key"));
    REQUIRE_THAT(again.Lua("reader.layout('w', 1)"), ContainsSubstring("table expected"));
    REQUIRE_THAT(again.Lua("reader.layout('w', {})"), ContainsSubstring("glyphs must be a list"));
    REQUIRE_THAT(again.Lua("reader.layout('w', { screen = 'left', glyphs = {} })"),
                 ContainsSubstring("screen must be"));
    REQUIRE_THAT(again.Lua("reader.layout('w', { box = {1, 2}, glyphs = {} })"),
                 ContainsSubstring("box must be"));
    REQUIRE(again.TakeLayouts().empty());
}

TEST_CASE("Frame and state callbacks run, and posted work runs on the next frame",
          "[reader][lua]") {
    Loaded t{R"lua(
        frames, loads = {}, 0
        reader.on_frame(function(n) frames[#frames + 1] = n end)
        reader.on_frame(function(n) if n == 2 then error("second frame") end end)
        reader.on_state_loaded(function() loads = loads + 1 end)
    )lua"};
    REQUIRE(t.loaded);
    int posted = 0;
    t.engine.Post([&] { ++posted; });
    REQUIRE(posted == 0);
    t.engine.OnFrame();
    REQUIRE(posted == 1);
    t.engine.OnFrame();
    t.engine.OnFrame();
    REQUIRE(posted == 1);
    REQUIRE(t.Lua("table.concat(frames, ',')") == "1,2,3");
    REQUIRE(t.CountLogs(LogLevel::Error, "second frame") == 1);
    t.engine.OnStateLoaded();
    REQUIRE(t.Lua("loads") == "1");
}

TEST_CASE("A frame callback that keeps failing is switched off", "[reader][lua]") {
    Loaded t{R"lua(
        calls, good = 0, 0
        reader.on_frame(function() calls = calls + 1 error("every frame") end)
        reader.on_frame(function() good = good + 1 end)
    )lua"};
    REQUIRE(t.loaded);
    for (int i = 0; i < 8; ++i) {
        t.engine.OnFrame();
    }
    REQUIRE(t.Lua("calls, good") == "5\t8");
    REQUIRE(t.CountLogs(LogLevel::Error, "every frame") == 5);
    REQUIRE(t.CountLogs(LogLevel::Error, "on_frame callback disabled after 5 consecutive errors") ==
            1);
}

TEST_CASE("Data files come from the frontend", "[reader][lua]") {
    Loaded t;
    REQUIRE(t.Lua("reader.data('table')") == "nil");
    t.engine.SetDataProvider([](const std::string& name) -> std::optional<std::string> {
        if (name == "table") {
            return std::string{"a\0b", 3};
        }
        return std::nullopt;
    });
    REQUIRE(t.Lua("#reader.data('table'), reader.data('other')") == "3\tnil");
}

TEST_CASE("Unloading a profile removes its hooks and nothing else", "[reader][lua]") {
    Loaded t{"reader.hook(0x00100000, function() reader.emit{ text = 'from profile' } end)"};
    REQUIRE(t.loaded);
    int native = 0;
    t.engine.AddNativeHook(Code, [&](const Reader::GuestRegisters&) { ++native; });
    t.engine.AddNativeHook(Code + 4, [&](const Reader::GuestRegisters&) { ++native; });
    REQUIRE(t.engine.HookCount() == 2); // addresses

    t.Fire(Code);
    REQUIRE(native == 1);
    REQUIRE(t.TakeEvents().size() == 1);

    t.engine.UnloadProfile();
    REQUIRE_FALSE(t.engine.HasProfile());
    REQUIRE(t.engine.HookCount() == 2);
    t.Fire(Code);
    t.Fire(Code + 4);
    REQUIRE(native == 3);
    REQUIRE(t.TakeEvents().empty());

    t.engine.ClearNativeHooks();
    REQUIRE(t.engine.HookCount() == 0);
    REQUIRE_FALSE(t.engine.IsHooked(Code));
}
