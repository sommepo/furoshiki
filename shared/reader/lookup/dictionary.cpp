// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

// reader/desktop/dictionary.cpp's lookup and glossary.cpp's compact definitions without Qt:
// each function here says which of those it follows; the logic is theirs, step for step.

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <zlib.h>
#include "reader/lookup/dictionary.h"

namespace ReaderLookup {

namespace {

constexpr char PackRaw = 0;
constexpr int MaxParameters = 400;

std::string Placeholders(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += i == 0 ? "?" : ",?";
    }
    return out;
}

std::string Column(sqlite3_stmt* statement, int column) {
    const unsigned char* text = sqlite3_column_text(statement, column);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

char32_t CodePointAt(const std::u16string& text, std::size_t index) {
    const char16_t c = text[index];
    if (c >= 0xD800 && c < 0xDC00 && index + 1 < text.size()) {
        return 0x10000 + ((static_cast<char32_t>(c) - 0xD800) << 10) + (text[index + 1] - 0xDC00);
    }
    return c;
}

int CharCountAt(const std::u16string& text, std::size_t index) {
    return CodePointAt(text, index) >= 0x10000 ? 2 : 1;
}

// ---- The scan (dictionary.cpp: Candidate, IsBetter, Scannable, Variants, Headword) ----

struct Candidate {
    int source_length;
    Deinflection deinflection;
};

bool IsBetter(const Candidate& a, const Candidate& b) {
    if (a.source_length != b.source_length) {
        return a.source_length > b.source_length;
    }
    return a.deinflection.reasons.size() < b.deinflection.reasons.size();
}

std::u16string Scannable(const std::u16string& text) {
    std::size_t end = 0;
    while (end < text.size() && end < static_cast<std::size_t>(Dictionary::MaxScanLength)) {
        const char32_t cp = CodePointAt(text, end);
        if (!JapaneseText::IsTermChar(cp) && cp != U'々' && cp != U'〇' && cp != U'・' &&
            cp != U'～' && cp != U'〜') {
            break;
        }
        end += CharCountAt(text, end);
    }
    return text.substr(0, end);
}

std::vector<std::u16string> Variants(const std::u16string& source) {
    std::vector<std::u16string> out;
    const auto add = [&out](const std::u16string& value) {
        if (std::find(out.begin(), out.end(), value) == out.end()) {
            out.push_back(value);
        }
    };
    add(source);
    add(JapaneseText::KatakanaToHiragana(source));
    const std::u16string ascii = JapaneseText::FullWidthToAscii(source);
    add(ascii);
    if (std::any_of(ascii.begin(), ascii.end(), [](char16_t c) { return c >= u'A' && c <= u'Z'; })) {
        std::u16string lower = ascii;
        for (char16_t& c : lower) {
            if (c >= u'A' && c <= u'Z') {
                c = static_cast<char16_t>(c + 32);
            }
        }
        add(lower);
    }
    return out;
}

struct Headword {
    std::string expression;
    std::string reading;
    int source_length{};
    std::vector<std::u16string> reasons;
    bool validated{};
    std::vector<TermEntry> entries;

    [[nodiscard]] int BestPriority() const {
        int best = std::numeric_limits<int>::max();
        for (const TermEntry& entry : entries) {
            best = std::min(best, entry.priority);
        }
        return best;
    }
    [[nodiscard]] int BestScore() const {
        int best = std::numeric_limits<int>::min();
        for (const TermEntry& entry : entries) {
            best = std::max(best, entry.score);
        }
        return best;
    }
};

// ---- Glossaries (glossary.cpp: Plain, OneLine, CollectMarked, CompactContent) ----

using Json = nlohmann::json;

std::string NumberText(const Json& node) {
    const double value = node.get<double>();
    if (value == static_cast<double>(static_cast<long long>(value))) {
        return std::to_string(static_cast<long long>(value));
    }
    return node.dump();
}

bool IsOneOf(const std::string& tag, std::initializer_list<const char*> tags) {
    return std::any_of(tags.begin(), tags.end(), [&tag](const char* t) { return tag == t; });
}

std::string StringField(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

void Plain(const Json& node, std::string& out) {
    if (node.is_string()) {
        out += node.get<std::string>();
    } else if (node.is_number()) {
        out += NumberText(node);
    } else if (node.is_boolean()) {
        out += node.get<bool>() ? "true" : "false";
    } else if (node.is_array()) {
        for (const Json& child : node) {
            Plain(child, out);
        }
    } else if (node.is_object()) {
        const std::string tag = StringField(node, "tag");
        const Json content = node.contains("content") ? node["content"] : Json();
        if (tag == "br") {
            out += '\n';
        } else if (IsOneOf(tag, {"img", "rt", "rp"})) {
            // left out
        } else if (IsOneOf(tag, {"li", "div", "p", "ul", "ol", "tr", "table", "details", "summary",
                                 "thead", "tbody", "tfoot"})) {
            out += '\n';
            Plain(content, out);
            out += '\n';
        } else {
            Plain(content, out);
            if (tag == "td" || tag == "th") {
                out += ' ';
            }
        }
    }
}

std::string PlainText(const Json& node) {
    std::string out;
    Plain(node, out);
    return out;
}

std::string Trimmed(const std::string& text) {
    const auto space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && space(text[begin])) {
        ++begin;
    }
    while (end > begin && space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string OneLine(const std::string& text) {
    std::string out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        const std::string line = Trimmed(text.substr(at, end - at));
        if (!line.empty()) {
            out += out.empty() ? line : "; " + line;
        }
        at = end + 1;
    }
    return out;
}

void CollectMarked(const Json& node, std::vector<const Json*>& glosses, std::vector<const Json*>& notes) {
    if (node.is_array()) {
        for (const Json& child : node) {
            CollectMarked(child, glosses, notes);
        }
    } else if (node.is_object()) {
        std::string mark;
        if (const auto data = node.find("data"); data != node.end() && data->is_object()) {
            mark = StringField(*data, "content");
        }
        if (mark == "glossary") {
            glosses.push_back(&node);
        } else if (IsOneOf(mark, {"notes", "infoGlossary", "sourceLanguages"})) {
            notes.push_back(&node);
        } else if (mark.empty() && node.contains("content")) {
            CollectMarked(node["content"], glosses, notes);
        }
        // Anything else is marked as something the compact popup leaves out.
    }
}

Compact CompactContent(const Json& content) {
    std::vector<const Json*> glosses;
    std::vector<const Json*> notes;
    CollectMarked(content, glosses, notes);
    if (glosses.empty()) {
        return Compact{OneLine(PlainText(content)), {}};
    }
    const auto join = [](const std::vector<const Json*>& nodes) {
        std::string out;
        for (const Json* node : nodes) {
            const std::string line = OneLine(PlainText(*node));
            out += out.empty() ? line : "; " + line;
        }
        return out;
    };
    return Compact{join(glosses), join(notes)};
}

} // namespace

// ---- JapaneseText ----

bool JapaneseText::IsKanji(char32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F) || cp == 0x3005 ||
           cp == 0x3007;
}

bool JapaneseText::IsKana(char16_t u) {
    const bool hiragana = (u >= u'ぁ' && u <= u'ゖ') || u == u'ゝ' || u == u'ゞ';
    const bool katakana = (u >= u'ァ' && u <= u'ヺ') || u == u'ヽ' || u == u'ヾ';
    return hiragana || katakana || u == u'ー';
}

bool JapaneseText::IsTermChar(char32_t cp) {
    if (IsKanji(cp) || (cp <= 0xFFFF && IsKana(static_cast<char16_t>(cp)))) {
        return true;
    }
    const bool ascii = (cp >= U'0' && cp <= U'9') || (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z');
    const bool full_width = (cp >= U'０' && cp <= U'９') || (cp >= U'Ａ' && cp <= U'Ｚ') ||
                            (cp >= U'ａ' && cp <= U'ｚ');
    const bool latin = cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7;
    const bool greek_cyrillic = (cp >= 0x370 && cp <= 0x3FF) || (cp >= 0x400 && cp <= 0x4FF);
    const bool half_width_kana = cp >= 0xFF66 && cp <= 0xFF9D;
    return ascii || full_width || latin || greek_cyrillic || half_width_kana;
}

std::u16string JapaneseText::KatakanaToHiragana(std::u16string text) {
    for (char16_t& c : text) {
        if ((c >= u'ァ' && c <= u'ヶ') || c == u'ヽ' || c == u'ヾ') {
            c = static_cast<char16_t>(c - 0x60);
        }
    }
    return text;
}

std::u16string JapaneseText::FullWidthToAscii(std::u16string text) {
    for (char16_t& c : text) {
        if (c >= u'！' && c <= u'～') {
            c = static_cast<char16_t>(c - 0xFEE0);
        }
    }
    return text;
}

// ---- Compact definitions (glossary.cpp: CompactDefinitions) ----

std::vector<Compact> CompactDefinitions(const std::string& glossary_json) {
    std::vector<Compact> out;
    const Json array = Json::parse(glossary_json, nullptr, false);
    if (!array.is_array()) {
        return out;
    }
    for (const Json& item : array) {
        Compact definition;
        if (item.is_string()) {
            definition = Compact{OneLine(item.get<std::string>()), {}};
        } else if (item.is_object()) {
            const std::string type = StringField(item, "type");
            if (type == "text") {
                definition = Compact{OneLine(StringField(item, "text")), {}};
            } else if (type == "structured-content" && item.contains("content")) {
                definition = CompactContent(item["content"]);
            }
        }
        if (!definition.glosses.empty()) {
            out.push_back(definition);
        }
    }
    return out;
}

// ---- The database (dictionary.cpp: DictionaryDatabase) ----

Dictionary::~Dictionary() {
    if (db != nullptr) {
        sqlite3_close(db);
    }
}

bool Dictionary::Open(const std::string& path, std::string& error, int cache_kib) {
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
    }
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        error = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    const std::string cache = "PRAGMA cache_size = -" + std::to_string(cache_kib);
    sqlite3_exec(db, cache.c_str(), nullptr, nullptr, nullptr);
    // A dictionary with terms: a file that is not one, or one never filled, is no use.
    sqlite3_stmt* statement = nullptr;
    const bool has_terms =
        sqlite3_prepare_v2(db, "SELECT 1 FROM terms LIMIT 1", -1, &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    if (!has_terms) {
        error = path + " has no dictionary terms";
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    return true;
}

std::vector<TermEntry> Dictionary::FindTerms(const std::vector<std::string>& terms) {
    std::vector<TermEntry> out;
    if (terms.empty() || db == nullptr) {
        return out;
    }
    std::vector<std::int64_t> seen;
    for (std::size_t from = 0; from < terms.size(); from += MaxParameters) {
        const std::size_t count = std::min<std::size_t>(MaxParameters, terms.size() - from);
        for (const char* column : {"expression", "reading"}) {
            const std::string sql =
                std::string("SELECT t.id, t.dict, d.title, d.priority, t.expression, t.reading, "
                            "t.def_tags, t.rules, t.score, t.glossary, t.sequence, t.term_tags "
                            "FROM terms t JOIN dictionaries d ON d.id = t.dict WHERE t.") +
                column + " IN (" + Placeholders(count) + ") AND d.enabled = 1";
            sqlite3_stmt* statement = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
                continue;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const std::string& term = terms[from + i];
                sqlite3_bind_text(statement, static_cast<int>(i) + 1, term.data(),
                                  static_cast<int>(term.size()), SQLITE_TRANSIENT);
            }
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const std::int64_t row = sqlite3_column_int64(statement, 0);
                if (std::find(seen.begin(), seen.end(), row) != seen.end()) {
                    continue;
                }
                seen.push_back(row);
                TermEntry entry;
                entry.dictionary_id = sqlite3_column_int64(statement, 1);
                entry.dictionary = Column(statement, 2);
                entry.priority = sqlite3_column_int(statement, 3);
                entry.expression = Column(statement, 4);
                entry.reading = Column(statement, 5);
                entry.definition_tags = Column(statement, 6);
                entry.rules = Column(statement, 7);
                entry.score = sqlite3_column_int(statement, 8);
                entry.glossary_json = Unpack(sqlite3_column_blob(statement, 9),
                                             static_cast<std::size_t>(sqlite3_column_bytes(statement, 9)));
                entry.sequence = sqlite3_column_int64(statement, 10);
                entry.term_tags = Column(statement, 11);
                out.push_back(std::move(entry));
            }
            sqlite3_finalize(statement);
        }
    }
    return out;
}

std::string Dictionary::Unpack(const void* blob, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(blob);
    if (bytes == nullptr || size == 0) {
        return "[]";
    }
    if (bytes[0] == PackRaw) {
        return std::string(reinterpret_cast<const char*>(bytes + 1), size - 1);
    }
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return "[]";
    }
    stream.next_in = const_cast<Bytef*>(bytes + 1);
    stream.avail_in = static_cast<uInt>(size - 1);
    std::string out;
    char buffer[4096];
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            break;
        }
        out.append(buffer, sizeof(buffer) - stream.avail_out);
    }
    inflateEnd(&stream);
    return result == Z_STREAM_END ? out : std::string("[]");
}

