// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The dictionary engine (docs/dictionary-engine.md) against the SQLite database it is to
// replace, with real dictionaries: every row of every dictionary, then the same lookups.
//   READER_COMPARE_SQLITE=library.db READER_COMPARE_ENGINE=folder reader_desktop_tests "[.engine-compare]"
// The ordinary dictionary tests run on the engine too: READER_DICT_BACKEND=engine.

#include <cstdio>
#include <catch2/catch_test_macros.hpp>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include "dict/library.h"
#include "reader/desktop/dictionary.h"
#include "reader/desktop/sqlite_db.h"

using namespace ReaderApp;

namespace {

struct Differences {
    long count = 0;
    void Note(const std::string& what) {
        if (++count <= 30) {
            std::printf("DIFFERENT: %s\n", what.c_str());
        }
    }
};

std::string S(const QString& text) {
    return text.toStdString();
}

} // namespace

TEST_CASE("Dictionary engine: every row and every lookup as SQLite has them", "[.engine-compare]") {
    const QString sqlite_path = qEnvironmentVariable("READER_COMPARE_SQLITE");
    const QString engine_path = qEnvironmentVariable("READER_COMPARE_ENGINE");
    if (sqlite_path.isEmpty() || engine_path.isEmpty()) {
        SKIP("READER_COMPARE_SQLITE and READER_COMPARE_ENGINE name no libraries");
    }
    Differences differences;
    long terms = 0;
    long metas = 0;
    long pictures = 0;
    {
        SqliteDb db;
        db.Open(sqlite_path);
        Dict::Library library(engine_path.toStdString());
        std::map<std::string, std::int64_t> engine_ids;
        for (const Dict::DictionaryInfo& info : library.Dictionaries()) {
            engine_ids[info.title] = info.id;
        }
        SqliteDb::Statement dictionaries(db, "SELECT id, title, styles, scope FROM dictionaries ORDER BY priority, id");
        while (dictionaries.Step()) {
            const qint64 id = dictionaries.Int(0);
            const std::string title = S(dictionaries.Text(1));
            const auto found = engine_ids.find(title);
            if (found == engine_ids.end()) {
                differences.Note("no dictionary " + title);
                continue;
            }
            const std::int64_t engine_id = found->second;
            for (const Dict::DictionaryInfo& info : library.Dictionaries()) {
                if (info.id == engine_id) {
                    if (info.styles != S(dictionaries.Text(2))) {
                        differences.Note(title + ": styles");
                    }
                    if (info.scope != S(dictionaries.Text(3))) {
                        differences.Note(title + ": scope");
                    }
                }
            }
            std::uint32_t at = 0;
            SqliteDb::Statement rows(db, "SELECT expression, reading, def_tags, rules, score, glossary, sequence, "
                                         "term_tags FROM terms WHERE dict = ? ORDER BY id");
            rows.Bind(1, id);
            while (rows.Step()) {
                const Dict::Term term = library.TermAt(engine_id, at);
                const std::string where = title + " term " + std::to_string(at);
                if (term.expression != S(rows.Text(0)) || term.reading != S(rows.Text(1)) ||
                    term.definition_tags != S(rows.Text(2)) || term.rules != S(rows.Text(3)) ||
                    term.score != rows.Int(4) || term.sequence != rows.Int(6) || term.term_tags != S(rows.Text(7))) {
                    differences.Note(where + ": fields (" + term.expression + ")");
                } else if (library.Definition(engine_id, at) != S(DictionaryDatabase::Unpack(rows.Blob(5)))) {
                    differences.Note(where + ": definition (" + term.expression + ")");
                }
                ++at;
                ++terms;
            }
            if (at != library.TermCount(engine_id)) {
                differences.Note(title + ": " + std::to_string(at) + " terms in SQLite, " +
                                 std::to_string(library.TermCount(engine_id)) + " in the engine");
            }
            std::uint32_t item = 0;
            SqliteDb::Statement meta_rows(db, "SELECT expression, mode, data FROM term_meta WHERE dict = ? ORDER BY id");
            meta_rows.Bind(1, id);
            while (meta_rows.Step()) {
                const Dict::Meta meta = library.MetaAt(engine_id, item);
                if (meta.expression != S(meta_rows.Text(0)) || meta.mode != S(meta_rows.Text(1)) ||
                    meta.data != S(meta_rows.Text(2))) {
                    differences.Note(title + " meta " + std::to_string(item));
                }
                ++item;
                ++metas;
            }
            if (item != library.MetaCount(engine_id)) {
                differences.Note(title + ": meta counts");
            }
            std::size_t media = 0;
            SqliteDb::Statement media_rows(db, "SELECT path, data FROM media WHERE dict = ? ORDER BY path");
            media_rows.Bind(1, id);
            while (media_rows.Step()) {
                const std::string path = S(media_rows.Text(0));
                const std::optional<std::string> bytes = library.Media(engine_id, path);
                const QByteArray expected = media_rows.Blob(1);
                if (!bytes || *bytes != std::string(expected.constData(), static_cast<std::size_t>(expected.size()))) {
                    differences.Note(title + " picture " + path);
                }
                ++media;
                ++pictures;
            }
            if (media != library.MediaPaths(engine_id).size()) {
                differences.Note(title + ": picture counts");
            }
        }
    }
    std::printf("rows: %ld terms, %ld metas, %ld pictures checked\n", terms, metas, pictures);

    // The same lookups on both: at every character of the text given (or a few sentences).
    DictionaryDatabase sqlite(sqlite_path);
    DictionaryDatabase engine(QStringLiteral("engine:") + engine_path);
    DictionaryLookup on_sqlite(sqlite);
    DictionaryLookup on_engine(engine);
    QString text = QStringLiteral("吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"
                                  "国境の長いトンネルを抜けると雪国であった。食べたかったけど、もう売り切れてしまった。");
    if (const QString file = qEnvironmentVariable("READER_COMPARE_TEXT"); !file.isEmpty()) {
        QFile in(file);
        if (in.open(QIODevice::ReadOnly)) {
            text = QString::fromUtf8(in.readAll());
        }
    }
    long lookups = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QString scan = text.mid(i, 16);
        const QList<LookupResult> a = on_sqlite.Lookup(scan, 24);
        const QList<LookupResult> b = on_engine.Lookup(scan, 24);
        ++lookups;
        const std::string where = "lookup " + S(scan);
        if (a.size() != b.size()) {
            differences.Note(where + ": " + std::to_string(a.size()) + " results against " + std::to_string(b.size()));
            continue;
        }
        for (qsizetype r = 0; r < a.size(); ++r) {
            const LookupResult& x = a[r];
            const LookupResult& y = b[r];
            if (x.expression != y.expression || x.reading != y.reading || x.matched_text != y.matched_text ||
                x.reasons != y.reasons || x.entries.size() != y.entries.size() ||
                x.frequencies.size() != y.frequencies.size() || x.pitches.size() != y.pitches.size()) {
                differences.Note(where + ": result " + std::to_string(r) + " (" + S(x.expression) + " / " +
                                 S(y.expression) + ")");
                break;
            }
            for (qsizetype e = 0; e < x.entries.size(); ++e) {
                const TermEntry& p = x.entries[e];
                const TermEntry& q = y.entries[e];
                if (p.dictionary != q.dictionary || p.glossary_json != q.glossary_json ||
                    p.definition_tags != q.definition_tags || p.score != q.score || p.media != q.media) {
                    differences.Note(where + ": entry " + std::to_string(e) + " of " + S(x.expression));
                    break;
                }
            }
            for (qsizetype f = 0; f < x.frequencies.size(); ++f) {
                if (x.frequencies[f].dictionary != y.frequencies[f].dictionary ||
                    x.frequencies[f].display != y.frequencies[f].display) {
                    differences.Note(where + ": frequency " + std::to_string(f) + " of " + S(x.expression));
                    break;
                }
            }
        }
    }
    std::printf("lookups: %ld compared\n", lookups);
    std::printf("differences: %ld\n", differences.count);
    CHECK(differences.count == 0);
}

// Converting a real SQLite file into the engine's library, timed (the file is not changed).
//   READER_CONVERT_DB=reader_dict.db READER_CONVERT_LIBRARY=folder reader_desktop_tests "[.convert]"
TEST_CASE("Dictionary engine: converting a SQLite file", "[.convert]") {
    const QString old = qEnvironmentVariable("READER_CONVERT_DB");
    const QString library = qEnvironmentVariable("READER_CONVERT_LIBRARY");
    if (old.isEmpty() || library.isEmpty()) {
        SKIP("READER_CONVERT_DB and READER_CONVERT_LIBRARY name nothing");
    }
    QElapsedTimer clock;
    clock.start();
    DictionaryDatabase converted(old, library);
    while (converted.Converting()) {
        QThread::msleep(100);
    }
    std::printf("converted %lld dictionaries in %.1f s\n", static_cast<long long>(converted.Dictionaries().size()),
                clock.elapsed() / 1000.0);
}
