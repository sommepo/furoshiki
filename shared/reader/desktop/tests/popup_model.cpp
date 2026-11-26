// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include "reader/desktop/glossary.h"
#include "reader/desktop/names.h"
#include "reader/desktop/popup_model.h"
#include "reader/desktop/settings.h"
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;

namespace {
TermEntry Sense(const QString& dictionary, const QString& tags, const QString& glossary) {
    TermEntry entry;
    entry.dictionary = dictionary;
    entry.definition_tags = tags;
    entry.glossary_json = glossary;
    return entry;
}
} // namespace

TEST_CASE("PopupModel: what the popup page draws for a headword", "[reader][popup]") {
    LookupResult result;
    result.expression = Q(u"死ぬほど");
    result.reading = Q(u"しぬほど");
    result.reasons = {QStringLiteral("masu")};
    result.frequencies = {Frequency{QStringLiteral("JPDB"), QStringLiteral("7621"), 7621.0, true}};
    result.pitches = {PitchAccent{QStringLiteral("NHK"), Q(u"しぬほど"), {0, 2}}};
    result.entries = {
        Sense(QStringLiteral("JMdict"), QStringLiteral("1 exp"), QStringLiteral(R"(["to death","to distraction"])")),
        Sense(QStringLiteral("JMdict"), QStringLiteral("2 exp"), QStringLiteral(R"(["like crazy"])")),
        Sense(QStringLiteral("JMdict"), QStringLiteral("forms"), Q(u"[\"死ぬほど | 死ぬ程★ | しぬほど(㊒)\"]")),
        Sense(QStringLiteral("Other"), QStringLiteral("n"), QStringLiteral(R"(["very much"])")),
    };
    const QJsonObject entry = PopupModel::Entry(result, QStringLiteral("Looked up once"));
    REQUIRE(entry.value(QStringLiteral("expression")).toString() == Q(u"死ぬほど"));
    REQUIRE(entry.value(QStringLiteral("reading")).toString() == Q(u"しぬほど"));
    REQUIRE(entry.value(QStringLiteral("history")).toString() == QStringLiteral("Looked up once"));
    REQUIRE(entry.value(QStringLiteral("pitches")).toArray().at(0).toString() == QStringLiteral("[0,2]"));
    REQUIRE(entry.value(QStringLiteral("frequencies")).toArray().at(0).toObject().value(QStringLiteral("value")).toString() ==
            QStringLiteral("7621"));
    REQUIRE(entry.value(QStringLiteral("state")).toString() == QStringLiteral("add"));

    const QJsonArray senses = entry.value(QStringLiteral("senses")).toArray();
    REQUIRE(senses.size() == 3);
    // Two dictionaries: each begins with its name. Numbers among the tags are left out, and a
    // tag the sense before had is not said again.
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("heading")).toString() == QStringLiteral("JMdict"));
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("tags")).toArray() == QJsonArray{QStringLiteral("exp")});
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("glosses")).toArray().size() == 2);
    REQUIRE(senses.at(1).toObject().value(QStringLiteral("number")).toInt() == 2);
    REQUIRE(senses.at(1).toObject().value(QStringLiteral("tags")).toArray().isEmpty());
    REQUIRE_FALSE(senses.at(1).toObject().contains(QStringLiteral("heading")));
    REQUIRE(senses.at(2).toObject().value(QStringLiteral("heading")).toString() == QStringLiteral("Other"));
    REQUIRE(senses.at(2).toObject().value(QStringLiteral("number")).toInt() == 1);

    // The other spellings, without JMdict's signs.
    REQUIRE(PopupModel::Forms(result) == QStringList{Q(u"死ぬほど"), Q(u"死ぬ程"), Q(u"しぬほど")});
    // No history: no line.
    REQUIRE_FALSE(PopupModel::Entry(result, {}).contains(QStringLiteral("history")));
    REQUIRE(PopupModel::StateName(2) == QStringLiteral("inDeck"));
    REQUIRE(PopupModel::StateName(9) == QStringLiteral("add"));
}

