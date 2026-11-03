// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The desktop's deinflector (reader/desktop/deinflector.*) without Qt, for an app that has
// none (the PS Vita's): the same rules, one for one, on UTF-16 strings as QString has them, so
// that a rule's suffix is matched as there. tests/deinflector.cpp checks the two give the
// same candidates in the same order.

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReaderLookup {

/// Word classes as dictionaries tag them in a term's "rules" field.
namespace WordClass {
inline constexpr int V1 = 1;     ///< ichidan verb
inline constexpr int V5 = 2;     ///< godan verb
inline constexpr int VS = 4;     ///< suru verb
inline constexpr int VK = 8;     ///< kuru verb
inline constexpr int VZ = 16;    ///< zuru verb
inline constexpr int ADJ_I = 32; ///< i-adjective
/// Not a dictionary class: the te-form, so that 食べている can pass through 食べて.
inline constexpr int TE = 64;
inline constexpr int ANY_VERB = V1 | V5 | VS | VK | VZ;

/// Parses a term's space-separated rules ("v1 vt", "v5", "adj-i", "vs"...).
[[nodiscard]] int Parse(const std::string& rules);
} // namespace WordClass

/// A possible dictionary form of some text, with the inflections that were undone to reach it.
struct Deinflection {
    std::u16string term;
    /// Word classes the dictionary entry must have (WordClass bits); 0 accepts any entry.
    int conditions{};
    /// Inflection names, outermost first: 食べさせられた gives [past, passive, causative].
    std::vector<std::u16string> reasons;
};

/// Undoes Japanese inflection by suffix rewriting (the desktop's Deinflector, which says how).
class Deinflector {
public:
    Deinflector();

    /// All candidates for `source`, the unchanged text first.
    [[nodiscard]] std::vector<Deinflection> Deinflect(const std::u16string& source) const;

private:
    struct Rule {
        std::u16string reason;
        std::u16string suffix_in;
        std::u16string suffix_out;
        int class_in;
        int class_out;
    };

    void Apply(const std::vector<int>& candidates, const Deinflection& current,
               std::vector<Deinflection>& results, std::unordered_set<std::u16string>& seen) const;

    std::vector<Rule> rules;
    /// Rules by the last character of their inflected suffix; rules with no suffix apply to all.
    std::unordered_map<char16_t, std::vector<int>> by_last_char;
    std::vector<int> unconditional;
};

/// UTF-8 and UTF-16, both ways (the dictionary keeps UTF-8).
[[nodiscard]] std::u16string Utf16(const std::string& utf8);
[[nodiscard]] std::string Utf8(const std::u16string& utf16);

} // namespace ReaderLookup
