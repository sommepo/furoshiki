// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "reader/desktop/catalog.h"
#include "reader/desktop/settings.h"
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;

TEST_CASE("Catalog: a profile says what it is in its first lines", "[reader][catalog]") {
    const QString unknown = TestUnknownGame;
    const ProfileInfo info = Catalog::Describe(unknown, Q(uR"(
-- reader: no hooks
-- reader: title 作り話 (A Made-up Game)
--   reader:   reads   Dialogue and choices
-- reader: version Base game, version 1
-- reader: something a later reader may know
-- An ordinary comment about how the game keeps its text.
local window = 0x1234
-- reader: title Not this: the comments at the top have ended
)"),
                                               false);
    REQUIRE(info.title == Q(u"作り話 (A Made-up Game)"));
    REQUIRE(info.reads == QStringLiteral("Dialogue and choices"));
    REQUIRE(info.version == QStringLiteral("Base game, version 1"));
    REQUIRE_FALSE(info.own);
    // A profile that says nothing is known by its ID.
    const ProfileInfo silent =
        Catalog::Describe(unknown, QStringLiteral("-- A profile of my own.\nlocal x = 1\n"), true);
    REQUIRE(silent.title == unknown);
    REQUIRE(silent.reads.isEmpty());
    REQUIRE(silent.version.isEmpty());
    REQUIRE(silent.own);
}

