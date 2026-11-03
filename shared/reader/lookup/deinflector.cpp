// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

// reader/desktop/deinflector.cpp without Qt: its rule table converted as it is, its search the
// same steps on standard containers.

#include <functional>
#include <initializer_list>
#include <tuple>
#include <utility>
#include "reader/lookup/deinflector.h"

namespace ReaderLookup {

int WordClass::Parse(const std::string& rules) {
    int flags = 0;
    std::size_t at = 0;
    while (at <= rules.size()) {
        const std::size_t end = std::min(rules.find(' ', at), rules.size());
        const std::string rule = rules.substr(at, end - at);
        if (rule.starts_with("v1")) {
            flags |= V1;
        } else if (rule.starts_with("v5")) {
            flags |= V5;
        } else if (rule.starts_with("vs")) {
            flags |= VS;
        } else if (rule == "vk") {
            flags |= VK;
        } else if (rule == "vz") {
            flags |= VZ;
        } else if (rule == "adj-i" || rule == "adj-ix") {
            flags |= ADJ_I;
        }
        at = end + 1;
    }
    return flags;
}

namespace {

constexpr int MaxCandidates = 400;
constexpr int MaxDepth = 6;

// Godan verbs by dictionary ending.
const std::u16string U = u"うくぐすつぬぶむる";
const std::u16string A = u"わかがさたなばまら"; // negative stem
const std::u16string I = u"いきぎしちにびみり"; // masu stem
const std::u16string E = u"えけげせてねべめれ"; // potential / imperative / conditional
const std::u16string O = u"おこごそとのぼもろ"; // volitional stem
const std::vector<std::u16string> TeForm = {u"って", u"いて", u"いで",
                            u"して", u"って", u"んで",
                            u"んで", u"んで", u"って"};

std::u16string Key(const std::u16string& term, int conditions) {
    const std::string number = std::to_string(conditions);
    return term + u'\0' + std::u16string(number.begin(), number.end());
}

std::u16string S(const char16_t* text) {
    return text;
}

std::u16string One(char16_t c) {
    return std::u16string(1, c);
}

} // namespace

Deinflector::Deinflector() {
    using namespace WordClass;
    const auto rule = [this](const std::u16string& reason, const std::u16string& from, const std::u16string& to,
                             int class_in, int class_out) {
        rules.push_back(Rule{reason, from, to, class_in, class_out});
    };
    // Adds `reason` for all godan endings: stem row + `suffix` goes back to the u row.
    const auto godan = [&rule](const std::u16string& reason, const std::u16string& row, const std::u16string& suffix,
                               int class_in) {
        for (std::size_t i = 0; i < U.size(); ++i) {
            rule(reason, row[i] + suffix, One(U[i]), class_in, V5);
        }
    };
    // Adds `reason` for forms built on the te/ta sound change, e.g. った / んだ.
    const auto godan_euphonic = [&rule](const std::u16string& reason,
                                        const std::function<std::u16string(const std::u16string&)>& replace,
                                        int class_in) {
        for (std::size_t i = 0; i < U.size(); ++i) {
            rule(reason, replace(TeForm[i]), One(U[i]), class_in, V5);
        }
        // 行く is the one godan verb in く with って.
        rule(reason, replace(S(u"行って")), S(u"行く"), class_in, V5);
        rule(reason, replace(S(u"いって")), S(u"いく"), class_in, V5);
    };
    // する, くる and their kanji forms: `suru` and `kuru` are the inflected spellings.
    const auto irregular = [&rule](const std::u16string& reason, int class_in, const std::u16string& suru,
                                   const std::u16string& kuru) {
        rule(reason, suru, S(u"する"), class_in, VS);
        // じる / ずる verbs (感じる, 論ずる) share the し-stem pattern.
        if (suru.starts_with(u'し')) {
            rule(reason, u'じ' + suru.substr(1), S(u"ずる"), class_in, VZ);
        }
        rule(reason, kuru, S(u"くる"), class_in, VK);
        rule(reason, u'来' + kuru.substr(1), S(u"来る"), class_in, VK);
    };
    // The te-form with its last kana replaced: って → った, んで → んだ.
    const auto ending = [](const std::u16string& voiceless, const std::u16string& voiced) {
        return [voiceless, voiced](const std::u16string& te) {
            return te.substr(0, te.size() - 1) + (te.ends_with(u'で') ? voiced : voiceless);
        };
    };
    // ---- Negative: ない behaves as an i-adjective ----
    rule(S(u"negative"), S(u"ない"), S(u"る"), ADJ_I, V1);
    godan(S(u"negative"), A, S(u"ない"), ADJ_I);
    irregular(S(u"negative"), ADJ_I, S(u"しない"), S(u"こない"));
    rule(S(u"negative"), S(u"くない"), S(u"い"), ADJ_I, ADJ_I);
    rule(S(u"negative"), S(u"ない"), S(u"ある"), ADJ_I, V5); // ある has the negative ない

    // ---- Past ----
    rule(S(u"past"), S(u"た"), S(u"る"), 0, V1);
    godan_euphonic(S(u"past"), ending(S(u"た"), S(u"だ")), 0);
    irregular(S(u"past"), 0, S(u"した"), S(u"きた"));
    rule(S(u"past"), S(u"かった"), S(u"い"), 0, ADJ_I);

    // ---- Te-form ----
    rule(S(u"-te"), S(u"て"), S(u"る"), TE, V1);
    godan_euphonic(S(u"-te"), [](const std::u16string& te) { return te; }, TE);
    irregular(S(u"-te"), TE, S(u"して"), S(u"きて"));
    rule(S(u"-te"), S(u"くて"), S(u"い"), TE, ADJ_I);
    rule(S(u"-te"), S(u"ないで"), S(u"ない"), TE, ADJ_I);

    // ---- Progressive / perfect: built on the te-form, behaves as ichidan ----
    for (const auto& [from, to] : std::initializer_list<std::pair<std::u16string, std::u16string>>{
             {S(u"ている"), S(u"て")},
             {S(u"ておる"), S(u"て")},
             {S(u"てる"), S(u"て")},
             {S(u"とる"), S(u"て")},
             {S(u"でいる"), S(u"で")},
             {S(u"でおる"), S(u"で")},
             {S(u"でる"), S(u"で")},
             {S(u"どる"), S(u"で")}}) {
        rule(S(u"progressive or perfect"), from, to, V1, TE);
    }

    // ---- Auxiliaries on the te-form ----
    rule(S(u"-shimau"), S(u"てしまう"), S(u"て"), V5, TE);
    rule(S(u"-shimau"), S(u"でしまう"), S(u"で"), V5, TE);
    rule(S(u"-oku"), S(u"ておく"), S(u"て"), V5, TE);
    rule(S(u"-oku"), S(u"でおく"), S(u"で"), V5, TE);

    // ---- Contractions of てしまう and ておく ----
    for (const auto& [name, aux, voiced] :
         std::initializer_list<std::tuple<std::u16string, std::u16string, std::u16string>>{
             {S(u"-chau"), S(u"ちゃう"), S(u"じゃう")},
             {S(u"-chimau"), S(u"ちまう"), S(u"じまう")}}) {
        rule(name, aux, S(u"る"), V5, V1);
        godan_euphonic(name, ending(aux, voiced), V5);
        irregular(name, V5, u'し' + aux, u'き' + aux);
    }
    rule(S(u"-toku"), S(u"とく"), S(u"る"), V5, V1);
    godan_euphonic(S(u"-toku"), ending(S(u"とく"), S(u"どく")), V5);
    irregular(S(u"-toku"), V5, S(u"しとく"), S(u"きとく"));

    // ---- Polite ----
    for (const auto& [name, suffix] : std::initializer_list<std::pair<std::u16string, std::u16string>>{
             {S(u"polite"), S(u"ます")},
             {S(u"polite negative"), S(u"ません")},
             {S(u"polite past"), S(u"ました")},
             {S(u"polite past negative"), S(u"ませんでした")},
             {S(u"polite volitional"), S(u"ましょう")}}) {
        rule(name, suffix, S(u"る"), 0, V1);
        godan(name, I, suffix, 0);
        irregular(name, 0, u'し' + suffix, u'き' + suffix);
    }
    rule(S(u"polite negative"), S(u"くありません"), S(u"い"), 0, ADJ_I);
    rule(S(u"polite past negative"), S(u"くありませんでした"), S(u"い"), 0, ADJ_I);

    // ---- Desire: たい behaves as an i-adjective ----
    rule(S(u"-tai"), S(u"たい"), S(u"る"), ADJ_I, V1);
    godan(S(u"-tai"), I, S(u"たい"), ADJ_I);
    irregular(S(u"-tai"), ADJ_I, S(u"したい"), S(u"きたい"));

    // ---- Volitional ----
    rule(S(u"volitional"), S(u"よう"), S(u"る"), 0, V1);
    for (std::size_t i = 0; i < U.size(); ++i) {
        rule(S(u"volitional"), O[i] + S(u"う"), One(U[i]), 0, V5);
    }
    irregular(S(u"volitional"), 0, S(u"しよう"), S(u"こよう"));
    rule(S(u"volitional"), S(u"かろう"), S(u"い"), 0, ADJ_I);

    // ---- Imperative ----
    rule(S(u"imperative"), S(u"ろ"), S(u"る"), 0, V1);
    rule(S(u"imperative"), S(u"よ"), S(u"る"), 0, V1);
    for (std::size_t i = 0; i < U.size(); ++i) {
        rule(S(u"imperative"), One(E[i]), One(U[i]), 0, V5);
    }
    rule(S(u"imperative"), S(u"しろ"), S(u"する"), 0, VS);
    rule(S(u"imperative"), S(u"せよ"), S(u"する"), 0, VS);
    rule(S(u"imperative"), S(u"こい"), S(u"くる"), 0, VK);
    rule(S(u"imperative"), S(u"来い"), S(u"来る"), 0, VK);
    rule(S(u"imperative negative"), S(u"な"), std::u16string(), 0, ANY_VERB);

    // ---- Request: なさい ----
    rule(S(u"-nasai"), S(u"なさい"), S(u"る"), 0, V1);
    godan(S(u"-nasai"), I, S(u"なさい"), 0);
    irregular(S(u"-nasai"), 0, S(u"しなさい"), S(u"きなさい"));

    // ---- Potential, passive, causative: all behave as ichidan verbs ----
    rule(S(u"potential or passive"), S(u"られる"), S(u"る"), V1, V1);
    rule(S(u"potential"), S(u"れる"), S(u"る"), V1, V1); // colloquial 食べれる
    for (std::size_t i = 0; i < U.size(); ++i) {
        rule(S(u"potential"), E[i] + S(u"る"), One(U[i]), V1, V5);
    }
    godan(S(u"passive"), A, S(u"れる"), V1);
    rule(S(u"passive"), S(u"される"), S(u"する"), V1, VS);
    rule(S(u"potential or passive"), S(u"こられる"), S(u"くる"), V1, VK);
    rule(S(u"potential or passive"), S(u"来られる"), S(u"来る"), V1, VK);
    rule(S(u"potential"), S(u"これる"), S(u"くる"), V1, VK);
    rule(S(u"potential"), S(u"来れる"), S(u"来る"), V1, VK);
    rule(S(u"causative"), S(u"させる"), S(u"る"), V1, V1);
    godan(S(u"causative"), A, S(u"せる"), V1);
    rule(S(u"causative"), S(u"させる"), S(u"する"), V1, VS);
    rule(S(u"causative"), S(u"こさせる"), S(u"くる"), V1, VK);
    rule(S(u"causative"), S(u"来させる"), S(u"来る"), V1, VK);
    godan(S(u"causative"), A, S(u"す"), V5); // short causative 読ます
    godan(S(u"causative passive"), A, S(u"される"), V1);
    rule(S(u"causative passive"), S(u"させられる"), S(u"る"), V1, V1);
    rule(S(u"causative passive"), S(u"させられる"), S(u"する"), V1, VS);

    // ---- Conditionals ----
    rule(S(u"-ba"), S(u"れば"), S(u"る"), 0, V1);
    for (std::size_t i = 0; i < U.size(); ++i) {
        rule(S(u"-ba"), E[i] + S(u"ば"), One(U[i]), 0, V5);
    }
    rule(S(u"-ba"), S(u"すれば"), S(u"する"), 0, VS);
    rule(S(u"-ba"), S(u"くれば"), S(u"くる"), 0, VK);
    rule(S(u"-ba"), S(u"来れば"), S(u"来る"), 0, VK);
    rule(S(u"-ba"), S(u"ければ"), S(u"い"), 0, ADJ_I);
    rule(S(u"-ba"), S(u"きゃ"), S(u"い"), 0, ADJ_I); // なきゃ, colloquial ければ
    rule(S(u"-ba"), S(u"けりゃ"), S(u"い"), 0, ADJ_I);
    rule(S(u"-tara"), S(u"たら"), S(u"る"), 0, V1);
    godan_euphonic(S(u"-tara"), ending(S(u"たら"), S(u"だら")), 0);
    irregular(S(u"-tara"), 0, S(u"したら"), S(u"きたら"));
    rule(S(u"-tara"), S(u"かったら"), S(u"い"), 0, ADJ_I);
    rule(S(u"-tari"), S(u"たり"), S(u"る"), 0, V1);
    godan_euphonic(S(u"-tari"), ending(S(u"たり"), S(u"だり")), 0);
    irregular(S(u"-tari"), 0, S(u"したり"), S(u"きたり"));
    rule(S(u"-tari"), S(u"かったり"), S(u"い"), 0, ADJ_I);

    // ---- ては contractions ----
    rule(S(u"-cha"), S(u"ちゃ"), S(u"る"), 0, V1);
    godan_euphonic(S(u"-cha"), ending(S(u"ちゃ"), S(u"じゃ")), 0);
    irregular(S(u"-cha"), 0, S(u"しちゃ"), S(u"きちゃ"));
    rule(S(u"-cha"), S(u"くちゃ"), S(u"い"), 0, ADJ_I);

    // ---- Classical and colloquial negatives ----
    for (const auto& [name, suffix] : std::initializer_list<std::pair<std::u16string, std::u16string>>{
             {S(u"-zu"), S(u"ず")}, {S(u"-nu"), S(u"ぬ")}, {S(u"-n"), S(u"ん")}}) {
        rule(name, suffix, S(u"る"), 0, V1);
        godan(name, A, suffix, 0);
        rule(name, u'せ' + suffix, S(u"する"), 0, VS);
        rule(name, u'こ' + suffix, S(u"くる"), 0, VK);
        rule(name, u'来' + suffix, S(u"来る"), 0, VK);
    }
    for (const std::u16string& slang : {S(u"ねえ"), S(u"ねぇ"), S(u"ねー")}) {
        rule(S(u"slang"), slang, S(u"ない"), 0, ADJ_I);
    }
    for (const std::u16string& slang : {S(u"てえ"), S(u"てぇ"), S(u"てー")}) {
        rule(S(u"slang"), slang, S(u"たい"), 0, ADJ_I);
    }

    // ---- Appearance and excess ----
    rule(S(u"-sou"), S(u"そう"), S(u"る"), 0, V1);
    godan(S(u"-sou"), I, S(u"そう"), 0);
    irregular(S(u"-sou"), 0, S(u"しそう"), S(u"きそう"));
    rule(S(u"-sou"), S(u"そう"), S(u"い"), 0, ADJ_I);
    for (const std::u16string& sugiru : {S(u"すぎる"), S(u"過ぎる")}) {
        rule(S(u"-sugiru"), sugiru, S(u"る"), V1, V1);
        godan(S(u"-sugiru"), I, sugiru, V1);
        irregular(S(u"-sugiru"), V1, u'し' + sugiru, u'き' + sugiru);
        rule(S(u"-sugiru"), sugiru, S(u"い"), V1, ADJ_I);
    }

    // ---- Stems and adjective derivations ----
    rule(S(u"masu stem"), std::u16string(), S(u"る"), 0, V1);
    for (std::size_t i = 0; i < U.size(); ++i) {
        rule(S(u"masu stem"), One(I[i]), One(U[i]), 0, V5);
    }
    rule(S(u"masu stem"), S(u"し"), S(u"する"), 0, VS);
    rule(S(u"masu stem"), S(u"き"), S(u"くる"), 0, VK);
    rule(S(u"masu stem"), S(u"来"), S(u"来る"), 0, VK);
    rule(S(u"adverb"), S(u"く"), S(u"い"), 0, ADJ_I);
    rule(S(u"noun"), S(u"さ"), S(u"い"), 0, ADJ_I);
    rule(S(u"-ki"), S(u"き"), S(u"い"), 0, ADJ_I);
    rule(S(u"-ge"), S(u"げ"), S(u"い"), 0, ADJ_I);
    rule(S(u"-garu"), S(u"がる"), S(u"い"), V5, ADJ_I);

    for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
        const Rule& r = rules[i];
        if (r.suffix_in.empty()) {
            unconditional.push_back(i);
        } else {
            by_last_char[r.suffix_in.back()].push_back(i);
        }
    }
}