TEST_CASE("PopupModel: a name in the running game is tagged once", "[reader][popup]") {
    LookupResult result;
    result.expression = Q(u"紅莉栖");
    result.reading = Q(u"くりす");
    TermEntry name = Sense(Q(u"名前 · STEINS;GATE"), Q(u"名前"), Q(u"[\"牧瀬 紅莉栖（まきせ くりす）\"]"));
    name.scoped = true;
    result.entries = {name, Sense(QStringLiteral("JMdict"), QStringLiteral("n"), QStringLiteral(R"(["Chris"])"))};
    const QJsonObject entry = PopupModel::Entry(result, {});
    REQUIRE(entry.value(QStringLiteral("name")).toBool());
    const QJsonArray senses = entry.value(QStringLiteral("senses")).toArray();
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("name")).toBool());
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("tags")).toArray().isEmpty());
    REQUIRE_FALSE(senses.at(1).toObject().contains(QStringLiteral("name")));

    // The same entries without the game's dictionary first: no tag.
    result.entries = {result.entries.at(1)};
    REQUIRE_FALSE(PopupModel::Entry(result, {}).contains(QStringLiteral("name")));
}

TEST_CASE("PopupModel: a name of the running game is a card", "[reader][popup][names]") {
    // As tools/titles.py writes a name's one glossary item (card_gloss).
    const QString glossary = Q(
        u"[{\"type\":\"structured-content\",\"content\":{\"tag\":\"div\",\"data\":{\"furoshiki\":\"name\",\"id\":\"c53392\","
        u"\"kind\":\"person\",\"name\":\"真宮寺 是清\",\"reading\":\"しんぐうじ これきよ\",\"romaji\":\"Shinguuji Korekiyo\","
        u"\"role\":\"Main character\",\"also\":\"超高校級の「民俗学者」、是清\",\"about\":\"He studies folklore.\","
        u"\"image\":\"img/c53392.jpg\"},\"content\":[{\"tag\":\"div\",\"content\":\"真宮寺 是清（しんぐうじ これきよ）\"},"
        u"{\"tag\":\"div\",\"content\":\"Main character\"}]}}]");
    const Names::Card card = Names::CardOf(glossary);
    REQUIRE(card.found);
    REQUIRE(card.name == Q(u"真宮寺 是清"));
    REQUIRE(card.also == QStringList{Q(u"超高校級の「民俗学者」"), Q(u"是清")});
    REQUIRE(card.image == QStringLiteral("img/c53392.jpg"));
    REQUIRE_FALSE(Names::CardOf(QStringLiteral(R"(["a plain gloss"])")).found);
    REQUIRE_FALSE(Names::CardOf(QStringLiteral("not json")).found);
    // A picture is only ever one of a remembered dictionary's own img/ folder.
    REQUIRE(Names::Picture(QStringLiteral("no such dictionary"), QStringLiteral("img/c53392.jpg")).isEmpty());
    REQUIRE(Names::Picture(QStringLiteral("no such dictionary"), QStringLiteral("../index.json")).isEmpty());

    LookupResult result;
    result.expression = Q(u"真宮寺くん");
    result.reading = Q(u"しんぐうじくん");
    TermEntry name = Sense(Q(u"名前 · Test"), Q(u"名前"), glossary);
    name.scoped = true;
    result.entries = {name, Sense(QStringLiteral("JMdict"), QStringLiteral("n"), QStringLiteral(R"(["a temple"])"))};
    const QJsonObject entry = PopupModel::Entry(result, {});
    REQUIRE(entry.value(QStringLiteral("name")).toBool());
    const QJsonObject shown = entry.value(QStringLiteral("card")).toObject();
    REQUIRE(shown.value(QStringLiteral("name")).toString() == Q(u"真宮寺 是清"));
    REQUIRE(shown.value(QStringLiteral("reading")).toString() == Q(u"しんぐうじ これきよ"));
    REQUIRE(shown.value(QStringLiteral("role")).toString() == QStringLiteral("Main character"));
    REQUIRE(shown.value(QStringLiteral("also")).toArray().size() == 2);
    REQUIRE(shown.value(QStringLiteral("about")).toString() == QStringLiteral("He studies folklore."));
    REQUIRE_FALSE(shown.contains(QStringLiteral("others")));
    // Two people written and read the same: the second is a card too, in the first's "others".
    result.entries = {name, name};
    const QJsonArray others =
        PopupModel::Entry(result, {}).value(QStringLiteral("card")).toObject().value(QStringLiteral("others")).toArray();
    REQUIRE(others.size() == 1);
    REQUIRE(others.first().toObject().value(QStringLiteral("name")).toString() == Q(u"真宮寺 是清"));
    REQUIRE_FALSE(shown.contains(QStringLiteral("image"))); // no zip remembered: no picture, the card all the same
    // The name's own sense is the card's words as plain lines, for the popup drawn without the page.
    const QJsonArray senses = entry.value(QStringLiteral("senses")).toArray();
    REQUIRE(senses.size() == 2);
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("name")).toBool());
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("glosses")).toArray().at(0).toString().startsWith(Q(u"真宮寺 是清（")));

    // Where nothing draws cards (an Anki card's glossary, Furochibi's page): the same, line by line.
    const QString plain = Glossary::ToPlainText(glossary);
    REQUIRE(plain.contains(Q(u"真宮寺 是清（しんぐうじ これきよ）")));
    REQUIRE(plain.contains(QStringLiteral("Main character")));
    REQUIRE_FALSE(plain.contains(QStringLiteral("c53392")));

    // A names dictionary from before the cards: the tag, no card.
    name.glossary_json = Q(u"[\"真宮寺 是清（しんぐうじ これきよ）\"]");
    result.entries = {name};
    REQUIRE_FALSE(PopupModel::Entry(result, {}).contains(QStringLiteral("card")));
}

