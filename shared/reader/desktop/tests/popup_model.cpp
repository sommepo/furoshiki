// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <QJsonArray>
#include "reader/desktop/glossary.h"
#include "reader/desktop/names.h"
#include "reader/desktop/popup_model.h"
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