// ---- The lookup (dictionary.cpp: DictionaryLookup::Lookup) ----

std::vector<LookupResult> Dictionary::Lookup(const std::string& text, int max_results) {
    const std::u16string scan = Scannable(Utf16(text));
    if (scan.empty()) {
        return {};
    }

    // Every (prefix, deinflection) pair, keyed by the term to look for.
    std::unordered_map<std::u16string, std::vector<Candidate>> by_term;
    std::vector<std::u16string> terms; // in the order first met
    for (int length = static_cast<int>(scan.size()); length > 0; --length) {
        const char16_t last = scan[length - 1];
        if (last >= 0xD800 && last < 0xDC00) {
            continue;
        }
        const std::u16string source = scan.substr(0, length);
        for (const std::u16string& variant : Variants(source)) {
            for (const Deinflection& deinflection : deinflector.Deinflect(variant)) {
                auto found = by_term.find(deinflection.term);
                if (found == by_term.end()) {
                    found = by_term.emplace(deinflection.term, std::vector<Candidate>{}).first;
                    terms.push_back(deinflection.term);
                }
                found->second.push_back(Candidate{length, deinflection});
            }
        }
    }

    std::vector<std::string> utf8_terms;
    utf8_terms.reserve(terms.size());
    for (const std::u16string& term : terms) {
        utf8_terms.push_back(Utf8(term));
    }
    const std::vector<TermEntry> rows = FindTerms(utf8_terms);
    if (rows.empty()) {
        return {};
    }

    std::vector<Headword> headwords;
    std::unordered_map<std::string, int> headword_index;
    for (const TermEntry& row : rows) {
        const int word_class = WordClass::Parse(row.rules);
        const Candidate* best = nullptr;
        bool best_validated = false;
        for (const std::string& key : {row.expression, row.reading}) {
            if (key.empty()) {
                continue;
            }
            const auto found = by_term.find(Utf16(key));
            if (found == by_term.end()) {
                continue;
            }
            for (const Candidate& candidate : found->second) {
                const int required = candidate.deinflection.conditions;
                const bool exact = required == 0;
                const bool class_matches = exact || (required & word_class) != 0;
                // A dictionary without word classes cannot confirm an inflection itself; such
                // rows are only kept when another dictionary confirms the headword.
                const bool acceptable = class_matches || word_class == 0;
                if (!acceptable) {
                    continue;
                }
                const bool better = best == nullptr || (class_matches && !best_validated) ||
                                    (class_matches == best_validated && IsBetter(candidate, *best));
                if (better) {
                    best = &candidate;
                    best_validated = class_matches;
                }
            }
        }
        if (best == nullptr) {
            continue;
        }
        const std::string key = row.expression + '\0' + row.reading;
        auto index = headword_index.find(key);
        if (index == headword_index.end()) {
            headwords.push_back(Headword{row.expression, row.reading, 0, {}, false, {}});
            index = headword_index.emplace(key, static_cast<int>(headwords.size()) - 1).first;
        }
        Headword& headword = headwords[index->second];
        headword.entries.push_back(row);
        if (best_validated) {
            const bool improves =
                !headword.validated || best->source_length > headword.source_length ||
                (best->source_length == headword.source_length &&
                 best->deinflection.reasons.size() < headword.reasons.size());
            if (improves) {
                headword.source_length = best->source_length;
                headword.reasons = best->deinflection.reasons;
            }
            headword.validated = true;
        } else if (!headword.validated && best->source_length > headword.source_length) {
            headword.source_length = best->source_length;
            headword.reasons = best->deinflection.reasons;
        }
    }

    std::vector<Headword> confirmed;
    for (const Headword& headword : headwords) {
        if (headword.validated) {
            confirmed.push_back(headword);
        }
    }
    std::stable_sort(confirmed.begin(), confirmed.end(), [](const Headword& a, const Headword& b) {
        if (a.source_length != b.source_length) {
            return a.source_length > b.source_length;
        }
        if (a.reasons.size() != b.reasons.size()) {
            return a.reasons.size() < b.reasons.size();
        }
        const int a_priority = a.BestPriority();
        const int b_priority = b.BestPriority();
        if (a_priority != b_priority) {
            return a_priority < b_priority;
        }
        const int a_score = a.BestScore();
        const int b_score = b.BestScore();
        if (a_score != b_score) {
            return a_score > b_score;
        }
        return a.expression < b.expression;
    });
    if (static_cast<int>(confirmed.size()) > max_results) {
        confirmed.resize(max_results);
    }

    std::vector<LookupResult> out;
    for (Headword& headword : confirmed) {
        LookupResult result;
        result.expression = headword.expression;
        result.reading = headword.reading;
        result.matched_length = headword.source_length;
        for (const std::u16string& reason : headword.reasons) {
            result.reasons.push_back(Utf8(reason));
        }
        result.entries = std::move(headword.entries);
        std::stable_sort(result.entries.begin(), result.entries.end(),
                         [](const TermEntry& a, const TermEntry& b) {
                             if (a.priority != b.priority) {
                                 return a.priority < b.priority;
                             }
                             return a.score > b.score;
                         });
        out.push_back(std::move(result));
    }
    return out;
}

} // namespace ReaderLookup
