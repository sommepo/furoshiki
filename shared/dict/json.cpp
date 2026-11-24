// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Dict::Json {

namespace {

void PutUtf8(std::string& out, char32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

int Hex4(const char* p, const char* end) {
    if (end - p < 4) {
        throw Malformed{};
    }
    int value = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = p[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value |= c - 'A' + 10;
        } else {
            throw Malformed{};
        }
    }
    return value;
}

const char* StringEnd(const char* p, const char* end) {
    for (++p; p < end; ++p) {
        if (*p == '\\') {
            if (++p >= end) {
                throw Malformed{};
            }
        } else if (*p == '"') {
            return p + 1;
        }
    }
    throw Malformed{};
}

/// A JSON number (or the start of a string holding one) as a double, whatever the system's
/// locale says a decimal point is. Whole numbers come out exact.
double ToDouble(std::string_view text) {
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    bool negative = false;
    if (p < end && (*p == '-' || *p == '+')) {
        negative = *p == '-';
        ++p;
    }
    std::uint64_t mantissa = 0;
    int digits = 0;
    int exponent = 0;
    bool any = false;
    for (; p < end && *p >= '0' && *p <= '9'; ++p) {
        any = true;
        if (digits < 19) {
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(*p - '0');
            if (mantissa != 0) {
                ++digits;
            }
        } else {
            ++exponent;
        }
    }
    if (p < end && *p == '.') {
        for (++p; p < end && *p >= '0' && *p <= '9'; ++p) {
            any = true;
            if (digits < 19) {
                mantissa = mantissa * 10 + static_cast<std::uint64_t>(*p - '0');
                if (mantissa != 0) {
                    ++digits;
                }
                --exponent;
            }
        }
    }
    if (!any) {
        return 0;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        bool minus = false;
        if (p < end && (*p == '-' || *p == '+')) {
            minus = *p == '-';
            ++p;
        }
        int power = 0;
        for (; p < end && *p >= '0' && *p <= '9'; ++p) {
            power = std::min(power * 10 + (*p - '0'), 10000);
        }
        exponent += minus ? -power : power;
    }
    if (mantissa == 0) {
        return negative ? -0.0 : 0.0;
    }
    double value = static_cast<double>(mantissa);
    if (exponent != 0) {
        value *= std::pow(10.0, exponent);
    }
    return negative ? -value : value;
}

/// %.15g, as Qt's QByteArray::number(d, 'g', 15) writes it, whatever the locale.
std::string General15(double number) {
    char buffer[64];
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof buffer, number, std::chars_format::general, 15);
    return ec == std::errc{} ? std::string(buffer, ptr) : std::string("0");
}

} // namespace

const char* SkipSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        ++p;
    }
    return p;
}

const char* ValueEnd(const char* p, const char* end) {
    if (p >= end) {
        throw Malformed{};
    }
    if (*p == '"') {
        return StringEnd(p, end);
    }
    if (*p == '[' || *p == '{') {
        int depth = 0;
        for (; p < end; ++p) {
            const char c = *p;
            if (c == '"') {
                p = StringEnd(p, end) - 1;
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                if (--depth == 0) {
                    return p + 1;
                }
            }
        }
        throw Malformed{};
    }
    const char* start = p;
    while (p < end && *p != ',' && *p != ']' && *p != '}' && *p != ' ' && *p != '\n' && *p != '\r' &&
           *p != '\t') {
        ++p;
    }
    if (p == start) {
        throw Malformed{};
    }
    return p;
}

std::pair<const char*, const char*> ArrayInside(std::string_view bank) {
    const char* p = bank.data();
    const char* end = p + bank.size();
    if (end - p >= 3 && static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
        static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3; // a byte order mark
    }
    p = SkipSpace(p, end);
    if (p >= end || *p != '[') {
        throw Malformed{};
    }
    // The closing bracket: the last one, after the rows.
    const char* close = end;
    while (close > p && (close[-1] == ' ' || close[-1] == '\n' || close[-1] == '\r' || close[-1] == '\t')) {
        --close;
    }
    if (close <= p + 1 || close[-1] != ']') {
        throw Malformed{};
    }
    return {p + 1, close - 1};
}

std::vector<std::pair<const char*, const char*>> SplitRows(std::string_view bank, std::size_t piece) {
    const auto [begin, end] = ArrayInside(bank);
    std::vector<std::pair<const char*, const char*>> out;
    const char* start = begin;
    const char* p = SkipSpace(begin, end);
    while (p < end) {
        p = SkipSpace(ValueEnd(p, end), end);
        if (p < end && *p == ',') {
            ++p;
        }
        p = SkipSpace(p, end);
        if (static_cast<std::size_t>(p - start) >= piece || p >= end) {
            out.emplace_back(start, p);
            start = p;
        }
    }
    if (out.empty()) {
        out.emplace_back(begin, end);
    }
    return out;
}

