// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdio>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QResource>
#include <QTemporaryDir>
#include <QThread>
#include <quazip.h>
#include <quazipfile.h>
#include "reader/desktop/dictionary.h"
#ifdef READER_DICT_ENGINE
#include "dict/library.h"
#endif
#include "reader/desktop/glossary.h"
#include "reader/desktop/names.h"
#include "reader/desktop/popup_model.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;

namespace {

using Files = QList<std::pair<QString, QString>>;

/// Dictionary import and lookup against small archives written for the test (no real data).
/// SQLite in memory, or the dictionary engine's library in a folder of the test's when
/// READER_DICT_BACKEND=engine (docs/dictionary-engine.md: the same tests on both).
QString DatabasePath(const QTemporaryDir& folder) {
    if (qEnvironmentVariable("READER_DICT_BACKEND") == QStringLiteral("engine")) {
        return QStringLiteral("engine:") + folder.filePath(QStringLiteral("library"));
    }
    return QStringLiteral(":memory:");
}

bool OnEngine() {
    return qEnvironmentVariable("READER_DICT_BACKEND") == QStringLiteral("engine");
}

struct Fixture {
    QTemporaryDir folder;
    DictionaryDatabase db{DatabasePath(folder)};
    DictionaryImporter importer{db};
    DictionaryLookup lookup{db};

    QString Archive(const QString& name, const Files& files) {
        const QString path = folder.filePath(name);
        QFile::remove(path);
        QuaZip zip(path);
        REQUIRE(zip.open(QuaZip::mdCreate));
        for (const auto& [entry, content] : files) {
            QuaZipFile file(&zip);
            REQUIRE(file.open(QIODevice::WriteOnly, QuaZipNewInfo(entry)));
            file.write(content.toUtf8());
            file.close();
        }
        zip.close();
        return path;
    }