std::vector<Deinflection> Deinflector::Deinflect(const std::u16string& source) const {
    std::vector<Deinflection> results;
    std::unordered_set<std::u16string> seen;
    results.push_back(Deinflection{source, 0, {}});
    seen.insert(Key(source, 0));
    std::size_t index = 0;
    while (index < results.size() && static_cast<int>(results.size()) < MaxCandidates) {
        // A copy: the list grows while it is walked.
        const Deinflection current = results[index++];
        if (current.term.empty() || static_cast<int>(current.reasons.size()) >= MaxDepth) {
            continue;
        }
        const auto candidates = by_last_char.find(current.term.back());
        if (candidates != by_last_char.end()) {
            Apply(candidates->second, current, results, seen);
        }
        Apply(unconditional, current, results, seen);
    }
    return results;
}

void Deinflector::Apply(const std::vector<int>& candidates, const Deinflection& current,
                        std::vector<Deinflection>& results,
                        std::unordered_set<std::u16string>& seen) const {
    for (const int rule_index : candidates) {
        const Rule& rule = rules[rule_index];
        if (current.conditions != 0 && (current.conditions & rule.class_in) == 0) {
            continue;
        }
        if (current.conditions == 0 && !current.reasons.empty()) {
            continue;
        }
        if (!current.term.ends_with(rule.suffix_in)) {
            continue;
        }
        const int stem_length = static_cast<int>(current.term.size() - rule.suffix_in.size());
        if (stem_length + static_cast<int>(rule.suffix_out.size()) <= 0) {
            continue;
        }
        // A masu-stem guess must not grow out of nothing or repeat forever.
        if (rule.suffix_in.empty() && !current.reasons.empty()) {
            continue;
        }
        const std::u16string term = current.term.substr(0, stem_length) + rule.suffix_out;
        const std::u16string key = Key(term, rule.class_out);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        if (static_cast<int>(results.size()) >= MaxCandidates) {
            return;
        }
        std::vector<std::u16string> reasons = current.reasons;
        reasons.push_back(rule.reason);
        results.push_back(Deinflection{term, rule.class_out, reasons});
    }
}

std::u16string Utf16(const std::string& utf8) {
    std::u16string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        char32_t code;
        int extra;
        if (c < 0x80) {
            code = c, extra = 0;
        } else if ((c >> 5) == 6) {
            code = c & 0x1F, extra = 1;
        } else if ((c >> 4) == 14) {
            code = c & 0x0F, extra = 2;
        } else if ((c >> 3) == 30) {
            code = c & 0x07, extra = 3;
        } else {
            ++i; // not UTF-8: skipped
            continue;
        }
        if (i + extra >= utf8.size()) {
            break; // a character cut short at the end
        }
        for (int k = 1; k <= extra; ++k) {
            code = (code << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        i += extra + 1;
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(code));
        }
    }
    return out;
}

std::string Utf8(const std::u16string& utf16) {
    std::string out;
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        char32_t code = utf16[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < utf16.size()) {
            code = 0x10000 + ((code - 0xD800) << 10) + (utf16[++i] - 0xDC00);
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

} // namespace ReaderLookup