std::string StringText(std::string_view literal) {
    if (literal.size() < 2) {
        throw Malformed{};
    }
    const char* p = literal.data() + 1;
    const char* stop = literal.data() + literal.size() - 1;
    const char* slash = static_cast<const char*>(std::memchr(p, '\\', static_cast<std::size_t>(stop - p)));
    if (slash == nullptr) {
        return std::string(p, stop);
    }
    std::string out(p, slash);
    out.reserve(literal.size());
    for (p = slash; p < stop; ++p) {
        if (*p != '\\') {
            out.push_back(*p);
            continue;
        }
        if (++p >= stop) {
            throw Malformed{};
        }
        switch (*p) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u': {
            char32_t code = static_cast<char32_t>(Hex4(p + 1, stop));
            p += 4;
            if (code >= 0xD800 && code < 0xDC00) {
                if (stop - p > 6 && p[1] == '\\' && p[2] == 'u') {
                    const int low = Hex4(p + 3, stop);
                    if (low >= 0xDC00 && low < 0xE000) {
                        code = 0x10000 + ((code - 0xD800) << 10) + static_cast<char32_t>(low - 0xDC00);
                        p += 6;
                    } else {
                        code = 0xFFFD;
                    }
                } else {
                    code = 0xFFFD;
                }
            } else if (code >= 0xDC00 && code < 0xE000) {
                code = 0xFFFD;
            }
            PutUtf8(out, code);
            break;
        }
        default: out.push_back(*p); break; // \" \\ \/
        }
    }
    return out;
}

std::string Text(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    if (value.front() == '"') {
        return StringText(value);
    }
    if (value.front() == '-' || (value.front() >= '0' && value.front() <= '9')) {
        const double number = ToDouble(value);
        char buffer[40];
        if (number == std::floor(number) && std::abs(number) < 9.0e15) {
            const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof buffer, static_cast<std::int64_t>(number));
            return std::string(buffer, ptr);
        }
        return General15(number);
    }
    return {};
}

std::int64_t Number(std::string_view value) {
    if (value.empty()) {
        return 0;
    }
    const double number = value.front() == '"' ? ToDouble(StringText(value)) : ToDouble(value);
    if (!std::isfinite(number)) {
        return 0;
    }
    if (number >= 9223372036854775808.0) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (number < -9223372036854775808.0) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return static_cast<std::int64_t>(number);
}

std::string Quote(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
    return out;
}

const Value* Value::Get(std::string_view key) const {
    for (const auto& [name, value] : object) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::string Value::String(std::string_view key, std::string fallback) const {
    const Value* value = Get(key);
    if (value == nullptr) {
        return fallback;
    }
    if (value->type == Type::String) {
        return value->string;
    }
    if (value->type == Type::Number) {
        char buffer[40];
        if (value->number == std::floor(value->number) && std::abs(value->number) < 9.0e15) {
            const auto [ptr, ec] =
                std::to_chars(buffer, buffer + sizeof buffer, static_cast<std::int64_t>(value->number));
            return std::string(buffer, ptr);
        }
        return General15(value->number);
    }
    return fallback;
}

double Value::Number(std::string_view key, double fallback) const {
    const Value* value = Get(key);
    return value != nullptr && value->type == Type::Number ? value->number : fallback;
}

bool Value::Bool(std::string_view key, bool fallback) const {
    const Value* value = Get(key);
    return value != nullptr && value->type == Type::Bool ? value->boolean : fallback;
}

namespace {

const char* ParseValue(const char* p, const char* end, Value& out, int depth) {
    if (depth > 64) {
        throw Malformed{};
    }
    p = SkipSpace(p, end);
    if (p >= end) {
        throw Malformed{};
    }
    if (*p == '"') {
        const char* stop = StringEnd(p, end);
        out.type = Value::Type::String;
        out.string = StringText(std::string_view(p, static_cast<std::size_t>(stop - p)));
        return stop;
    }
    if (*p == '[') {
        out.type = Value::Type::Array;
        p = SkipSpace(p + 1, end);
        while (p < end && *p != ']') {
            out.array.emplace_back();
            p = SkipSpace(ParseValue(p, end, out.array.back(), depth + 1), end);
            if (p < end && *p == ',') {
                ++p;
            }
            p = SkipSpace(p, end);
        }
        if (p >= end) {
            throw Malformed{};
        }
        return p + 1;
    }
    if (*p == '{') {
        out.type = Value::Type::Object;
        p = SkipSpace(p + 1, end);
        while (p < end && *p != '}') {
            if (*p != '"') {
                throw Malformed{};
            }
            const char* key_end = StringEnd(p, end);
            std::string key = StringText(std::string_view(p, static_cast<std::size_t>(key_end - p)));
            p = SkipSpace(key_end, end);
            if (p >= end || *p != ':') {
                throw Malformed{};
            }
            Value value;
            p = SkipSpace(ParseValue(p + 1, end, value, depth + 1), end);
            out.object.emplace_back(std::move(key), std::move(value));
            if (p < end && *p == ',') {
                ++p;
            }
            p = SkipSpace(p, end);
        }
        if (p >= end) {
            throw Malformed{};
        }
        return p + 1;
    }
    const char* stop = ValueEnd(p, end);
    const std::string_view word(p, static_cast<std::size_t>(stop - p));
    if (word == "true" || word == "false") {
        out.type = Value::Type::Bool;
        out.boolean = word == "true";
    } else if (word == "null") {
        out.type = Value::Type::Null;
    } else {
        out.type = Value::Type::Number;
        out.number = ToDouble(word);
    }
    return stop;
}

} // namespace

bool Parse(std::string_view text, Value& out) {
    out = Value{};
    const char* p = text.data();
    const char* end = p + text.size();
    if (end - p >= 3 && static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
        static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3;
    }
    try {
        ParseValue(p, end, out, 0);
        return true;
    } catch (const Malformed&) {
        out = Value{};
        return false;
    }
}

} // namespace Dict::Json