    QString Structured() const {
        return Q(uR"({"type":"structured-content","content":[
        {"tag":"div","style":{"fontWeight":"bold"},"data":{"content":"gloss"},"content":"food"},
        {"tag":"ul","content":[{"tag":"li","content":"item one"},
          {"tag":"li","content":["item ",{"tag":"ruby","content":["二",{"tag":"rt","content":"に"}]}]}]},
        {"tag":"img","path":"x.png"}]})");
    }

    QString Terms() const {
        return Q(uR"json([
        ["食べる","たべる","v1","v1",100,["to eat","to live on"],1,""],
        ["読む","よむ","v5","v5",90,["to read"],2,""],
        ["高い","たかい","adj-i","adj-i",80,["high","expensive"],3,""],
        ["これ","","pn","",70,["this"],4,""],
        ["食べ物","たべもの","n","",60,[)json") +
               Structured() +
               Q(uR"json(],5,""],
        ["名","な","n","",50,["name"],6,""],
        ["名乗る","なのる","v5","v5",50,["to give one's name"],7,""],
        ["マリア","","n","",1,["Maria <the name>"],8,""],
        ["飼う","かう","v5","v5",40,["to keep (a pet)"],9,""],
        ["買う","かう","v5","v5",95,["to buy"],10,""]
    ])json");
    }

    QString Main() {
        return Archive(
            QStringLiteral("main.zip"),
            {
                // Deliberately not first in the archive: the importer must not rely on entry
                // order.
                {QStringLiteral("term_bank_1.json"), Terms()},
                {QStringLiteral("index.json"),
                 Q(uR"({"title":"Test Dict","revision":"r1","format":3,"author":"tests"})")},
                {QStringLiteral("tag_bank_1.json"), Q(uR"([["n","partOfSpeech",0,"noun",0]])")},
            });
    }

    QString FrequencyArchive() {
        return Archive(
            QStringLiteral("freq.zip"),
            {{QStringLiteral("index.json"),
              Q(uR"({"title":"Freq","revision":"1","format":3,"frequencyMode":"rank-based"})")},
             {QStringLiteral("term_meta_bank_1.json"), Q(uR"([
                ["食べる","freq",1500],
                ["読む","freq",{"reading":"よむ","frequency":{"value":800,"displayValue":"800㋕"}}],
                ["読む","freq",{"reading":"どく","frequency":5}],
                ["読む","pitch",{"reading":"よむ","pitches":[{"position":1},{"position":0}]}],
                ["買う","freq","rank 200"]
            ])")}});
    }

    /// A names dictionary for two games (tools/titles.py makes them: docs/titles.md).
    QString Names() {
        return Archive(QStringLiteral("names.zip"),
                       {{QStringLiteral("index.json"),
                         Q(uR"({"title":"名前 · Test","revision":"t-1","format":3,
                               "furoshikiScope":["test-001","TEST-002"],"furoshikiTitle":"test"})")},
                        {QStringLiteral("term_bank_1.json"),
                         Q(uR"([["マリアさん","","名前","",25,["マリア"],1,""],
                ["マリア","","名前","",27,["マリア","Also: 姫"],1,""],
                ["名","めい","名前","",20,["名（めい）"],2,""]])")}});
    }

    /// A monolingual-style dictionary: no word classes at all.
    QString Mono() {
        return Archive(QStringLiteral("mono.zip"),
                       {{QStringLiteral("index.json"),
                         Q(uR"({"title":"Mono","revision":"1","format":3})")},
                        {QStringLiteral("term_bank_1.json"),
                         Q(uR"([["食べる","たべる","","",0,["食物を口に入れる。"],0,""],
                ["た","","","",0,["助動詞。"],0,""]])")}});
    }
};

QStringList Expressions(const QList<LookupResult>& results) {
    QStringList out;
    for (const LookupResult& result : results) {
        out.append(result.expression);
    }
    return out;
}

QStringList DictionariesOf(const LookupResult& result) {
    QStringList out;
    for (const TermEntry& entry : result.entries) {
        out.append(entry.dictionary);
    }
    return out;
}

QStringList Titles(DictionaryDatabase& db) {
    QStringList out;
    for (const DictionaryInfo& info : db.Dictionaries()) {
        out.append(info.title);
    }
    return out;
}

} // namespace

TEST_CASE("Dictionary: terms are imported and progress reported", "[reader][dictionary]") {
    Fixture f;
    QList<std::pair<int, int>> steps;
    const auto result = f.importer.Import(
        f.Main(), [&steps](int done, int total, const QString&) { steps.append({done, total}); });
    REQUIRE(result.ok);
    REQUIRE(result.title == QStringLiteral("Test Dict"));
    REQUIRE(result.terms == 10);
    REQUIRE(result.metas == 0);
    REQUIRE(steps == QList<std::pair<int, int>>{{0, 1}, {1, 1}});
    const QList<DictionaryInfo> all = f.db.Dictionaries();
    REQUIRE(all.size() == 1);
    REQUIRE(all.first().title == QStringLiteral("Test Dict"));
    REQUIRE(all.first().revision == QStringLiteral("r1"));
    REQUIRE(all.first().term_count == 10);
    REQUIRE(all.first().enabled);
}

TEST_CASE("Dictionary: an app's own dictionaries are taken into the shared file once", "[reader][dictionary]") {
    Fixture f;
    // The app's own file from before the apps shared one: two dictionaries, one switched off.
    const QString own = f.folder.filePath(QStringLiteral("own.db"));
    {
        DictionaryDatabase app{own};
        DictionaryImporter importer{app};
        REQUIRE(importer.Import(f.Main()).ok);
        REQUIRE(importer.Import(f.FrequencyArchive()).ok);
        app.SetEnabled(app.Dictionaries().last().id, false);
    }
    // The shared file has the first already, imported in another app.
    const QString shared = f.folder.filePath(QStringLiteral("shared.db"));
    DictionaryDatabase db{shared};
    DictionaryImporter importer{db};
    REQUIRE(importer.Import(f.Main()).ok);
    REQUIRE(db.TakeFrom(own) == 1);
    const QList<DictionaryInfo> all = db.Dictionaries();
    REQUIRE(all.size() == 2);
    REQUIRE(all.at(0).title == QStringLiteral("Test Dict"));
    REQUIRE_FALSE(all.at(1).enabled);
    REQUIRE(all.at(1).meta_count > 0);
    // What came is there to look up once switched on: its rows came along.
    REQUIRE(db.FindMeta({Q(u"食べる")}).isEmpty());
    db.SetEnabled(all.at(1).id, true);
    REQUIRE_FALSE(db.FindMeta({Q(u"食べる")}).isEmpty());
    // A second time nothing comes, and a file that is not there brings nothing.
    REQUIRE(db.TakeFrom(own) == 0);
    REQUIRE(db.TakeFrom(f.folder.filePath(QStringLiteral("none.db"))) == 0);
    REQUIRE(db.Dictionaries().size() == 2);
}

TEST_CASE("Dictionary: duplicates and broken archives are refused", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    const auto again = f.importer.Import(f.Main());
    REQUIRE_FALSE(again.ok);
    REQUIRE(again.error.contains(QStringLiteral("already installed")));
    REQUIRE_FALSE(f.importer
                      .Import(f.Archive(QStringLiteral("empty.zip"),
                                        {{QStringLiteral("readme.txt"), QStringLiteral("hello")}}))
                      .ok);
    REQUIRE_FALSE(f.importer
                      .Import(f.Archive(QStringLiteral("noterms.zip"),
                                        {{QStringLiteral("index.json"),
                                          Q(uR"({"title":"X","format":3})")}}))
                      .ok);
    // A bank that breaks half way leaves nothing behind.
    REQUIRE_FALSE(
        f.importer
            .Import(f.Archive(QStringLiteral("broken.zip"),
                              {{QStringLiteral("index.json"), Q(uR"({"title":"Broken","format":3})")},
                               {QStringLiteral("term_bank_1.json"),
                                Q(uR"([["良い","よい","","adj-i",1,["good"],1,""],["oops)")}}))
            .ok);
    REQUIRE_FALSE(f.importer.Import(f.folder.filePath(QStringLiteral("missing.zip"))).ok);
    REQUIRE(Titles(f.db) == QStringList{QStringLiteral("Test Dict")});
    REQUIRE(f.lookup.Lookup(Q(u"良い")).isEmpty());
}

TEST_CASE("Dictionary: a cancelled import leaves the database unchanged", "[reader][dictionary]") {
    Fixture f;
    const auto result = f.importer.Import(f.Main(), {}, [] { return false; });
    REQUIRE_FALSE(result.ok);
    REQUIRE(f.db.Dictionaries().isEmpty());
}

TEST_CASE("Dictionary: inflected words are found, with why", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    const auto past = f.lookup.Lookup(Q(u"食べた。それから"));
    REQUIRE_FALSE(past.isEmpty());
    REQUIRE(past.at(0).expression == Q(u"食べる"));
    REQUIRE(past.at(0).reading == Q(u"たべる"));
    REQUIRE(past.at(0).matched_text == Q(u"食べた"));
    REQUIRE(past.at(0).reasons == QStringList{QStringLiteral("past")});
    const auto progressive = f.lookup.Lookup(Q(u"読んでいる"));
    REQUIRE(progressive.at(0).expression == Q(u"読む"));
    REQUIRE(progressive.at(0).matched_text == Q(u"読んでいる"));
    REQUIRE(progressive.at(0).reasons ==
            QStringList{QStringLiteral("progressive or perfect"), QStringLiteral("-te")});
    const auto imperative = f.lookup.Lookup(Q(u"名乗れ"));
    REQUIRE(Expressions(imperative) == QStringList{Q(u"名乗る"), Q(u"名")});
    REQUIRE(imperative.at(0).matched_text == Q(u"名乗れ"));
    REQUIRE(imperative.at(1).matched_text == Q(u"名"));
}

TEST_CASE("Dictionary: the longest match comes first", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    const auto results = f.lookup.Lookup(Q(u"食べ物を買う"));
    REQUIRE(results.at(0).expression == Q(u"食べ物"));
    // The shorter reading of the same text is still offered after it.
    REQUIRE(std::any_of(results.begin(), results.end(), [](const LookupResult& r) {
        return r.expression == Q(u"食べる") && r.matched_text == Q(u"食べ");
    }));
}

TEST_CASE("Dictionary: kana matches by reading, katakana as written", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    const auto by_reading = f.lookup.Lookup(Q(u"たかくない"));
    REQUIRE(by_reading.at(0).expression == Q(u"高い"));
    REQUIRE(by_reading.at(0).matched_text == Q(u"たかくない"));
    // Two words share the reading かう: both are offered, the higher score first.
    REQUIRE(Expressions(f.lookup.Lookup(Q(u"かった"))) == QStringList{Q(u"買う"), Q(u"飼う")});
    REQUIRE(f.lookup.Lookup(Q(u"マリアだ")).at(0).expression == Q(u"マリア"));
    REQUIRE(f.lookup.Lookup(Q(u"マリアだ")).at(0).DisplayReading().isEmpty());
    REQUIRE(f.lookup.Lookup(Q(u"これは")).at(0).expression == Q(u"これ"));
}

TEST_CASE("Dictionary: the scan stops at punctuation", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    REQUIRE(f.lookup.Lookup(QString()).isEmpty());
    REQUIRE(f.lookup.Lookup(Q(u"、食べる")).isEmpty());
    REQUIRE(f.lookup.Lookup(Q(u"…")).isEmpty());
    // 食べる。読む: the scan must not run across the full stop.
    REQUIRE(f.lookup.Lookup(Q(u"食べる。読む")).at(0).matched_text == Q(u"食べる"));
}

TEST_CASE("Dictionary: word classes rule out false deinflections", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    // 名た would be a "past" of an ichidan 名る; 名 is a noun, so only the plain 名 matches.
    const auto results = f.lookup.Lookup(Q(u"名た"));
    REQUIRE(Expressions(results) == QStringList{Q(u"名")});
    REQUIRE(results.at(0).matched_text == Q(u"名"));
    REQUIRE(results.at(0).reasons.isEmpty());
}

TEST_CASE("Dictionary: one without word classes joins confirmed headwords",
          "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    REQUIRE(f.importer.Import(f.Mono()).ok);
    const auto results = f.lookup.Lookup(Q(u"食べた"));
    REQUIRE(results.at(0).expression == Q(u"食べる"));
    REQUIRE(DictionariesOf(results.at(0)) ==
            QStringList{QStringLiteral("Test Dict"), QStringLiteral("Mono")});
    // On its own, the class-less dictionary cannot confirm an inflection...
    for (const DictionaryInfo& info : f.db.Dictionaries()) {
        if (info.title == QStringLiteral("Test Dict")) {
            f.db.SetEnabled(info.id, false);
        }
    }
    const auto alone = f.lookup.Lookup(Q(u"食べた"));
    REQUIRE(std::none_of(alone.begin(), alone.end(),
                         [](const LookupResult& r) { return r.expression == Q(u"食べる"); }));
    // ...but still matches what is written.
    REQUIRE(f.lookup.Lookup(Q(u"食べる")).at(0).expression == Q(u"食べる"));
}

TEST_CASE("Dictionary: frequencies and pitch are those of the right reading",
          "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    const auto imported = f.importer.Import(f.FrequencyArchive());
    REQUIRE(imported.ok);
    REQUIRE(imported.metas == 5);
    const LookupResult yomu = f.lookup.Lookup(Q(u"読む")).at(0);
    REQUIRE(yomu.frequencies.size() == 1);
    REQUIRE(yomu.frequencies.at(0).dictionary == QStringLiteral("Freq"));
    REQUIRE(yomu.frequencies.at(0).display == Q(u"800㋕"));
    REQUIRE(yomu.frequencies.at(0).value == 800.0);
    REQUIRE(yomu.frequencies.at(0).rank_based);
    REQUIRE(yomu.pitches.size() == 1);
    REQUIRE(yomu.pitches.at(0).positions == QList<int>{1, 0});
    const LookupResult taberu = f.lookup.Lookup(Q(u"食べる")).at(0);
    REQUIRE(taberu.frequencies.size() == 1);
    REQUIRE(taberu.frequencies.at(0).display == QStringLiteral("1500"));
    REQUIRE(taberu.frequencies.at(0).value == 1500.0);
    REQUIRE(taberu.pitches.isEmpty());
    const LookupResult kau = f.lookup.Lookup(Q(u"買う")).at(0);
    REQUIRE(kau.frequencies.size() == 1);
    REQUIRE(kau.frequencies.at(0).display == QStringLiteral("rank 200"));
    REQUIRE_FALSE(kau.frequencies.at(0).value.has_value());
    REQUIRE(DictionaryLookup::HarmonicRank(taberu.frequencies) == 1500);
    REQUIRE_FALSE(DictionaryLookup::HarmonicRank(kau.frequencies).has_value());
    REQUIRE(DictionaryLookup::HarmonicRank(
                {Frequency{QStringLiteral("a"), QStringLiteral("800"), 800.0, true},
                 Frequency{QStringLiteral("b"), QStringLiteral("1500"), 1500.0, true}}) == 1043);
}

TEST_CASE("Dictionary: order, enabling and deletion are respected", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    REQUIRE(f.importer.Import(f.Mono()).ok);
    const QList<DictionaryInfo> both = f.db.Dictionaries();
    const DictionaryInfo main = both.at(0);
    const DictionaryInfo mono = both.at(1);
    f.db.SetOrder({mono.id, main.id});
    REQUIRE(Titles(f.db) == QStringList{QStringLiteral("Mono"), QStringLiteral("Test Dict")});
    REQUIRE(DictionariesOf(f.lookup.Lookup(Q(u"食べる")).at(0)) ==
            QStringList{QStringLiteral("Mono"), QStringLiteral("Test Dict")});
    f.db.SetEnabled(mono.id, false);
    REQUIRE(DictionariesOf(f.lookup.Lookup(Q(u"食べる")).at(0)) ==
            QStringList{QStringLiteral("Test Dict")});
    REQUIRE(f.db.HasEnabledDictionaries());
    f.db.Delete(main.id);
    REQUIRE(f.lookup.Lookup(Q(u"食べる")).isEmpty());
    REQUIRE_FALSE(f.db.HasEnabledDictionaries());
    REQUIRE(Titles(f.db) == QStringList{QStringLiteral("Mono")});
}

TEST_CASE("Dictionary: a names dictionary is used only for its games, and first",
          "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    REQUIRE(f.importer.Import(f.Names()).ok);
    const QList<DictionaryInfo> all = f.db.Dictionaries();
    REQUIRE(all.at(0).scope.isEmpty());
    REQUIRE(all.at(1).scope == QStringList{QStringLiteral("TEST-001"), QStringLiteral("TEST-002")});

    // No game, or another game: as if the names were not there.
    for (const QString& game : {QString(), QStringLiteral("OTHER-1"), QStringLiteral("TEST-00")}) {
        const QList<LookupResult> none = f.lookup.Lookup(Q(u"マリアさん"), 24, game);
        REQUIRE(Expressions(none) == QStringList{Q(u"マリア")});
        REQUIRE(DictionariesOf(none.at(0)) == QStringList{QStringLiteral("Test Dict")});
        REQUIRE_FALSE(none.at(0).entries.at(0).scoped);
    }

    // Its game (in any case): the names, before the dictionary above them in the order.
    const QList<LookupResult> named = f.lookup.Lookup(Q(u"マリアさん"), 24, QStringLiteral("test-001"));
    REQUIRE(Expressions(named) == QStringList{Q(u"マリアさん"), Q(u"マリア")});
    REQUIRE(named.at(0).entries.at(0).scoped);
    REQUIRE(DictionariesOf(named.at(1)) == QStringList{Q(u"名前 · Test"), QStringLiteral("Test Dict")});

    // A name of the same length as another word comes first; a longer word still wins.
    const QList<LookupResult> same = f.lookup.Lookup(Q(u"名は"), 24, QStringLiteral("TEST-002"));
    REQUIRE(same.size() == 2);
    REQUIRE(same.at(0).reading == Q(u"めい"));
    REQUIRE(same.at(1).reading == Q(u"な"));
    REQUIRE(Expressions(f.lookup.Lookup(Q(u"名乗る"), 24, QStringLiteral("TEST-002"))).first() ==
            Q(u"名乗る"));

    REQUIRE(DictionaryDatabase::ScopeText({Q(u" a-1 "), Q(u"A-1"), Q(u"b")}) == Q(u"|A-1|B|"));
    REQUIRE(DictionaryDatabase::ScopeText({}).isEmpty());
}

TEST_CASE("Names: a game's names dictionary is found, installed once and refreshed",
          "[reader][dictionary][names]") {
    Fixture f;
    const auto pack_zip = [&f](const QString& folder, const QString& revision) {
        QDir().mkpath(f.folder.filePath(folder));
        Files files{
            {QStringLiteral("index.json"),
             Q(uR"({"title":"名前 · Test","revision":")") + revision +
                 Q(uR"(","format":3,"furoshikiScope":["TEST-001"],"furoshikiTitle":"test"})")},
            {QStringLiteral("term_bank_1.json"), Q(uR"([["マリア","","名前","",27,["マリア"],1,""]])")},
        };
        return f.Archive(folder + QStringLiteral("/names-test.zip"), files);
    };
    pack_zip(QStringLiteral("user"), QStringLiteral("r2"));
    pack_zip(QStringLiteral("bundled"), QStringLiteral("r1"));
    // A dictionary that is not a names dictionary is not one, wherever it is.
    QFile::copy(f.Main(), f.folder.filePath(QStringLiteral("bundled/names-other.zip")));

    // The user's folder before what the program comes with: one pack a title.
    const QList<Names::Pack> both = Names::Find(
        {f.folder.filePath(QStringLiteral("user")), f.folder.filePath(QStringLiteral("bundled"))});
    REQUIRE(both.size() == 1);
    REQUIRE(both.first().revision == QStringLiteral("r2"));
    REQUIRE(both.first().scope == QStringList{QStringLiteral("TEST-001")});
    REQUIRE_FALSE(Names::For(QStringLiteral("OTHER"), both).has_value());

    const QList<Names::Pack> bundled = Names::Find({f.folder.filePath(QStringLiteral("bundled"))});
    const auto old = Names::For(QStringLiteral("test-001"), bundled);
    REQUIRE(old.has_value());
    REQUIRE(Names::Ensure(f.db, *old));
    REQUIRE_FALSE(Names::Ensure(f.db, *old)); // there at its revision: nothing to do
    REQUIRE(f.lookup.Lookup(Q(u"マリア"), 24, QStringLiteral("TEST-001")).at(0).entries.at(0).scoped);

    // Switched off, then a newer revision: replaced, and still off.
    f.db.SetEnabled(f.db.Dictionaries().first().id, false);
    REQUIRE(Names::Ensure(f.db, both.first()));
    const QList<DictionaryInfo> after = f.db.Dictionaries();
    REQUIRE(after.size() == 1);
    REQUIRE(after.first().revision == QStringLiteral("r2"));
    REQUIRE_FALSE(after.first().enabled);
}

TEST_CASE("Dictionary: glossaries are rendered for cards and for the popup",
          "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    const QString plain = f.lookup.Lookup(Q(u"食べる")).at(0).entries.at(0).glossary_json;
    REQUIRE(Glossary::ToPlainText(plain) == QStringLiteral("to eat; to live on"));
    REQUIRE(Glossary::ToHtml(plain, Glossary::Mode::Full) ==
            QStringLiteral("<ul><li>to eat</li><li>to live on</li></ul>"));
    const QString structured = f.lookup.Lookup(Q(u"食べ物")).at(0).entries.at(0).glossary_json;
    const QString full = Glossary::ToHtml(structured, Glossary::Mode::Full);
    REQUIRE(full.contains(QStringLiteral("data-sc-content=\"gloss\"")));
    REQUIRE(full.contains(QStringLiteral("font-weight: bold")));
    REQUIRE(full.contains(Q(u"<ruby>二<rt>に</rt></ruby>")));
    REQUIRE_FALSE(full.contains(QStringLiteral("img")));
    const QString simple = Glossary::ToHtml(structured, Glossary::Mode::Simple);
    REQUIRE(simple.contains(QStringLiteral("<b>food</b>")));
    REQUIRE(simple.contains(Q(u"• item one")));
    REQUIRE_FALSE(simple.contains(Q(u"に")));
    // What is escaped in a plain gloss stays escaped as HTML and comes back as text.
    const QString maria = f.lookup.Lookup(Q(u"マリア")).at(0).entries.at(0).glossary_json;
    REQUIRE(Glossary::ToHtml(maria, Glossary::Mode::Full) ==
            QStringLiteral("Maria &lt;the name&gt;"));
    REQUIRE(Glossary::ToPlainText(maria) == QStringLiteral("Maria <the name>"));
}

TEST_CASE("Dictionary: glossaries survive packing", "[reader][dictionary]") {
    const QString small = QStringLiteral("[\"short\"]");
    REQUIRE(DictionaryDatabase::Unpack(DictionaryDatabase::Pack(small)) == small);
    QString large = QStringLiteral("[");
    for (int i = 0; i < 400; ++i) {
        large += QStringLiteral("\"definition number %1 with some text\",").arg(i);
    }
    large += Q(u"\"終わり\"]");
    const QByteArray packed = DictionaryDatabase::Pack(large);
    REQUIRE(packed.size() < large.toUtf8().size() / 2);
    REQUIRE(packed.at(0) == 1);
    REQUIRE(DictionaryDatabase::Unpack(packed) == large);
    REQUIRE(DictionaryDatabase::Unpack({}) == QStringLiteral("[]"));
}

TEST_CASE("Dictionary: a dictionary's styles.css and pictures come with it, and go with it", "[reader][dictionary]") {
    Fixture f;
    // A picture's bytes are stored as they are; a zip entry is written as text here, so a few
    // letters stand in for an image.
    const QString archive = f.Archive(
        QStringLiteral("styled.zip"),
        {{QStringLiteral("dict/index.json"), Q(uR"({"title":"Styled","revision":"1","format":3})")},
         {QStringLiteral("dict/styles.css"), QStringLiteral("li[data-sc-content=\"sense\"] { color: red; }")},
         {QStringLiteral("dict/img/a.png"), QStringLiteral("PNGDATA")},
         {QStringLiteral("dict/term_bank_1.json"),
          Q(uR"([["絵","え","n","",1,[{"type":"structured-content","content":[{"tag":"img","path":"img/a.png"},"picture"]}],1,""],
                 ["線","せん","n","",1,["line"],2,""]])")},
         {QStringLiteral("dict/term_meta_bank_1.json"),
          Q(uR"([["絵","pitch",{"reading":"え","pitches":[{"position":"HL"}]}],
                 ["線","pitch",{"reading":"せん","pitches":[{"position":"LHH"}]}]])")}});
    const DictionaryImporter::Result result = f.importer.Import(archive);
    REQUIRE(result.ok);
    REQUIRE(result.media == 1);
    REQUIRE(f.db.Styles().value(QStringLiteral("Styled")).contains(QStringLiteral("color: red")));
    const QList<LookupResult> found = f.lookup.Lookup(Q(u"絵"));
    REQUIRE(!found.isEmpty());
    const QString picture = found.first().entries.first().media.value(QStringLiteral("img/a.png"));
    REQUIRE(picture == QStringLiteral("data:image/png;base64,") + QString::fromLatin1(QByteArray("PNGDATA").toBase64()));
    // Yomitan's other form of a pitch: high and low by mora.
    REQUIRE(found.first().pitches.first().positions == QList<int>{1});
    REQUIRE(f.lookup.Lookup(Q(u"線")).first().pitches.first().positions == QList<int>{0});
    REQUIRE(f.lookup.Lookup(Q(u"線")).first().entries.first().media.isEmpty());
    // Switched off, its styles are not handed out; deleted, its pictures go too.
    const qint64 id = f.db.Dictionaries().first().id;
    f.db.SetEnabled(id, false);
    REQUIRE(f.db.Styles().isEmpty());
    f.db.Delete(id);
    if (OnEngine()) {
        // Its folder goes, pictures and all.
        const QStringList left = QDir(f.folder.filePath(QStringLiteral("library")))
                                     .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        REQUIRE(left.isEmpty());
        return;
    }
    SqliteDb::Statement left(f.db.Db(), "SELECT COUNT(*) FROM media");
    REQUIRE(left.Step());
    REQUIRE(left.Int(0) == 0);
}

// Not run by default: times importing real dictionaries into a file on disk, as the apps do.
//   READER_BENCH_DICTS="a.zip:b.zip" reader_desktop_tests "[.bench]"
TEST_CASE("Dictionary: many dictionaries switched and deleted at once", "[reader][dictionary]") {
    Fixture f;
    REQUIRE(f.importer.Import(f.Main()).ok);
    REQUIRE(f.importer.Import(f.FrequencyArchive()).ok);
    REQUIRE(f.importer.Import(f.Mono()).ok);
    QList<qint64> ids;
    for (const DictionaryInfo& info : f.db.Dictionaries()) {
        ids.append(info.id);
    }
    f.db.SetEnabled(ids.mid(0, 2), false);
    QList<bool> enabled;
    for (const DictionaryInfo& info : f.db.Dictionaries()) {
        enabled.append(info.enabled);
    }
    REQUIRE(enabled == QList<bool>{false, false, true});
    f.db.Delete(ids.mid(1, 2));
    REQUIRE(Titles(f.db) == QStringList{Q(u"Test Dict")});
    REQUIRE(f.lookup.Lookup(Q(u"食べる")).isEmpty()); // switched off
    f.db.SetEnabled(ids.mid(0, 1), true);
    REQUIRE_FALSE(f.lookup.Lookup(Q(u"食べる")).isEmpty());
}

TEST_CASE("Dictionary: the SQLite file's dictionaries are converted into the engine's library once",
          "[reader][dictionary]") {
#ifdef READER_DICT_ENGINE
    Fixture f;
    const QString old = f.folder.filePath(QStringLiteral("reader_dict.db"));
    const QString library = f.folder.filePath(QStringLiteral("dictionaries"));
    QStringList before;
    QList<bool> enabled;
    {
        DictionaryDatabase sqlite(old);
        DictionaryImporter importer(sqlite);
        REQUIRE(importer.Import(f.Main()).ok);
        REQUIRE(importer.Import(f.FrequencyArchive()).ok);
        REQUIRE(importer.Import(f.Mono()).ok);
        // The order and a switch, as the user left them.
        QList<qint64> ids;
        for (const DictionaryInfo& info : sqlite.Dictionaries()) {
            ids.prepend(info.id);
        }
        sqlite.SetOrder(ids);
        sqlite.SetEnabled(sqlite.Dictionaries().last().id, false);
        DictionaryLookup lookup(sqlite);
        for (const QString& word : {Q(u"食べた"), Q(u"読む"), Q(u"食べ物")}) {
            for (const LookupResult& result : lookup.Lookup(word)) {
                before.append(result.expression + u'|' + result.reading + u'|' + DictionariesOf(result).join(u',') + u'|' +
                              QString::number(result.frequencies.size()));
            }
        }
        for (const DictionaryInfo& info : sqlite.Dictionaries()) {
            enabled.append(info.enabled);
        }
    }
    QStringList titles_before;
    {
        DictionaryDatabase sqlite(old);
        titles_before = Titles(sqlite);
    }
    {
        DictionaryDatabase converted(old, library);
        for (int i = 0; i < 500 && converted.Converting(); ++i) {
            QThread::msleep(10);
        }
        REQUIRE_FALSE(converted.Converting());
        const DictionaryDatabase::Conversion progress = converted.ConversionProgress();
        REQUIRE(progress.done == 3);
        REQUIRE(progress.total == 3);
        REQUIRE(Titles(converted) == titles_before);
        QList<bool> now;
        for (const DictionaryInfo& info : converted.Dictionaries()) {
            now.append(info.enabled);
        }
        REQUIRE(now == enabled);
        DictionaryLookup lookup(converted);
        QStringList after;
        for (const QString& word : {Q(u"食べた"), Q(u"読む"), Q(u"食べ物")}) {
            for (const LookupResult& result : lookup.Lookup(word)) {
                after.append(result.expression + u'|' + result.reading + u'|' + DictionariesOf(result).join(u',') + u'|' +
                             QString::number(result.frequencies.size()));
            }
        }
        REQUIRE(after == before);
    }
    // Converted whole: the old file is gone with its data, and the next start is the library's.
    REQUIRE_FALSE(QFileInfo::exists(old));
    {
        DictionaryDatabase again(old, library);
        REQUIRE_FALSE(again.Converting());
        REQUIRE(Titles(again) == titles_before);
    }
    REQUIRE_FALSE(QFileInfo::exists(old));
#endif
}

TEST_CASE("Dictionary: import timing of real archives", "[.bench]") {
    const QStringList archives =
        qEnvironmentVariable("READER_BENCH_DICTS").split(u':', Qt::SkipEmptyParts);
    if (archives.isEmpty()) {
        SKIP("READER_BENCH_DICTS names no archives");
    }
    QTemporaryDir folder;
    // READER_BENCH_DB keeps the database where it says, to be looked at afterwards.
    const QString path = qEnvironmentVariableIsSet("READER_BENCH_DB")
                             ? qEnvironmentVariable("READER_BENCH_DB")
                             : folder.filePath(QStringLiteral("bench.db"));
    QFile::remove(path);
    DictionaryDatabase db{path};
    DictionaryImporter importer{db};
    for (const QString& archive : archives) {
        QElapsedTimer timer;
        timer.start();
        const DictionaryImporter::Result result = importer.Import(archive);
        INFO(result.error.toStdString());
        CHECK(result.ok);
        std::printf("%s\n", qPrintable(result.Summary()));
        std::printf("import %s: %lld terms, %lld metas, %lld media, %.2f s\n", qPrintable(result.title),
                    static_cast<long long>(result.terms), static_cast<long long>(result.metas),
                    static_cast<long long>(result.media), timer.elapsed() / 1000.0);
    }
#ifdef READER_DICT_ENGINE
    if (Dict::Library* engine = db.Engine()) {
        QElapsedTimer timer;
        timer.start();
        engine->Compact();
        std::printf("library index: %.2f s\n", timer.elapsed() / 1000.0);
    }
#endif
    // READER_BENCH_KANJI="亜食" prints what the popup would be given for each character.
    for (const uint code : qEnvironmentVariable("READER_BENCH_KANJI").toUcs4()) {
        const QString character = QString::fromUcs4(reinterpret_cast<const char32_t*>(&code), 1);
        std::printf("kanji %s: [%s]\n", qPrintable(character),
                    PopupModel::KanjiText(db.FindKanji(character), db.FindKanjiMeta(character)).constData());
    }
    std::printf("database: %.1f MB\n",
                QFileInfo(path).size() / 1048576.0);
}

TEST_CASE("Dictionary: kanji dictionaries give a character's readings, meanings and stats", "[reader][dictionary]") {
    Fixture f;
    const QString archive = f.Archive(
        QStringLiteral("kanji.zip"),
        {{QStringLiteral("index.json"), Q(uR"({"title":"Kanji Test","revision":"k1","format":3})")},
         {QStringLiteral("kanji_bank_1.json"),
          Q(uR"([["亜","ア","つ.ぐ","jouyou",["Asia","rank next"],{"freq":"1509","grade":"8","strokes":"7"}],
                 ["食","ショク ジキ","く.う た.べる","jouyou",["eat","food"],{"strokes":"9"}]])")},
         {QStringLiteral("kanji_meta_bank_1.json"), Q(uR"([["食","freq",321]])")}});
    const auto result = f.importer.Import(archive);
    if (!OnEngine()) {
        // The SQLite file has no kanji tables: a dictionary of only kanji has nothing for it.
        REQUIRE_FALSE(result.ok);
        REQUIRE(f.db.FindKanji(QStringLiteral("食")).isEmpty());
        return;
    }
    REQUIRE(result.ok);
    const QList<KanjiEntry> found = f.db.FindKanji(QStringLiteral("食"));
    REQUIRE(found.size() == 1);
    REQUIRE(found.first().dictionary == QStringLiteral("Kanji Test"));
    REQUIRE(found.first().onyomi == QStringList{QStringLiteral("ショク"), QStringLiteral("ジキ")});
    REQUIRE(found.first().kunyomi == QStringList{QStringLiteral("く.う"), QStringLiteral("た.べる")});
    REQUIRE(found.first().meanings == QStringList{QStringLiteral("eat"), QStringLiteral("food")});
    REQUIRE(found.first().stats.value(QStringLiteral("strokes")).toString() == QStringLiteral("9"));
    REQUIRE(f.db.FindKanji(QStringLiteral("猫")).isEmpty());
    const QList<TermMeta> frequencies = f.db.FindKanjiMeta(QStringLiteral("食"));
    REQUIRE(frequencies.size() == 1);

    const QByteArray text = PopupModel::KanjiText(found, frequencies);
    const QJsonObject entry = QJsonDocument::fromJson('[' + text + ']').array().first().toObject();
    REQUIRE(entry.value(QStringLiteral("expression")).toString() == QStringLiteral("食"));
    REQUIRE(entry.value(QStringLiteral("kanji")).toObject().value(QStringLiteral("character")).toString() ==
            QStringLiteral("食"));
    REQUIRE(entry.value(QStringLiteral("frequencies")).toArray().first().toObject().value(QStringLiteral("value")) ==
            QStringLiteral("321"));

    // Disabled, it is not asked.
    f.db.SetEnabled(found.first().dictionary_id, false);
    REQUIRE(f.db.FindKanji(QStringLiteral("食")).isEmpty());
}

// A names dictionary among a program's own files (":/reader/names/..."), as Furoshiki has them:
// imported from a copy, since the zip reader maps a file on disk.
//   READER_RESOURCE_RCC=names.rcc READER_RESOURCE_ZIP=:/names/names-x.zip reader_desktop_tests "[.resource]"
TEST_CASE("Dictionary: a zip among the program's files is imported", "[.resource]") {
    const QString rcc = qEnvironmentVariable("READER_RESOURCE_RCC");
    const QString zip = qEnvironmentVariable("READER_RESOURCE_ZIP");
    if (rcc.isEmpty() || zip.isEmpty()) {
        SKIP("READER_RESOURCE_RCC and READER_RESOURCE_ZIP name nothing");
    }
    REQUIRE(QResource::registerResource(rcc));
    Fixture f;
    const auto result = f.importer.Import(zip);
    INFO(result.error.toStdString());
    REQUIRE(result.ok);
    std::printf("%s\n", qPrintable(result.Summary()));
}

TEST_CASE("Dictionary engine: two processes share one library", "[reader][dictionary]") {
#ifdef READER_DICT_ENGINE
    // Android's app and its game process each open the library: what one changes, the other
    // sees at its next call. Two Library objects on one folder stand for them.
    Fixture f;
    const std::string folder = f.folder.filePath(QStringLiteral("shared-library")).toStdString();
    Dict::Library app(folder);
    Dict::Library game(folder);
    const Dict::ImportResult first = app.Import(f.Main().toStdString());
    REQUIRE(first.ok);
    REQUIRE(game.Dictionaries().size() == 1);
    REQUIRE_FALSE(game.FindTerms({"食べる"}).empty());

    // An import in each: different ids, both listed in both.
    const Dict::ImportResult second = game.Import(f.FrequencyArchive().toStdString());
    REQUIRE(second.ok);
    REQUIRE(second.id != first.id);
    REQUIRE(app.Dictionaries().size() == 2);
    app.Compact();
    REQUIRE(game.Uncovered() == 0);

    game.SetEnabled(first.id, false);
    REQUIRE(app.FindTerms({"食べる"}).empty());
    app.SetEnabled(first.id, true);
    app.Delete(second.id);
    REQUIRE(game.Dictionaries().size() == 1);
    REQUIRE_FALSE(game.FindTerms({"食べる"}).empty());

    // A third opening (a process started later) leaves the others' dictionaries alone.
    Dict::Library later(folder);
    REQUIRE(later.Dictionaries().size() == 1);
    REQUIRE_FALSE(app.FindTerms({"食べる"}).empty());
#else
    SKIP("the dictionary engine is not built");
#endif
}