TEST_CASE("PopupModel: each sense says its dictionary's kind, for the popup's tabs", "[reader][popup]") {
    const auto kind = [](const QString& dictionary, const QString& expression = QStringLiteral("x"), bool scoped = false) {
        TermEntry entry = Sense(dictionary, {}, QStringLiteral("[]"));
        entry.expression = expression;
        entry.scoped = scoped;
        return PopupModel::KindOf(entry);
    };
    REQUIRE(kind(Q(u"日本語文法辞典(全集)")) == QStringLiteral("grammar"));
    REQUIRE(kind(QStringLiteral("Dictionary of Japanese Grammar")) == QStringLiteral("grammar"));
    REQUIRE(kind(QStringLiteral("JMnedict")) == QStringLiteral("name"));
    REQUIRE(kind(QStringLiteral("JMdict")) == QStringLiteral("word"));
    REQUIRE(kind(Q(u"漢字辞典"), Q(u"位")) == QStringLiteral("kanji"));
    REQUIRE(kind(Q(u"漢字辞典"), Q(u"位置")) == QStringLiteral("word"));
    // Titles the rule missed (2026-11-26): a pitch dictionary named after kanji is not a kanji
    // dictionary, 字源 is one, and two grammar dictionaries say neither 文法 nor grammar.
    REQUIRE(kind(QStringLiteral("Kanjium Pitch Accents"), Q(u"位")) == QStringLiteral("word"));
    REQUIRE(kind(Q(u"角川新字源 改訂新版"), Q(u"髪")) == QStringLiteral("kanji"));
    REQUIRE(kind(Q(u"旺文社漢字典 第四版"), Q(u"髪")) == QStringLiteral("kanji"));
    REQUIRE(kind(Q(u"[画像付き] 絵でわかる日本語")) == QStringLiteral("grammar"));
    REQUIRE(kind(Q(u"毎日のんびり日本語教師")) == QStringLiteral("grammar"));
    REQUIRE(kind(Q(u"JLPT文法解説まとめ")) == QStringLiteral("grammar"));
    REQUIRE(kind(Q(u"名前 · Game"), Q(u"紅莉栖"), true) == QStringLiteral("name"));
    // The user's kind wins.
    TermEntry other = Sense(Q(u"大辞林"), {}, QStringLiteral("[]"));
    REQUIRE(PopupModel::KindOf(other, {{Q(u"大辞林"), QStringLiteral("grammar")}}) == QStringLiteral("grammar"));

    // A grammar dictionary's sense carries its glossary whole, as the dictionary laid it out.
    LookupResult result;
    result.expression = Q(u"くらい");
    result.entries = {
        Sense(QStringLiteral("JMdict"), QStringLiteral("prt"), QStringLiteral(R"(["about"])")),
        Sense(QStringLiteral("Bunpro"), {},
              QStringLiteral(R"([{"type":"structured-content","content":{"tag":"div","data":{"content":"formation"},"content":"N + くらい"}}])")),
    };
    const QJsonArray senses = PopupModel::Senses(result);
    REQUIRE(senses.size() == 2);
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("word"));
    // A word's sense carries its glossary too, for the full layout (the compact one is the page's).
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("content")).toArray().at(0).toString() == QStringLiteral("about"));
    REQUIRE(senses.at(0).toObject().value(QStringLiteral("glosses")).toArray().at(0).toString() == QStringLiteral("about"));
    REQUIRE(senses.at(1).toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("grammar"));
    REQUIRE(senses.at(1).toObject().value(QStringLiteral("content")).toArray().at(0).toObject()
                .value(QStringLiteral("type")).toString() == QStringLiteral("structured-content"));
}

