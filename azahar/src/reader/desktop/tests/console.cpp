// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// The tests of what is this console's own (core/reader/console.h, the bundled profiles). The
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

TEST_CASE("Console: which files are profiles: the 3DS's title IDs", "[reader][catalog][console]") {
    REQUIRE(Catalog::TitleIdOf(QStringLiteral("0100D7F00FB1A000.lua")) ==
            QStringLiteral("0100D7F00FB1A000"));
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("0100D7F00FB1A000.table.bin")).has_value());
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
        const ProfileInfo info = Catalog::Describe(*id, QString::fromUtf8(file.read(4096)), false);
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
    std::sort(ids.begin(), ids.end());
    // Shin Megami Tensei IV.
    REQUIRE(ids == QStringList{QStringLiteral("0004000000088A00")});
}

TEST_CASE("Console: a card's {url} is empty: a 3DS game has no page its title ID leads to",
          "[reader][anki][console]") {
    REQUIRE(SourceAddress::Of(QStringLiteral("0004000000088A00")).isEmpty());
}
