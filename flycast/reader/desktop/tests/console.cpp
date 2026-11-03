// SPDX-FileCopyrightText: Flycast Reader fork contributors
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

TEST_CASE("Console: which files are profiles: the Dreamcast's product numbers", "[reader][catalog][console]") {
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("HDR-0164.lua")) == QStringLiteral("HDR-0164"));
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("T-1214M.lua")) == QStringLiteral("T-1214M"));
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("MK-51058.lua")) == QStringLiteral("MK-51058"));
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("HDR-0164.table.bin")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("notes.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("README.lua")).has_value());
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
    // otherwise. None of the bundled games is one so far.
    REQUIRE(without_voices.isEmpty());
    std::sort(ids.begin(), ids.end());
    REQUIRE(ids == QStringList{QStringLiteral("6107179"), QStringLiteral("HDR-0072"), QStringLiteral("HDR-0074"), QStringLiteral("HDR-0151"),
                               QStringLiteral("HDR-0164"), QStringLiteral("T14301M"),
                               QStringLiteral("T35401M"), QStringLiteral("T46303M")});
}

TEST_CASE("Console: a card's {url} is Sega's list of the console's software", "[reader][anki][console]") {
    REQUIRE(SourceAddress::Of(QStringLiteral("T-99002")) ==
            QStringLiteral("https://sega.jp/history/hard/dreamcast/software.html#T-99002"));
    REQUIRE(SourceAddress::Of(QStringLiteral(" hdr-0164 ")) ==
            QStringLiteral("https://sega.jp/history/hard/dreamcast/software.html#HDR-0164"));
}