TEST_CASE("PopupModel: pitch accents with their readings, and a glossary's pictures", "[reader][popup]") {
    LookupResult result;
    result.expression = Q(u"食べる");
    result.reading = Q(u"たべる");
    result.pitches = {PitchAccent{QStringLiteral("Kanjium"), Q(u"たべる"), {2}}};
    TermEntry sense = Sense(QStringLiteral("Jitendex"), {},
                            QStringLiteral(R"([{"type":"structured-content","content":{"tag":"img","path":"a/b.png"}}])"));
    sense.media.insert(QStringLiteral("a/b.png"), QStringLiteral("data:image/png;base64,AAAA"));
    result.entries = {sense};
    const QJsonObject entry = PopupModel::Entry(result, {});
    const QJsonObject accent = entry.value(QStringLiteral("pitchAccents")).toArray().at(0).toObject();
    REQUIRE(accent.value(QStringLiteral("reading")).toString() == Q(u"たべる"));
    REQUIRE(accent.value(QStringLiteral("dictionary")).toString() == QStringLiteral("Kanjium"));
    REQUIRE(accent.value(QStringLiteral("positions")).toArray() == QJsonArray{2});
    REQUIRE(entry.value(QStringLiteral("pitches")).toArray().at(0).toString() == QStringLiteral("[2]"));
    const QJsonObject first = entry.value(QStringLiteral("senses")).toArray().at(0).toObject();
    REQUIRE(first.value(QStringLiteral("media")).toObject().value(QStringLiteral("a/b.png")).toString() ==
            QStringLiteral("data:image/png;base64,AAAA"));
}

TEST_CASE("ReaderSettings: the popup's look follows the shelf's theme unless chosen", "[reader][popup]") {
    REQUIRE(ReaderSettings::PopupThemeOfShelf(QStringLiteral("kon")) == QStringLiteral("kon"));
    REQUIRE(ReaderSettings::PopupThemeOfShelf(QStringLiteral("solarized")) == QStringLiteral("solarized-dark"));
    REQUIRE(ReaderSettings::PopupThemeOfShelf(QStringLiteral("paper")) == QStringLiteral("paper"));
    REQUIRE(ReaderSettings::PopupThemeOfShelf(QStringLiteral("mono")) == QStringLiteral("kumihan"));
    REQUIRE(ReaderSettings::PopupThemeOfShelf(QString()) == QStringLiteral("console"));
    ReaderSettings::SetPopupThemeDefault(QStringLiteral("kon"));
    REQUIRE(ReaderSettings::ResolvePopupTheme(QStringLiteral("auto")) == QStringLiteral("kon"));
    REQUIRE(ReaderSettings::ResolvePopupTheme(QString()) == QStringLiteral("kon"));
    REQUIRE(ReaderSettings::ResolvePopupTheme(QStringLiteral("glass")) == QStringLiteral("glass"));
    REQUIRE(ReaderSettings::ResolvePopupTheme(QStringLiteral("console")) == QStringLiteral("console"));
    ReaderSettings::SetPopupThemeDefault(QStringLiteral("console"));
    REQUIRE(ReaderSettings::ResolvePopupTheme(QStringLiteral("auto")) == QStringLiteral("console"));
}