TEST_CASE("Catalog: which files are profiles", "[reader][catalog]") {
    // The console's own examples are in tests/console.cpp.
    REQUIRE(Catalog::TitleIdOf(TestGame + QStringLiteral(".lua")) == TestGame);
    REQUIRE_FALSE(Catalog::TitleIdOf(TestGame + QStringLiteral(".table.bin")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("notes.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QStringLiteral("README.lua")).has_value());
    REQUIRE_FALSE(Catalog::TitleIdOf(QString::fromLatin1(Reader::ConsoleTraits::Test::ForeignId) +
                                     QStringLiteral(".lua"))
                      .has_value());
}

TEST_CASE("Settings: the reader apps share their settings; an app keeps its own beside them",
          "[reader][settings]") {
    QTemporaryDir folder;
    const QString own = folder.filePath(QStringLiteral("app.ini"));
    const QString shared = folder.filePath(QStringLiteral("shared.ini"));
    // From before: everything in the app's own file.
    {
        ReaderSettings before(own);
        before.SetValue(ReaderSettings::KeyAnkiDeck, QStringLiteral("Mining"));
        before.SetValue(ReaderSettings::KeyCursorButton, 7);
        before.SetValue(QStringLiteral("picture/crt"), QStringLiteral("enabled=1"));
        before.Sync();
    }
    {
        ReaderSettings settings(own, shared);
        REQUIRE(settings.AnkiDeck() == QStringLiteral("Mining"));
        REQUIRE(settings.CursorButton() == 7);
        settings.SetValue(ReaderSettings::KeyAnkiTags, QStringLiteral("reading"));
        settings.Sync();
    }
    // Another app sees the shared ones and not the first app's own.
    QTemporaryDir other_folder;
    ReaderSettings other(other_folder.filePath(QStringLiteral("app.ini")), shared);
    REQUIRE(other.AnkiDeck() == QStringLiteral("Mining"));
    REQUIRE(other.AnkiTags() == QStringLiteral("reading"));
    REQUIRE(other.CursorButton() == Reader::ConsoleTraits::CursorButtonDefault);
    REQUIRE(other.Value(QStringLiteral("picture/crt")).toString().isEmpty());
    // The first app's own file keeps only what is its own.
    ReaderSettings first_alone(own);
    REQUIRE(first_alone.Value(ReaderSettings::KeyAnkiDeck).toString().isEmpty());
    REQUIRE(first_alone.Value(QStringLiteral("picture/crt")).toString() == QStringLiteral("enabled=1"));
    // A setting the shared file has wins over what an app still had from before.
    {
        ReaderSettings stale(other_folder.filePath(QStringLiteral("stale.ini")));
        stale.SetValue(ReaderSettings::KeyAnkiDeck, QStringLiteral("Old deck"));
        stale.Sync();
    }
    ReaderSettings later(other_folder.filePath(QStringLiteral("stale.ini")), shared);
    REQUIRE(later.AnkiDeck() == QStringLiteral("Mining"));
}

TEST_CASE("Settings: pointing at a word looks it up unless switched off", "[reader][settings]") {
    QTemporaryDir folder;
    ReaderSettings settings(folder.filePath(QStringLiteral("reader.ini")));
    REQUIRE(settings.HoverLookup() == QStringLiteral("always"));
    settings.SetValue(ReaderSettings::KeyHoverLookup, QStringLiteral("shift"));
    REQUIRE(settings.HoverLookup() == QStringLiteral("shift"));
    settings.SetValue(ReaderSettings::KeyHoverLookup, QStringLiteral("off"));
    REQUIRE(settings.HoverLookup() == QStringLiteral("off"));
    settings.SetValue(ReaderSettings::KeyHoverLookup, QStringLiteral("nonsense"));
    REQUIRE(settings.HoverLookup() == QStringLiteral("always"));
}

TEST_CASE("Settings: where the saves are kept is a choice, and an older setup is kept",
          "[reader][settings]") {
    QTemporaryDir folder;
    ReaderSettings settings(folder.filePath(QStringLiteral("reader.ini")));
    using Method = ReaderSettings::SaveSyncMethod;
    // Nothing is preset, and the question of the first start has not been asked.
    REQUIRE(settings.SaveSyncMode() == Method::Off);
    REQUIRE_FALSE(settings.SaveSyncEnabled());
    REQUIRE_FALSE(settings.SaveSyncAsked());
    REQUIRE(settings.SaveSyncPort() == 8642);
    // A setup from before there was a choice: the switch alone means another computer.
    settings.SetValue(ReaderSettings::KeySaveSyncEnabled, true);
    REQUIRE(settings.SaveSyncMode() == Method::Client);
    REQUIRE(settings.SaveSyncEnabled());
    // The choice comes first once it is made.
    settings.SetValue(ReaderSettings::KeySaveSyncMode, QStringLiteral("hub"));
    REQUIRE(settings.SaveSyncMode() == Method::Hub);
    settings.SetValue(ReaderSettings::KeySaveSyncMode, QStringLiteral("off"));
    REQUIRE(settings.SaveSyncMode() == Method::Off);
    REQUIRE_FALSE(settings.SaveSyncEnabled());
    settings.SetValue(ReaderSettings::KeySaveSyncMode, QStringLiteral(" Client "));
    REQUIRE(settings.SaveSyncMode() == Method::Client);
    for (const Method method : {Method::Off, Method::Hub, Method::Client}) {
        settings.SetValue(ReaderSettings::KeySaveSyncMode, ReaderSettings::SaveSyncModeText(method));
        REQUIRE(settings.SaveSyncMode() == method);
    }
    // A port a program may not listen on is not taken.
    settings.SetValue(ReaderSettings::KeySaveSyncPort, 80);
    REQUIRE(settings.SaveSyncPort() == 1024);
    settings.SetValue(ReaderSettings::KeySaveSyncPort, 18642);
    REQUIRE(settings.SaveSyncPort() == 18642);
}

TEST_CASE("Settings: the line's audio on a card is a choice, for all games and per game",
          "[reader][settings]") {
    QTemporaryDir folder;
    ReaderSettings settings(folder.filePath(QStringLiteral("reader.ini")));
    using Audio = ReaderSettings::CardAudio;
    const QString game = TestGame;
    const QString other = TestOtherGame;
    // Nothing is preset: every card asks.
    REQUIRE(settings.CardAudioGeneral() == Audio::Ask);
    REQUIRE_FALSE(settings.CardAudioOf(game).has_value());
    REQUIRE(settings.CardAudioFor(game) == Audio::Ask);
    // A game's own choice comes first; the others follow the general one.
    settings.SetCardAudioOf(game, Audio::Never);
    settings.SetValue(ReaderSettings::KeyCardAudio, QStringLiteral("always"));
    REQUIRE(settings.CardAudioFor(game) == Audio::Never);
    REQUIRE(settings.CardAudioFor(other) == Audio::Always);
    REQUIRE(settings.CardAudioFor(QString()) == Audio::Always);
    // Taken away, the game follows the general one again.
    settings.SetCardAudioOf(game, std::nullopt);
    REQUIRE_FALSE(settings.CardAudioOf(game).has_value());
    REQUIRE(settings.CardAudioFor(game) == Audio::Always);
    // What the game's profile says stands between the two: before the general choice, after
    // the user's own for the game.
    REQUIRE(settings.CardAudioFor(game, Audio::Never) == Audio::Never);
    settings.SetCardAudioOf(game, Audio::Ask);
    REQUIRE(settings.CardAudioFor(game, Audio::Never) == Audio::Ask);
    settings.SetCardAudioOf(game, std::nullopt);
    REQUIRE(Catalog::Describe(game, QStringLiteral("-- reader: audio Never\n"), false).audio ==
            QStringLiteral("never"));
    // What is not one of the three is no choice.
    settings.SetValue(ReaderSettings::KeyCardAudio, QStringLiteral("sometimes"));
    REQUIRE(settings.CardAudioGeneral() == Audio::Ask);
    REQUIRE(ReaderSettings::CardAudioValue(Audio::Never) == QStringLiteral("never"));
}

TEST_CASE("Catalog: a profile says how its game is played by touch; the setting is on",
          "[reader][catalog][settings]") {
    const QString game = TestGame;
    REQUIRE(Catalog::Describe(game, QStringLiteral("-- reader: touch Press\n"), false).touch ==
            QStringLiteral("press"));
    REQUIRE(Catalog::Describe(game, QStringLiteral("-- reader: touch game\n"), false).touch ==
            QStringLiteral("game"));
    // Anything else, or nothing, is a game not played by touch.
    REQUIRE(Catalog::Describe(game, QStringLiteral("-- reader: touch sometimes\n"), false)
                .touch.isEmpty());
    REQUIRE(Catalog::Describe(game, QStringLiteral("-- reader: title x\n"), false).touch.isEmpty());
    QTemporaryDir folder;
    ReaderSettings settings(folder.filePath(QStringLiteral("reader.ini")));
    REQUIRE_FALSE(settings.TouchPlay()); // off at first on a computer
    settings.SetValue(ReaderSettings::KeyTouchPlay, true);
    REQUIRE(settings.TouchPlay());
}
