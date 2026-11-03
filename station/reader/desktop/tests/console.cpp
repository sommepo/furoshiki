// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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

TEST_CASE("Console: which files are profiles: the PlayStation's serials", "[reader][catalog][console]") {
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("SLPS-01794.lua")) == QStringLiteral("SLPS-01794"));
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("SCPS-10001.lua")) == QStringLiteral("SCPS-10001"));
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("SLPS-01645.charset")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("notes.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("README.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("HDR-0164.lua")).has_value());
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
    // The games whose lines have no voices get no audio on their cards unless the user says
    // otherwise: both sound novels, and every disc of Final Fantasy VII and IX (IX's under both
    // its serials: the database's for the app, the boot file's for Furoshiki's core).
    const QStringList voiceless{
        QStringLiteral("SCPS-45500"), QStringLiteral("SCPS-45501"), QStringLiteral("SCPS-45502"),
        QStringLiteral("SCPS-45503"), QStringLiteral("SLPS-00700"), QStringLiteral("SLPS-00701"),
        QStringLiteral("SLPS-00702"), QStringLiteral("SLPS-01645"), QStringLiteral("SLPS-01794"),
        QStringLiteral("SLPS-02000"), QStringLiteral("SLPS-02001"), QStringLiteral("SLPS-02002"),
        QStringLiteral("SLPS-02003")};
    std::sort(without_voices.begin(), without_voices.end());
    REQUIRE(without_voices == voiceless);
    // The rest leave a card's audio to the reader's setting (Memories Off has voices).
    QStringList expected = voiceless;
    expected << QStringLiteral("SCPS-10053") << QStringLiteral("SCPS-10054")
             << QStringLiteral("SCPS-10131") << QStringLiteral("SCPS-10132")
             << QStringLiteral("SLPS-00404") << QStringLiteral("SLPS-02296");
    std::sort(expected.begin(), expected.end());
    std::sort(ids.begin(), ids.end());
    REQUIRE(ids == expected);
}

TEST_CASE("Console: a card's {url} is the game's page on PlayStation's Japanese software list",
          "[reader][anki][console]") {
    REQUIRE(SourceAddress::Of(QStringLiteral("SLPS-69002")) ==
            QStringLiteral("https://www.jp.playstation.com/software/title/slps69002.html"));
    REQUIRE(SourceAddress::Of(QStringLiteral(" slps-01794 ")) ==
            QStringLiteral("https://www.jp.playstation.com/software/title/slps01794.html"));
}
