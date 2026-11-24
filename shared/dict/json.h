// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Dict::Json {

/// Thrown when a bank is not JSON.
struct Malformed {};

const char* SkipSpace(const char* p, const char* end);
/// The end of the JSON value that begins at p (on its first character).
const char* ValueEnd(const char* p, const char* end);
/// The text of a JSON string literal (quotes included), unescaped, in UTF-8.
std::string StringText(std::string_view literal);
/// A string or number as text (a whole number without a point); anything else as "".
std::string Text(std::string_view value);
/// A number, or a string holding one, as a whole number (0 when it is neither).
std::int64_t Number(std::string_view value);
/// `text` as a JSON string literal, quotes included.
std::string Quote(std::string_view text);

/**
 * Calls each(fields) for every row in [p, end): rows (JSON arrays) one after another, with
 * commas and spaces between, as they are inside a bank. Anything not a row is passed by.
 */
template <typename Each>
void ForEachRowIn(const char* p, const char* end, Each&& each) {
    std::vector<std::string_view> fields;
    p = SkipSpace(p, end);
    while (p < end && *p != ']') {
        if (*p != '[') {
            p = SkipSpace(ValueEnd(p, end), end);
        } else {
            fields.clear();
            p = SkipSpace(p + 1, end);
            while (p < end && *p != ']') {
                const char* value_end = ValueEnd(p, end);
                fields.emplace_back(p, static_cast<std::size_t>(value_end - p));
                p = SkipSpace(value_end, end);
                if (p < end && *p == ',') {
                    p = SkipSpace(p + 1, end);
                }
            }
            if (p >= end) {
                throw Malformed{};
            }
            p = SkipSpace(p + 1, end);
            each(fields);
        }
        if (p < end && *p == ',') {
            p = SkipSpace(p + 1, end);
        }
    }
}

/// The inside of a bank (a JSON array): [begin, end) between its brackets. Throws Malformed.
std::pair<const char*, const char*> ArrayInside(std::string_view bank);

/**
 * The inside of a bank cut into pieces of about `piece` bytes, each a whole number of rows,
 * so that they can be read at the same time.
 */
std::vector<std::pair<const char*, const char*>> SplitRows(std::string_view bank, std::size_t piece);

/**
 * Calls each(fields) for every row of a bank (a JSON array of arrays), with the row's values
 * as views into `bank`. Anything else in the array is passed by. Throws Malformed.
 */
template <typename Each>
void ForEachRow(std::string_view bank, Each&& each) {
    const auto [begin, end] = ArrayInside(bank);
    ForEachRowIn(begin, end, each);
}

/// A parsed JSON value, for the small files (a dictionary's index.json, the library's own).
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    [[nodiscard]] const Value* Get(std::string_view key) const;
    [[nodiscard]] std::string String(std::string_view key, std::string fallback = {}) const;
    [[nodiscard]] double Number(std::string_view key, double fallback = 0) const;
    [[nodiscard]] bool Bool(std::string_view key, bool fallback = false) const;
};

/// Parses `text` into `out`; false when it is not JSON.
bool Parse(std::string_view text, Value& out);

} // namespace Dict::Json