// Not run by default: what the page is handed for real lookups in a real database, for pictures
// of the popup (each word's model as one JSON object a line, with the dictionaries' styles).
//   READER_MODEL_DB=reader_dict.db READER_MODEL_WORDS="ぶつぶつ,客" READER_MODEL_OUT=models.jsonl \
//     reader_desktop_tests "[.model]"
TEST_CASE("PopupModel: models of real lookups, for pictures", "[.model]") {
    const QString path = qEnvironmentVariable("READER_MODEL_DB");
    if (path.isEmpty()) {
        SKIP("READER_MODEL_DB names no database");
    }
    DictionaryDatabase db{path};
    DictionaryLookup lookup{db};
    QFile out(qEnvironmentVariable("READER_MODEL_OUT"));
    REQUIRE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QJsonObject styles;
    const QHash<QString, QString> sheets = db.Styles();
    for (auto it = sheets.cbegin(); it != sheets.cend(); ++it) {
        styles.insert(it.key(), it.value());
    }
    for (const QString& word : qEnvironmentVariable("READER_MODEL_WORDS").split(u',', Qt::SkipEmptyParts)) {
        QJsonArray entries;
        for (const LookupResult& result : lookup.Lookup(word, 6)) {
            entries.append(PopupModel::Entry(result, {}));
        }
        const QJsonObject model{{QStringLiteral("word"), word},
                                {QStringLiteral("entries"), entries},
                                {QStringLiteral("styles"), styles}};
        out.write(QJsonDocument(model).toJson(QJsonDocument::Compact) + '\n');
    }
}

