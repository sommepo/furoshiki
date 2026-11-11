// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The desktop's dictionary lookup (reader/desktop/dictionary.*, glossary.*) without Qt, for an
// app that has none (the PS Vita's): the same reader_dict.db (the tables the desktop and
// Android apps write), the same query, the same unpacking of glossaries, the same scan of the
// text and ranking of what is found, and the desktop's compact definitions for a small popup.
// Not here: frequencies and pitch (term_meta), importing a dictionary.
//
// Reading the dictionary from a Vita's memory card costs some milliseconds a page (Rubi for
// Vita measured 3.7 ms a read): every candidate of a lookup goes in one query, and the
// connection keeps a page cache of its own (CacheKiB).

#include <cstdint>
#include <string>
#include <vector>
#include "reader/lookup/deinflector.h"

struct sqlite3;

namespace ReaderLookup {

/// One dictionary row for a term (the desktop's TermEntry), UTF-8.
struct TermEntry {
    std::int64_t dictionary_id{};
    std::string dictionary;
    int priority{};
    std::string expression;
    std::string reading;
    std::string definition_tags;
    std::string rules;
    int score{};
    std::string glossary_json;
    std::int64_t sequence{};
    std::string term_tags;
    /// From a dictionary for the running game only (a names dictionary).
    bool scoped{};
};

/// A definition cut down to one line (the desktop's Glossary::Compact).
struct Compact {
    std::string glosses;
    std::string notes;
};

/// The definitions of a glossary as short plain text (the desktop's CompactDefinitions).
[[nodiscard]] std::vector<Compact> CompactDefinitions(const std::string& glossary_json);

/// One headword found at the looked-up place (the desktop's LookupResult, without its
/// frequencies and pitch).
struct LookupResult {
    std::string expression;
    std::string reading;
    /// How much of the scanned text this headword covers, in UTF-16 units (as the desktop).
    int matched_length{};
    /// Inflections undone to reach the headword, outermost first.
    std::vector<std::string> reasons;
    /// Dictionary entries, most preferred dictionary first.
    std::vector<TermEntry> entries;

    /// The reading to show: empty when the expression is already kana.
    [[nodiscard]] std::string DisplayReading() const {
        return (reading.empty() || reading == expression) ? std::string() : reading;
    }
};

class Dictionary {
public:
    /// The page cache the connection keeps, in KiB.
    static constexpr int CacheKiB = 16 * 1024;
    /// Longest stretch of text tried as one word (the desktop's MaxScanLength).
    static constexpr int MaxScanLength = 20;

    Dictionary() = default;
    ~Dictionary();
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    /// Opens reader_dict.db read-only, with a page cache of `cache_kib`. False with `error` set
    /// when it cannot.
    bool Open(const std::string& path, std::string& error, int cache_kib = CacheKiB);
    [[nodiscard]] bool IsOpen() const {
        return db != nullptr;
    }

    /// Term rows of enabled dictionaries whose expression or reading is one of `terms`: of the
    /// dictionaries for every game, and of those scoped to `game` (a game ID) when one is given.
    [[nodiscard]] std::vector<TermEntry> FindTerms(const std::vector<std::string>& terms,
                                                   const std::string& game = {});

    /// The words starting at the first character of `text` (UTF-8): at most `max_results`,
    /// best first. `game` as the desktop's: its names dictionaries are used and come first.
    [[nodiscard]] std::vector<LookupResult> Lookup(const std::string& text, int max_results = 24,
                                                   const std::string& game = {});

    /// A stored glossary as its JSON (a first byte 0: as it is; 1: deflated).
    [[nodiscard]] static std::string Unpack(const void* blob, std::size_t size);

private:
    sqlite3* db = nullptr;
    /// Whether the file knows scopes (version 2 of its tables): an older one has none.
    bool has_scope = false;
    Deinflector deinflector;
};

/// Text helpers (the desktop's JapaneseText), on UTF-16 as there.
namespace JapaneseText {
[[nodiscard]] bool IsKanji(char32_t cp);
[[nodiscard]] bool IsKana(char16_t c);
/// A character a word may hold: kanji, kana, a letter or a digit. Without Qt's character
/// tables, "letter" is that of the scripts met in these games: Latin, Greek, Cyrillic and
/// their full-width forms.
[[nodiscard]] bool IsTermChar(char32_t cp);
[[nodiscard]] std::u16string KatakanaToHiragana(std::u16string text);
[[nodiscard]] std::u16string FullWidthToAscii(std::u16string text);
} // namespace JapaneseText

} // namespace ReaderLookup
