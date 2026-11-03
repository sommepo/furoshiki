// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <span>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "reader/core/text_codec.h"

namespace {

std::span<const u8> Bytes(const std::string& s) {
    return {reinterpret_cast<const u8*>(s.data()), s.size()};
}

} // Anonymous namespace

TEST_CASE("Shift-JIS is decoded to UTF-8", "[reader]") {
    // "あ漢字ｱA" as CP932: double-byte kana and kanji, half-width katakana, ASCII.
    const std::string sjis = "\x82\xA0\x8A\xBF\x8E\x9A\xB1"
                             "A";
    REQUIRE(Reader::SjisToUtf8(Bytes(sjis)) == "あ漢字ｱA");

    // The wave dash and other CP932-specific mappings follow Microsoft's table.
    REQUIRE(Reader::SjisToUtf8(Bytes("\x81\x60")) == "～");
    REQUIRE(Reader::SjisToUtf8(Bytes("\x81\x63")) == "…");

    // A lead byte at the end of the data and an unassigned code become U+FFFD.
    REQUIRE(Reader::SjisToUtf8(Bytes("\x82")) == "\xEF\xBF\xBD");
    REQUIRE(Reader::SjisToUtf8(Bytes("\x85\x9F")) == "\xEF\xBF\xBD");
    // A broken lead byte does not swallow the ASCII character or line break after it.
    REQUIRE(Reader::SjisToUtf8(Bytes("\x82"
                                     "A")) == "\xEF\xBF\xBD"
                                              "A");
    REQUIRE(Reader::SjisToUtf8(Bytes("\x85\x40")) == "\xEF\xBF\xBD@");
    REQUIRE(Reader::SjisToUtf8(Bytes("\x82\n\x82\xA0")) == "\xEF\xBF\xBD\nあ");
}

TEST_CASE("UTF-16 is decoded to UTF-8", "[reader]") {
    const std::array<u8, 8> le{0x42, 0x30, 0x41, 0x00, 0x42, 0xD8, 0xB7, 0xDF}; // あ A 𠮷
    REQUIRE(Reader::Utf16ToUtf8(le, false) == "あA𠮷");
    const std::array<u8, 4> be{0x30, 0x42, 0x00, 0x41};
    REQUIRE(Reader::Utf16ToUtf8(be, true) == "あA");
    // Lone surrogates and an odd trailing byte do not break the output.
    const std::array<u8, 5> broken{0x42, 0xD8, 0x41, 0x00, 0x99};
    REQUIRE(Reader::Utf16ToUtf8(broken, false) == "\xEF\xBF\xBD"
                                                  "A");
}

TEST_CASE("Malformed UTF-8 is replaced, valid UTF-8 kept", "[reader]") {
    REQUIRE(Reader::SanitizeUtf8("道を見張れ 𠮷 ok") == "道を見張れ 𠮷 ok");
    REQUIRE(Reader::SanitizeUtf8("a\xFF"
                                 "b") == "a\xEF\xBF\xBD"
                                         "b");
    REQUIRE(Reader::SanitizeUtf8("\xE3\x81") == "\xEF\xBF\xBD\xEF\xBF\xBD"); // truncated あ
    REQUIRE(Reader::SanitizeUtf8("\xC0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD"); // overlong NUL
    REQUIRE(Reader::SanitizeUtf8("\xED\xA0\x80") ==
            "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"); // a surrogate encoded as UTF-8
}

TEST_CASE("Code points are counted, not bytes", "[reader]") {
    REQUIRE(Reader::CountCodePoints("") == 0);
    REQUIRE(Reader::CountCodePoints("abc") == 3);
    REQUIRE(Reader::CountCodePoints("道を見張れ") == 5);
    REQUIRE(Reader::CountCodePoints("𠮷野家") == 3);
}

TEST_CASE("Hashes are stable 64-bit FNV-1a", "[reader]") {
    REQUIRE(Reader::HashHex(Bytes("")) == "cbf29ce484222325");
    REQUIRE(Reader::HashHex(Bytes("a")) == "af63dc4c8601ec8c");
    REQUIRE(Reader::HashHex(Bytes("foobar")) == "85944171f73967e8");
}

TEST_CASE("JSON strings are escaped", "[reader]") {
    std::string out;
    Reader::AppendJsonString(out, "a\"b\\c\n\t\x01名");
    REQUIRE(out == "a\\\"b\\\\c\\n\\t\\u0001名");
}
