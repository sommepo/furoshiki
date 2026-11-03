// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

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

TEST_CASE("Console: which files are profiles: the PS2's disc serials", "[reader][catalog][console]") {
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("SLPM-66302.lua")) ==
            QStringLiteral("SLPM-66302"));
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("SLPM-66302.table.bin")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("notes.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("0100d7f00fb1a000.lua")).has_value());
    // A disc's serial as the console writes it: four letters, a hyphen, five digits.
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("SLPM66302.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("slpm-66302.lua")).has_value());
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
    // No bundled game is without voices so far.
    REQUIRE(without_voices.isEmpty());
    // At least the games of the published list (サイレントヒル2, Ever17, Remember11,
    // ドラゴンクエストV, ひぐらしのなく頃に祭 カケラ遊び, Never7, アマガミ): a checkout of one's
    // own has more, those whose scripts need a table that is not published.
    for (const char* id : {"SLPM-65051", "SLPM-65421", "SLPM-65550", "SLPM-65555", "SLPM-66913",
                           "SLPS-25256", "SLPS-25918"}) {
        INFO(id);
        REQUIRE(ids.contains(QString::fromLatin1(id)));
    }
}

TEST_CASE("Console: a card's {url} is the game's page at PlayStation, without the hyphen",
          "[reader][anki][console]") {
    REQUIRE(SourceAddress::Of(QStringLiteral("SLPM-66302")) ==
            QStringLiteral("https://www.jp.playstation.com/software/title/slpm66302.html"));
    REQUIRE(SourceAddress::Of(QStringLiteral(" slpm-66302 ")) ==
            QStringLiteral("https://www.jp.playstation.com/software/title/slpm66302.html"));
    REQUIRE(SourceAddress::Of(QStringLiteral("SLPM66302")).isEmpty());
}
