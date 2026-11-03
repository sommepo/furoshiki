// SPDX-FileCopyrightText: PPSSPP Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The tests of what is this console's own (reader/core/console.h, the bundled scripts). The
// other tests are shared by every reader fork (the repository's shared/).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include "reader/desktop/anki.h"
#include "reader/desktop/catalog.h"
#include "reader/desktop/settings.h"
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;

TEST_CASE("Console: which files are profiles: the PSP's game IDs", "[reader][catalog][console]") {
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("ULJS00219.lua")) ==
            QStringLiteral("ULJS00219"));
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("ULJS00219.table.bin")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("notes.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("0100d7f00fb1a000.lua")).has_value());
}

TEST_CASE("Catalog: every bundled profile says its game, what is read and for which version",
          "[reader][catalog]") {
    const QDir folder(QStringLiteral(READER_BUNDLED_PROFILE_DIR));
    QStringList ids;
    QStringList without_voices;
    for (const QString& name : folder.entryList(QDir::Files)) {
        const auto id = Catalog::TitleIdOf(name);
        if (!id) {
            continue;
        }
        ids.append(*id);
        QFile file(folder.filePath(name));
        REQUIRE(file.open(QIODevice::ReadOnly));
        const ProfileInfo info =
            Catalog::Describe(*id, QString::fromUtf8(file.read(4096)), false);
        INFO(id->toStdString());
        REQUIRE(info.title != *id);
        REQUIRE_FALSE(info.title.trimmed().isEmpty());
        REQUIRE_FALSE(info.reads.trimmed().isEmpty());
        REQUIRE_FALSE(info.version.trimmed().isEmpty());
        // A profile that speaks of the audio says one of the three choices.
        if (!info.audio.isEmpty()) {
            REQUIRE(ReaderSettings::CardAudioOfValue(info.audio).has_value());
            if (info.audio == QStringLiteral("never")) {
                without_voices.append(*id);
            }
        }
    }
    // The games whose lines have no voices: 428 and Sora no Kiseki FC. Their cards get no audio
    // unless the user says otherwise.
    std::sort(without_voices.begin(), without_voices.end());
    REQUIRE(without_voices == QStringList{QStringLiteral("ULJM05170"), QStringLiteral("ULJS00219")});
    std::sort(ids.begin(), ids.end());
    REQUIRE(ids == QStringList{QStringLiteral("NPJH50372"), QStringLiteral("ULJM05078"),
                               QStringLiteral("ULJM05170"), QStringLiteral("ULJM05603"),
                               QStringLiteral("ULJM05718"), QStringLiteral("ULJM05719"),
                               QStringLiteral("ULJM05821"), QStringLiteral("ULJM05887"),
                               QStringLiteral("ULJM06119"), QStringLiteral("ULJM06232"),
                               QStringLiteral("ULJM06233"), QStringLiteral("ULJS00219")});
    // The two discs of D.C.I&II P.S.P. are one program built twice, and so are Grisaia no
    // Kajitsu's: each pair's profiles are the same after the lines that name the disc.
    const auto body = [&folder](const char* name) {
        QFile file(folder.filePath(QString::fromLatin1(name)));
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QByteArray all = file.readAll();
        const qsizetype at = all.indexOf("-- The two discs are one program");
        REQUIRE(at > 0);
        return all.mid(at);
    };
    REQUIRE(body("ULJM05718.lua") == body("ULJM05719.lua"));
    REQUIRE(body("ULJM06232.lua") == body("ULJM06233.lua"));
}

TEST_CASE("Console: a card's {url} is the game's page at PlayStation", "[reader][anki][console]") {
    REQUIRE(SourceAddress::Of(QStringLiteral("ULJM05000")) ==
            QStringLiteral("https://www.jp.playstation.com/software/title/uljm05000.html"));
    REQUIRE(SourceAddress::Of(QStringLiteral(" uljs00219 ")) ==
            QStringLiteral("https://www.jp.playstation.com/software/title/uljs00219.html"));
}
