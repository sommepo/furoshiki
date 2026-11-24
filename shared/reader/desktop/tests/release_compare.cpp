// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Imports and lookups timed the same way on two versions of Furoshiki (a release's source and
// the newest), for the speed a release can claim. Only what every version since 0.3.3 has is
// used: DictionaryDatabase(path), DictionaryImporter::Import, DictionaryLookup::Lookup and the
// popup's model of each result.
//   READER_COMPARE_DICTS="a.zip|b.zip" READER_COMPARE_DB=library.db (or engine:FOLDER)
//   reader_desktop_tests "[.release-compare]"

#include <algorithm>
#include <cstdio>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/popup_model.h"
#ifdef READER_DICT_ENGINE
#include "dict/library.h"
#endif

using namespace ReaderApp;

TEST_CASE("Release compare: the same dictionaries imported and looked up", "[.release-compare]") {
    const QStringList archives = qEnvironmentVariable("READER_COMPARE_DICTS").split(u'|', Qt::SkipEmptyParts);
    const QString path = qEnvironmentVariable("READER_COMPARE_DB");
    if (archives.isEmpty() || path.isEmpty()) {
        SKIP("READER_COMPARE_DICTS and READER_COMPARE_DB name nothing");
    }
    if (!path.startsWith(QStringLiteral("engine:"))) {
        QFile::remove(path);
    }
    DictionaryDatabase db{path};
    {
        DictionaryImporter importer{db};
        QElapsedTimer all;
        all.start();
        int done = 0;
        for (const QString& archive : archives) {
            QElapsedTimer one;
            one.start();
            const DictionaryImporter::Result result = importer.Import(archive);
            std::printf("import %s: %s in %.2f s\n", qPrintable(result.title.isEmpty() ? archive : result.title),
                        result.ok ? "ok" : qPrintable(result.error), one.elapsed() / 1000.0);
            std::fflush(stdout);
            done += result.ok ? 1 : 0;
        }
#ifdef READER_DICT_ENGINE
        if (Dict::Library* engine = db.Engine()) {
            engine->Compact(); // as the app does once a batch is over
        }
#endif
        std::printf("imported %d of %lld in %.1f s\n", done, static_cast<long long>(archives.size()), all.elapsed() / 1000.0);
    }

    DictionaryLookup lookup{db};
    const QStringList sentences{
        QStringLiteral("吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"),
        QStringLiteral("何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。"),
        QStringLiteral("国境の長いトンネルを抜けると雪国であった。夜の底が白くなった。"),
        QStringLiteral("昨日は友達と駅前の喫茶店でコーヒーを飲みながら話していました。"),
        QStringLiteral("食べたかったけど、もう売り切れてしまったみたいで残念だった。"),
        QStringLiteral("彼女は振り返らずに、静かに部屋を出て行ってしまった。")};
    QStringList scans;
    for (const QString& sentence : sentences) {
        for (int i = 0; i < sentence.size(); ++i) {
            if (sentence[i] != u'、' && sentence[i] != u'。') {
                scans.append(sentence.mid(i, 16));
            }
        }
    }
    const auto report = [](const char* what, std::vector<double> times) {
        std::sort(times.begin(), times.end());
        const auto at = [&times](double p) { return times[static_cast<size_t>(p * (times.size() - 1))]; };
        std::printf("%s: %zu, median %.2f ms, 90th %.2f ms, 99th %.2f ms, slowest %.2f ms\n", what, times.size(), at(0.5),
                    at(0.9), at(0.99), times.back());
    };
    for (const QString& scan : scans) {
        (void)lookup.Lookup(scan, 24); // warm
    }
    std::vector<double> lookups;
    std::vector<double> hovers;
#ifdef READER_DICT_ENGINE
    QSet<QString> sent; // the page keeps the pictures it was sent
#endif
    for (int pass = 0; pass < 3; ++pass) {
        for (const QString& scan : scans) {
#ifdef READER_DICT_ENGINE
            // Nothing kept from the last hover: every popup made as 0.3.3 made it, from nothing.
            PopupModel::ForgetPrepared();
#endif
            QElapsedTimer timer;
            timer.start();
            const QList<LookupResult> results = lookup.Lookup(scan, 24);
            lookups.push_back(timer.nsecsElapsed() / 1e6);
            // The popup's model of them, as each version makes it for its page.
            QByteArray text = "[";
            for (const LookupResult& result : results) {
                if (text.size() > 1) {
                    text += ',';
                }
#ifdef READER_DICT_ENGINE
                QJsonObject pictures;
                text += PopupModel::EntryText(result, {}, {}, sent, pictures);
#else
                text += QJsonDocument(PopupModel::Entry(result, {})).toJson(QJsonDocument::Compact);
#endif
            }
            text += ']';
            hovers.push_back(timer.nsecsElapsed() / 1e6);
        }
    }
    report("lookup", lookups);
    report("lookup + popup model", hovers);
}
