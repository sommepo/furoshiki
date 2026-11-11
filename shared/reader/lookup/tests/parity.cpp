// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The Qt-free lookup (reader/lookup) against the desktop's (reader/desktop): both look up the
// same texts at every character in the same reader_dict.db, and every result must be the same
// (headword, reading, length matched, inflections, entries, each entry's compact glosses).
// Built against a fork's desktop library and Qt (tests/parity.sh). Prints each difference and
// the count; exits 1 on any.
//   parity DICT_DB [TEXT_FILE [GAME]]   (one text a line; without it, the texts below; GAME, a
//   game ID, looks up as while that game runs, with its names dictionaries; "-" for the texts below)

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/glossary.h"
#include "reader/lookup/dictionary.h"

namespace {

const char* const Texts[] = {
    "いきなりの対向車だ。",
    "「ダメだ！　ぶつかる！！」",
    "車体のうしろが大きくもっていかれる。",
    "車は道からはずれて、すべるように右のくさむらに飛び込んだ。",
    "奈美の横顔は、いつ見てもキレイだ。",
    "食べさせられなかった",
    "行ってしまいました",
    "読みたくない",
    "来なかった",
    "勉強しておけば",
    "見ちゃった",
    "ＡＢＣを使う",
    "感じられない",
    "美しければ",
};

std::string Describe(const std::string& expression, const std::string& reading, int length,
                     const std::vector<std::string>& reasons, const std::vector<std::string>& glosses) {
    std::string out = expression + "|" + reading + "|" + std::to_string(length) + "|";
    for (const std::string& reason : reasons) {
        out += reason + ",";
    }
    out += "|" + std::to_string(glosses.size());
    for (const std::string& gloss : glosses) {
        out += "|" + gloss;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "parity DICT_DB [TEXT_FILE]\n");
        return 2;
    }
    std::vector<std::string> texts(std::begin(Texts), std::end(Texts));
    const std::string game = argc > 3 ? argv[3] : "";
    if (argc > 2 && std::string(argv[2]) != "-") {
        texts.clear();
        std::ifstream file{argv[2]};
        for (std::string line; std::getline(file, line);) {
            if (!line.empty()) {
                texts.push_back(line);
            }
        }
    }
    ReaderApp::DictionaryDatabase qt_db{QString::fromUtf8(argv[1])};
    ReaderApp::DictionaryLookup qt_lookup{qt_db};
    ReaderLookup::Dictionary dictionary;
    std::string error;
    if (!dictionary.Open(argv[1], error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    int lookups = 0, found = 0, differences = 0;
    for (const std::string& text : texts) {
        const std::u16string utf16 = ReaderLookup::Utf16(text);
        for (std::size_t at = 0; at < utf16.size(); ++at) {
            const std::string from = ReaderLookup::Utf8(utf16.substr(at));
            const QList<ReaderApp::LookupResult> theirs = qt_lookup.Lookup(QString::fromStdString(from), 24, QString::fromStdString(game));
            const std::vector<ReaderLookup::LookupResult> ours = dictionary.Lookup(from, 24, game);
            ++lookups;
            found += !ours.empty();
            std::vector<std::string> a, b;
            for (const ReaderApp::LookupResult& r : theirs) {
                std::vector<std::string> reasons, glosses;
                for (const QString& reason : r.reasons) {
                    reasons.push_back(reason.toStdString());
                }
                for (const ReaderApp::TermEntry& entry : r.entries) {
                    for (const auto& compact : ReaderApp::Glossary::CompactDefinitions(entry.glossary_json)) {
                        glosses.push_back(compact.glosses.toStdString() + " / " + compact.notes.toStdString());
                    }
                }
                a.push_back((r.entries.isEmpty() || !r.entries.first().scoped ? "" : "name:") +
                            Describe(r.expression.toStdString(), r.reading.toStdString(),
                                     static_cast<int>(r.matched_text.size()), reasons, glosses));
            }
            for (const ReaderLookup::LookupResult& r : ours) {
                std::vector<std::string> glosses;
                for (const ReaderLookup::TermEntry& entry : r.entries) {
                    for (const auto& compact : ReaderLookup::CompactDefinitions(entry.glossary_json)) {
                        glosses.push_back(compact.glosses + " / " + compact.notes);
                    }
                }
                b.push_back((r.entries.empty() || !r.entries.front().scoped ? "" : "name:") +
                            Describe(r.expression, r.reading, r.matched_length, r.reasons, glosses));
            }
            if (a != b) {
                ++differences;
                std::printf("DIFFERENT at %s\n", from.c_str());
                for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
                    const std::string x = i < a.size() ? a[i] : "(none)";
                    const std::string y = i < b.size() ? b[i] : "(none)";
                    if (x != y) {
                        std::printf("  desktop: %.300s\n  vita:    %.300s\n", x.c_str(), y.c_str());
                    }
                }
            }
        }
    }
    std::printf("%d lookups, %d with a word, %d different\n", lookups, found, differences);
    return differences == 0 ? 0 : 1;
}