// How long a hover takes with a real library: a lookup starting at every character of a few
// ordinary sentences (as Hachidori's JMdict benchmark does), then the popup's model of it.
//   READER_LOOKUP_DB=reader_dict.db reader_desktop_tests "[.lookup]"
TEST_CASE("PopupModel: lookup timing with a real library", "[.lookup]") {
    const QString path = qEnvironmentVariable("READER_LOOKUP_DB");
    if (path.isEmpty()) {
        SKIP("READER_LOOKUP_DB names no database");
    }
    DictionaryDatabase db{path};
    DictionaryLookup lookup{db};
    // READER_LOOKUP_MAX: results per lookup (24, the popup's; Hoshidicts' own bench uses 16).
    const int max = qEnvironmentVariableIntValue("READER_LOOKUP_MAX") > 0
                        ? qEnvironmentVariableIntValue("READER_LOOKUP_MAX")
                        : 24;
    QStringList sentences{
        QStringLiteral("吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"),
        QStringLiteral("何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。"),
        QStringLiteral("国境の長いトンネルを抜けると雪国であった。夜の底が白くなった。"),
        QStringLiteral("昨日は友達と駅前の喫茶店でコーヒーを飲みながら話していました。"),
        QStringLiteral("食べたかったけど、もう売り切れてしまったみたいで残念だった。"),
        QStringLiteral("彼女は振り返らずに、静かに部屋を出て行ってしまった。")};
    // READER_LOOKUP_TEXT: these sentences instead (one a line), to look at a dictionary's entries.
    if (const QString own = qEnvironmentVariable("READER_LOOKUP_TEXT"); !own.isEmpty()) {
        sentences = own.split(u'\n', Qt::SkipEmptyParts);
    }
    QStringList scans;
    for (const QString& sentence : sentences) {
        for (int i = 0; i < sentence.size(); ++i) {
            if (sentence[i] != u'、' && sentence[i] != u'。') {
                scans.append(sentence.mid(i, 16));
            }
        }
    }
    qint64 bytes = 0;
    const auto time = [&](bool model) {
        std::vector<double> times;
        for (const QString& scan : scans) {
            QElapsedTimer timer;
            timer.start();
            const QList<LookupResult> results = lookup.Lookup(scan, max);
            if (model) {
                QJsonArray entries;
                for (const LookupResult& result : results) {
                    entries.append(PopupModel::Entry(result, {}));
                }
                // As the popup sends it to its page.
                bytes += QJsonDocument(QJsonObject{{QStringLiteral("entries"), entries}}).toJson(QJsonDocument::Compact).size();
            }
            times.push_back(timer.nsecsElapsed() / 1e6);
        }
        std::sort(times.begin(), times.end());
        return times;
    };
    QElapsedTimer first;
    first.start();
    (void)lookup.Lookup(scans.first(), max);
    std::printf("first lookup: %.2f ms\n", first.nsecsElapsed() / 1e6);
    (void)time(false); // warm
    for (const bool model : {false, true}) {
        const std::vector<double> times = time(model);
        const auto at = [&times](double p) { return times[static_cast<size_t>(p * (times.size() - 1))]; };
        std::printf("%s: %zu scans, p50 %.2f ms, p90 %.2f ms, p99 %.2f ms, max %.2f ms\n",
                    model ? "lookup + popup model" : "lookup", times.size(), at(0.5), at(0.9), at(0.99),
                    times.back());
    }
    std::printf("models: %.0f kB each on average\n", bytes / 1024.0 / scans.size());

    // As the app does since phase 5 (docs/dictionary-engine.md): a line's words prepared when
    // it is walked (their first headword, PopupModel::Prepare), each headword's model kept, each
    // picture sent once, the stylesheets only the first time.
    PopupModel::ForgetPrepared();
    std::vector<double> walks;
    for (const QString& sentence : sentences) {
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < sentence.size(); ++i) {
            const QList<LookupResult> first = lookup.Lookup(sentence.mid(i, 16), 1);
            if (!first.isEmpty()) {
                PopupModel::PrepareText(first.first());
            }
        }
        walks.push_back(timer.nsecsElapsed() / 1e6);
    }
    std::sort(walks.begin(), walks.end());
    QSet<QString> sent;
    qint64 sent_bytes = 0;
    std::vector<double> hovers;
    // READER_LOOKUP_DUMP=FILE: the first hovers as the page gets them, for timing the page.
    QFile dump(qEnvironmentVariable("READER_LOOKUP_DUMP"));
    const bool dumping = !dump.fileName().isEmpty() && dump.open(QIODevice::WriteOnly | QIODevice::Truncate);
    int dumped = 0;
    for (const QString& scan : scans) {
        QElapsedTimer timer;
        timer.start();
        QByteArray text = "[";
        QJsonObject pictures;
        for (const LookupResult& result : lookup.Lookup(scan, max)) {
            if (text.size() > 1) {
                text += ',';
            }
            text += PopupModel::EntryText(result, {}, {}, sent, pictures);
        }
        text += ']';
        sent_bytes += text.size() + QJsonDocument(pictures).toJson(QJsonDocument::Compact).size();
        hovers.push_back(timer.nsecsElapsed() / 1e6);
        if (dumping && dumped < 40) {
            QJsonObject head{{QStringLiteral("pictures"), pictures}, {QStringLiteral("look"), QJsonObject()}};
            if (dumped == 0) {
                QJsonObject styles;
                const QHash<QString, QString> sheets = db.Styles();
                for (auto it = sheets.cbegin(); it != sheets.cend(); ++it) {
                    styles.insert(it.key(), it.value());
                }
                head.insert(QStringLiteral("styles"), styles);
            }
            QByteArray model = QJsonDocument(head).toJson(QJsonDocument::Compact);
            model.chop(1);
            dump.write("MODELS.push(" + model + ",\"entries\":" + text + "});\n");
            ++dumped;
        }
    }
    std::sort(hovers.begin(), hovers.end());
    std::printf("a line walked and prepared: median %.0f ms, longest %.0f ms\n", walks[walks.size() / 2], walks.back());
    std::printf("a hover as the app makes it now: p50 %.2f ms, p90 %.2f ms, p99 %.2f ms; %.0f kB sent each on average\n",
                hovers[hovers.size() / 2], hovers[static_cast<size_t>(0.9 * (hovers.size() - 1))],
                hovers[static_cast<size_t>(0.99 * (hovers.size() - 1))], sent_bytes / 1024.0 / scans.size());
    // Again, everything kept now: what is left of a hover.
    {
        double look = 0, model = 0, share = 0, json = 0;
        qint64 senses_bytes = 0;
        for (const QString& scan : scans) {
            QElapsedTimer timer;
            timer.start();
            const QList<LookupResult> results = lookup.Lookup(scan, max);
            look += timer.nsecsElapsed() / 1e6;
            timer.restart();
            QByteArray text = "[";
            QJsonObject pictures;
            for (const LookupResult& result : results) {
                text += PopupModel::EntryText(result, {}, {}, sent, pictures);
            }
            model += timer.nsecsElapsed() / 1e6;
            senses_bytes += text.size();
        }
        const double n = static_cast<double>(scans.size());
        std::printf("a hover with everything kept, on average: lookup %.2f, model %.2f, pictures %.2f, JSON %.2f ms; senses %.0f kB\n",
                    look / n, model / n, share / n, json / n, senses_bytes / 1024.0 / n);
    }
}
