// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// What the reader apps' files amount to, with made-up files; the sounds' form; the one
// program's settings and their layers; the window's themes as their files say them.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include "app_folders.h"
#include "library.h"
#include "look.h"
#include "old_settings.h"
#ifndef READER_NO_SAVE_SYNC
#include "pairing.h"
#endif
#include "play_settings.h"
#include "popup_controls.h"
#include "popup_style.h"
#include "sounds.h"

namespace {

int failures = 0;
#define CHECK(what)                                                                     \
    do {                                                                                \
        if (!(what)) {                                                                  \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what);              \
            ++failures;                                                                 \
        }                                                                               \
    } while (false)

void Write(const QString& path, const QByteArray& text) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text);
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir folder;
    QDir().mkpath(folder.filePath(QStringLiteral("apps")));
    const QString program = folder.filePath(QStringLiteral("made-up-reader"));
    Write(program, "x");
    Write(folder.filePath(QStringLiteral("apps/b-reader.json")),
          QByteArray(R"({"app":"b-reader","name":"B Reader","system":"Dreamcast","program":")") + program.toUtf8() +
              R"(","game_args":["-fullscreen","%ROM%"],"menu_args":["-menu"],"written":1000,
              "games":[{"id":"T-1","title":"あるゲーム","path":"/games/one.chd","cover":""},
                       {"id":"T-2","title":"Zebra","path":"/games/two.chd"},
                       {"id":"T-3","title":"Not for reading","path":"/games/three.chd","readable":false},
                       {"id":"T-4","title":"No file"}],
              "stats":{"games":{"T-2":"Zebra"},"days":[
                {"day":"2026-10-01","game":"T-2","chars":100,"active_ms":60000},
                {"day":"2026-10-03","game":"T-2","chars":50,"active_ms":120000}]}})");
    Write(folder.filePath(QStringLiteral("apps/a-reader.json")),
          R"({"app":"a-reader","name":"A Reader","system":"Nintendo Switch","program":"/nowhere/a",
              "games":[{"id":"0100","title":"Alpha","path":"/games/alpha.nsp"}],
              "stats":{"days":[{"day":"2026-10-03","game":"0100","chars":7,"active_ms":1000},
                               {"day":"2026-10-03","game":"gone","chars":1,"active_ms":1}]}})");
    Write(folder.filePath(QStringLiteral("apps/c-reader.json")),
          R"({"app":"c-reader","name":"C Reader","system":"PlayStation 2","program":"/nowhere/c",
              "games":[{"id":"S-1","title":"Twice","path":"/games/Twice (Japan) (Disc 1).iso"},
                       {"id":"S-1","title":"Twice","path":"/games/Twice (Japan) (Disc 2).iso"},
                       {"id":"S-2","title":"Copy","path":"/games/copy a.iso"},
                       {"id":"S-2","title":"Copy","path":"/games/copy b.iso"}]})");
    // An app whose games are installed titles, started by their ID; with its console's label,
    // place on the line and words for full screen of its own.
    Write(folder.filePath(QStringLiteral("apps/d-reader.json")),
          R"({"format":1,"app":"d-reader","name":"D Reader","system":"Made-up Handheld","short":"MUH","order":5,
              "program":"/nowhere/d","game_args":["-r","%ID%"],"fullscreen_args":["-F"],
              "games":[{"id":"ABCD00001","title":"Installed"},{"id":"","title":"No ID"}]})");
    Write(folder.filePath(QStringLiteral("apps/newer.json")), R"({"format":2,"app":"newer-reader"})");
    Write(folder.filePath(QStringLiteral("apps/broken.json")), "{ not json");
    Write(folder.filePath(QStringLiteral("apps/other.json")), R"({"something":"else"})");

    Home::Library library = Home::Load(folder.path());
    const Home::Library whole = library;
    // The consoles in their order, whatever the files are called.
    CHECK(library.apps.size() == 4);
    CHECK(library.apps[0].app == QStringLiteral("d-reader")); // its own order, before the Switch's
    library.apps.removeFirst();
    CHECK(library.apps[0].app == QStringLiteral("a-reader"));
    CHECK(library.apps[1].app == QStringLiteral("c-reader"));
    CHECK(library.apps[2].app == QStringLiteral("b-reader"));
    CHECK(!library.apps[0].program_found);
    CHECK(library.apps[2].program_found);
    QStringList twice;
    for (const Home::Game& game : library.apps[1].games) {
        twice.append(game.title);
    }
    CHECK(twice == (QStringList{QStringLiteral("Twice · Disc 1"), QStringLiteral("Twice · Disc 2"),
                                QStringLiteral("Copy · copy a"), QStringLiteral("Copy · copy b")}));
    CHECK(library.apps[0].game_args == QStringList{QStringLiteral("%ROM%")});
    CHECK(library.problems.size() == 3); // broken, other, and newer (a format of the future)
    CHECK(library.problems.filter(QStringLiteral("newer.json")).size() == 1);
    // Only what can be read and has a file (or, for an app that starts games by ID, an ID);
    // the last read first, then by title.
    CHECK(library.games.size() == 8);
    library.games.removeIf([](const Home::Game& game) { return game.app == QStringLiteral("d-reader"); });
    CHECK(library.games.size() == 7);
    CHECK(library.games[0].title == QStringLiteral("Alpha") || library.games[0].title == QStringLiteral("Zebra"));
    CHECK(library.games[0].chars > 0);
    CHECK(library.games[6].chars == 0);
    for (const Home::Game& game : library.games) {
        if (game.id == QStringLiteral("T-2")) {
            CHECK(game.chars == 150);
            CHECK(game.active_ms == 180000);
            CHECK(game.last_day == QStringLiteral("2026-10-03"));
            CHECK(game.system == QStringLiteral("Dreamcast"));
        }
    }
    // The days of every app together, oldest first; a game no longer listed still counts.
    CHECK(library.days.size() == 2);
    CHECK(library.days[0].day == QStringLiteral("2026-10-01"));
    CHECK(library.days[1].chars == 58);
    CHECK(library.chars == 158);
    CHECK(library.apps[2].chars == 150);
    Home::Game two;
    two.id = QStringLiteral("T-2");
    two.path = QStringLiteral("/games/two.chd");
    CHECK(Home::GameArguments(library.apps[2], two, false) ==
          (QStringList{QStringLiteral("-fullscreen"), QStringLiteral("/games/two.chd")}));
    CHECK(Home::GameArguments(library.apps[2], two, true) == QStringList{QStringLiteral("/games/two.chd")});
    // By ID, with the app's own word for full screen added unless games start in a window.
    const Home::App& by_id = whole.apps[0];
    CHECK(!Home::NeedsFile(by_id));
    CHECK(by_id.games.size() == 1);
    CHECK(by_id.games[0].title == QStringLiteral("Installed"));
    CHECK(Home::GameArguments(by_id, by_id.games[0], false) ==
          (QStringList{QStringLiteral("-F"), QStringLiteral("-r"), QStringLiteral("ABCD00001")}));
    CHECK(Home::GameArguments(by_id, by_id.games[0], true) ==
          (QStringList{QStringLiteral("-r"), QStringLiteral("ABCD00001")}));
    // Words for full screen that only mean it together, beside others of the same kind.
    Home::App two_words;
    two_words.game_args = {QStringLiteral("-config"), QStringLiteral("x=1"), QStringLiteral("%ROM%")};
    two_words.fullscreen_args = {QStringLiteral("-config"), QStringLiteral("window:fullscreen=yes")};
    CHECK(Home::GameArguments(two_words, two, true) ==
          (QStringList{QStringLiteral("-config"), QStringLiteral("x=1"), QStringLiteral("/games/two.chd")}));
    CHECK(Home::GameArguments(two_words, two, false) ==
          (QStringList{QStringLiteral("-config"), QStringLiteral("window:fullscreen=yes"), QStringLiteral("-config"),
                       QStringLiteral("x=1"), QStringLiteral("/games/two.chd")}));
    CHECK(Home::WithoutFullScreen(two_words, Home::GameArguments(two_words, two, false)) ==
          Home::GameArguments(two_words, two, true));
    // The consoles' labels: the app's own, Furoshiki's, or the name.
    CHECK(Home::ShortSystem(whole, QStringLiteral("Made-up Handheld")) == QStringLiteral("MUH"));
    CHECK(Home::ShortSystem(whole, QStringLiteral("PlayStation 2")) == QStringLiteral("PS2"));
    CHECK(Home::ShortSystem(whole, QStringLiteral("Some Console")) == QStringLiteral("SOME CONSOLE"));
    // A game's discs count once among the statistics.
    CHECK(Home::DistinctGames(library.games).size() == 5);
    CHECK(Home::TimeText(0) == QStringLiteral("not yet"));
    CHECK(Home::TimeText(59000) == QStringLiteral("under a minute"));
    CHECK(Home::TimeText(45 * 60000) == QStringLiteral("45m"));
    CHECK(Home::TimeText(200 * 60000) == QStringLiteral("3h 20m"));
    CHECK(Home::CountText(12345) == QStringLiteral("12,345"));
    // The shared settings come back as they were written.
    QJsonObject steam;
    steam.insert(QStringLiteral("sensitivity"), 1.5);
    QJsonObject shared;
    shared.insert(QStringLiteral("steam"), steam);
    CHECK(Home::LoadShared(folder.path()).isEmpty());
    CHECK(Home::SaveShared(folder.path(), shared));
    CHECK(Home::LoadShared(folder.path()) == shared);
    // An empty folder is an empty library.
    CHECK(Home::Load(folder.filePath(QStringLiteral("none"))).apps.isEmpty());

    // Each sound is a WAV file with something in it, and short.
    for (int i = 0; i < static_cast<int>(Home::Sound::Count); ++i) {
        const QByteArray wav = Home::MakeSound(static_cast<Home::Sound>(i));
        CHECK(wav.startsWith("RIFF"));
        CHECK(wav.mid(8, 4) == "WAVE");
        CHECK(wav.size() > 44 + 1000);
        CHECK(wav.size() < 44 + 2 * 3 * 44100);
        bool loud = false;
        for (int at = 44; at + 1 < wav.size(); at += 2) {
            const int sample = static_cast<qint16>(static_cast<quint8>(wav[at]) | (static_cast<quint8>(wav[at + 1]) << 8));
            loud = loud || std::abs(sample) > 3000;
        }
        CHECK(loud);
    }
    // The settings for playing, in layers: a game's, a console's, all consoles', the core's.
    {
        const QString file = folder.filePath(QStringLiteral("program/settings.json"));
        Home::PlaySetting resolution{QStringLiteral("picture.resolution"), {}, Home::PlaySetting::Number,
                                     QStringLiteral("1"), 1, 8, 1, {}};
        Home::PlaySetting true_color{QStringLiteral("ps1.true_color"), QStringLiteral("True colour"),
                                     Home::PlaySetting::Switch, QStringLiteral("0"), 0, 0, 0, {}};
        Home::PlaySetting aspect{QStringLiteral("picture.aspect"), {}, Home::PlaySetting::Choice, QStringLiteral("auto"),
                                 0, 0, 0, {QStringLiteral("auto"), QStringLiteral("4:3"), QStringLiteral("16:9")}};
        Home::PlaySetting crt{QStringLiteral("picture.crt"), {}, Home::PlaySetting::Text, {}, 0, 0, 0, {}};
        CHECK(resolution.Common() && !true_color.Common() && crt.Hidden() && !aspect.Hidden());
        Home::PlaySettings settings(file);
        // Nothing set: the core's own.
        CHECK(settings.Value(resolution, QStringLiteral("ps1")).value == QStringLiteral("1"));
        CHECK(settings.Value(resolution, QStringLiteral("ps1")).from == Home::PlaySettings::Layer::Default);
        // All consoles, then one console differing, then one game.
        settings.Set(QStringLiteral("picture.resolution"), QStringLiteral("2"));
        CHECK(settings.Value(resolution, QStringLiteral("ps1")).value == QStringLiteral("2"));
        CHECK(settings.Value(resolution, QStringLiteral("ps1")).from == Home::PlaySettings::Layer::All);
        settings.Set(QStringLiteral("picture.resolution"), QStringLiteral("4"), QStringLiteral("ps1"));
        CHECK(settings.Value(resolution, QStringLiteral("ps1")).value == QStringLiteral("4"));
        CHECK(settings.Value(resolution, QStringLiteral("ps2")).value == QStringLiteral("2"));
        settings.Set(QStringLiteral("picture.resolution"), QStringLiteral("3"), QStringLiteral("ps1"), QStringLiteral("SLPS-01645"));
        CHECK(settings.Value(resolution, QStringLiteral("ps1"), QStringLiteral("SLPS-01645")).from ==
              Home::PlaySettings::Layer::Game);
        CHECK(settings.HasGame(QStringLiteral("ps1"), QStringLiteral("SLPS-01645")));
        CHECK(settings.Value(resolution, QStringLiteral("ps1"), QStringLiteral("SLPS-01794")).value == QStringLiteral("4"));
        // A console's own setting is not taken from all consoles' layer.
        settings.Set(QStringLiteral("ps1.true_color"), QStringLiteral("1"));
        CHECK(settings.Value(true_color, QStringLiteral("ps1")).value == QStringLiteral("0"));
        settings.Clear(QStringLiteral("ps1.true_color"));
        settings.Set(QStringLiteral("ps1.true_color"), QStringLiteral("1"), QStringLiteral("ps1"));
        // "Use the shared value" takes the console's away.
        settings.Clear(QStringLiteral("picture.resolution"), QStringLiteral("ps1"));
        CHECK(!settings.IsSet(QStringLiteral("picture.resolution"), QStringLiteral("ps1")));
        CHECK(settings.Value(resolution, QStringLiteral("ps1")).value == QStringLiteral("2"));
        // Kept in its file and read back.
        CHECK(settings.Save());
        const Home::PlaySettings again(file);
        CHECK(again.Value(true_color, QStringLiteral("ps1")).value == QStringLiteral("1"));
        CHECK(again.Value(resolution, QStringLiteral("ps1"), QStringLiteral("SLPS-01645")).value == QStringLiteral("3"));
        // What a core is handed: every setting it understands, the reader's own left out.
        const Home::PlayConsole ps1{QStringLiteral("ps1"), QStringLiteral("PS1"), QStringLiteral("station-reader"),
                                    {resolution, true_color, aspect, crt}};
        const auto values = again.ValuesFor(ps1);
        CHECK(values.size() == 3);
        CHECK(values.contains({QStringLiteral("picture.resolution"), QStringLiteral("2")}));
        CHECK(values.contains({QStringLiteral("ps1.true_color"), QStringLiteral("1")}));
        CHECK(values.contains({QStringLiteral("picture.aspect"), QStringLiteral("auto")}));
        // A page's moves: a switch flips, a choice goes round, a number stays within its ends.
        CHECK(true_color.Moved(QStringLiteral("0"), 1) == QStringLiteral("1"));
        CHECK(true_color.Moved(QStringLiteral("1"), -1) == QStringLiteral("0"));
        CHECK(aspect.Moved(QStringLiteral("16:9"), 1) == QStringLiteral("auto"));
        CHECK(aspect.Moved(QStringLiteral("auto"), -1) == QStringLiteral("16:9"));
        CHECK(resolution.Moved(QStringLiteral("8"), 1) == QStringLiteral("8"));
        CHECK(resolution.Moved(QStringLiteral("2"), 1) == QStringLiteral("3"));
        CHECK(resolution.Shown(QStringLiteral("3")) == QStringLiteral("3×"));
    }

    // The popup's look in the layers (popup_style.h): set for all consoles, one console or one
    // game; nothing set leaves the reader's own setting; a style file out and back in.
    {
        QTemporaryDir program;
        const QString folder = Home::PopupStyle::Folder(program.path());
        CHECK(QFile::exists(folder + QStringLiteral("/example.css")));
        QFile mine(folder + QStringLiteral("/mine.css"));
        CHECK(mine.open(QIODevice::WriteOnly) && mine.write(".popup { border-radius: 9px; }") > 0);
        mine.close();
        const QList<Home::PlaySetting> popup = Home::PopupStyle::Settings(folder);
        CHECK(popup.size() == 4 && popup[0].Common() && popup[3].choices.contains(QStringLiteral("mine")));
        Home::PlaySettings settings(program.filePath(QStringLiteral("settings.json")));
        const auto layered = [&](const char* key, const char* console = "", const char* game = "") {
            return Home::PopupStyle::Layered(settings, folder, QString::fromLatin1(key), QString::fromLatin1(console),
                                             QString::fromLatin1(game));
        };
        CHECK(!layered("reader_popup_theme").has_value());
        settings.Set(QStringLiteral("popup.theme"), QStringLiteral("paper"));
        settings.Set(QStringLiteral("popup.theme"), QStringLiteral("yomitan-dark"), QStringLiteral("ps2"));
        settings.Set(QStringLiteral("popup.css"), QStringLiteral("mine"), QStringLiteral("ps2"), QStringLiteral("SLPM-65550"));
        settings.Set(QStringLiteral("popup.mark_unknown"), QStringLiteral("0"), QStringLiteral("ps1"));
        CHECK(layered("reader_popup_theme", "ps1") == QStringLiteral("paper"));
        CHECK(layered("reader_popup_theme", "ps2") == QStringLiteral("yomitan-dark"));
        CHECK(layered("reader_popup_css", "ps2", "SLPM-65550") == QStringLiteral(".popup { border-radius: 9px; }"));
        CHECK(!layered("reader_popup_css", "ps2").has_value());
        CHECK(layered("reader_popup_mark_unknown", "ps1") == QStringLiteral("false"));
        CHECK(!layered("reader_crt").has_value());
        // No core is handed the popup's.
        const Home::PlayConsole ps2{QStringLiteral("ps2"), QStringLiteral("PS2"), QStringLiteral("pcsx2-reader"), popup};
        CHECK(settings.ValuesFor(ps2, QStringLiteral("SLPM-65550")).isEmpty());
        // Out as one file, and in again for another console, its CSS a file of its own.
        const QJsonObject style = Home::PopupStyle::Export(settings, folder, QStringLiteral("ps2"), QStringLiteral("SLPM-65550"),
                                                           [](const QString&) { return QStringLiteral("count"); });
        CHECK(style.value(QStringLiteral("theme")).toString() == QStringLiteral("yomitan-dark"));
        CHECK(style.value(QStringLiteral("css")).toString().contains(QStringLiteral("9px")));
        CHECK(style.value(QStringLiteral("lookups")).toString() == QStringLiteral("count"));
        CHECK(Home::PopupStyle::Import(style, settings, folder, QStringLiteral("from ps2!"), QStringLiteral("psp"), {}).isEmpty());
        CHECK(layered("reader_popup_theme", "psp") == QStringLiteral("yomitan-dark"));
        CHECK(settings.Raw(QStringLiteral("popup.css"), QStringLiteral("psp")) == QStringLiteral("from-ps2-"));
        CHECK(QFile::exists(folder + QStringLiteral("/from-ps2-.css")));
        CHECK(!Home::PopupStyle::Import(QJsonObject{{QStringLiteral("theme"), QStringLiteral("paper")}}, settings, folder,
                                        QStringLiteral("x"), {}, {}).isEmpty());
    }

    // The popup's controls (popup_controls.h): one block of the user's CSS, read back as it was
    // written, replaced in place, taken out when every control is the look's own.
    {
        namespace Controls = Home::PopupControls;
        const QString mine = QStringLiteral(".popup { border-radius: 9px; }\n");
        CHECK(Controls::Read(mine).isEmpty());
        Controls::Values values{{QStringLiteral("headword"), QStringLiteral("30")},
                                {QStringLiteral("background"), QStringLiteral("#802b2119")},
                                {QStringLiteral("font"), QStringLiteral("mincho")}};
        const QString with = Controls::With(mine, values);
        CHECK(with.startsWith(mine));
        CHECK(with.contains(QStringLiteral(".headword { font-size: calc(30px * var(--scale)) !important; }")));
        CHECK(with.contains(QStringLiteral("rgba(43, 33, 25, 0.502)")));
        CHECK(with.contains(QStringLiteral("Noto Serif JP")));
        CHECK(Controls::Read(with) == values);
        values.insert(QStringLiteral("corners"), QStringLiteral("0"));
        values.remove(QStringLiteral("headword"));
        const QString again = Controls::With(with, values);
        CHECK(again.count(QStringLiteral("furoshiki-controls end")) == 1 && !again.contains(QStringLiteral("30px")));
        CHECK(again.contains(QStringLiteral("border-radius: 0px !important;")));
        CHECK(Controls::With(again, {}) == mine);
        for (const Controls::Control& control : Controls::Controls()) {
            Controls::Values one{{control.key, control.kind == Controls::Control::Colour ? QStringLiteral("#ff102030")
                                               : control.kind == Controls::Control::Font ? QStringLiteral("rounded")
                                                                                         : QString::number(control.low)}};
            CHECK(Controls::Block(one).count(QLatin1Char('{')) >= 2); // a rule of its own, besides the kept values
        }
    }

    // The old apps' settings: only what differs from each emulator's own defaults.
    {
        using Home::OldSettings::Value;
        const auto has = [](const QList<Value>& values, const char* console, const char* key, const char* value) {
            for (const Value& one : values) {
                if (one.console == QLatin1String(console) && one.key == QLatin1String(key)) {
                    return one.value == QLatin1String(value);
                }
            }
            return false;
        };
        // DuckStation's file as Station Reader writes it, everything at its default...
        const QString station = QStringLiteral(
            "[Console]\nRegion = Auto\n\n[GPU]\nRenderer = OpenGL\nResolutionScale = 1\nTrueColor = false\n"
            "TextureFilter = Nearest\nPGXPEnable = false\n\n[Display]\nAspectRatio = Auto (Game Native)\n"
            "Stretch = false\nVSync = false\n\n[CDROM]\nReadSpeedup = 1\n\n[Audio]\nOutputVolume = 100\n"
            "OutputMuted = false\n\n[BIOS]\nPatchFastBoot = false\n\n[Reader]\nPlayStationHome = true\n");
        CHECK(Home::OldSettings::FromStation(station).isEmpty());
        // ... and with what a user changes.
        QString changed = station;
        changed.replace(QStringLiteral("ResolutionScale = 1"), QStringLiteral("ResolutionScale = 3"))
            .replace(QStringLiteral("TrueColor = false"), QStringLiteral("TrueColor = true"))
            .replace(QStringLiteral("TextureFilter = Nearest"), QStringLiteral("TextureFilter = JINC2"))
            .replace(QStringLiteral("AspectRatio = Auto (Game Native)"), QStringLiteral("AspectRatio = 4:3"))
            .replace(QStringLiteral("OutputVolume = 100"), QStringLiteral("OutputVolume = 60"))
            .replace(QStringLiteral("Region = Auto"), QStringLiteral("Region = NTSC-J"));
        const QList<Value> from_station = Home::OldSettings::FromStation(changed);
        CHECK(from_station.size() == 6);
        CHECK(has(from_station, "ps1", "picture.resolution", "3"));
        CHECK(has(from_station, "ps1", "ps1.true_color", "1"));
        CHECK(has(from_station, "ps1", "ps1.texture_filter", "jinc2"));
        CHECK(has(from_station, "ps1", "picture.aspect", "4:3"));
        CHECK(has(from_station, "ps1", "sound.volume", "60"));
        CHECK(has(from_station, "ps1", "ps1.region", "ntsc-j"));
        // Flycast's emu.cfg: the USA and English are its defaults, not the user's choice.
        const QString flycast = QStringLiteral(
            "[config]\nDreamcast.Language = 1\nDreamcast.Region = 1\naica.Volume = 100\nrend.Resolution = 1440\n"
            "rend.TextureFiltering = 0\nrend.WideScreen = yes\n[window]\n");
        const QList<Value> from_flycast = Home::OldSettings::FromFlycast(flycast);
        CHECK(from_flycast.size() == 2);
        CHECK(has(from_flycast, "dc", "picture.resolution", "3"));
        CHECK(has(from_flycast, "dc", "dc.widescreen", "1"));
        // PPSSPP's ppsspp.ini, with its old volume of 0 to 10.
        const QList<Value> from_ppsspp = Home::OldSettings::FromPpsspp(QStringLiteral(
            "[CPU]\nCPUCore = 1\n[Graphics]\nInternalResolution = 2\nTextureFiltering = 1\n[Sound]\n"
            "GlobalVolume = 5\n[SystemParam]\nGameLanguage = 0\nButtonPreference = 0\n"));
        CHECK(from_ppsspp.size() == 4);
        CHECK(has(from_ppsspp, "psp", "picture.resolution", "2"));
        CHECK(has(from_ppsspp, "psp", "sound.volume", "50"));
        CHECK(has(from_ppsspp, "psp", "psp.language", "japanese"));
        CHECK(has(from_ppsspp, "psp", "psp.confirm", "circle"));
#if !defined(_WIN32)
        // Read once, from where Flycast Reader keeps its file on Linux, into the console's
        // layer where the console has no value of its own.
        QTemporaryDir old_home;
        QDir(old_home.path()).mkpath(QStringLiteral("config/flycast-reader"));
        QFile cfg(old_home.filePath(QStringLiteral("config/flycast-reader/emu.cfg")));
        CHECK(cfg.open(QIODevice::WriteOnly));
        cfg.write(flycast.toUtf8());
        cfg.close();
        const QByteArray config_before = qgetenv("XDG_CONFIG_HOME");
        const QByteArray data_before = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_CONFIG_HOME", old_home.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", old_home.filePath(QStringLiteral("data")).toUtf8());
        Home::PlaySettings settings(old_home.filePath(QStringLiteral("settings.json")));
        settings.Set(QStringLiteral("dc.widescreen"), QStringLiteral("0"), QStringLiteral("dc"));
        const QList<Value> taken = Home::OldSettings::ReadOnce(settings, old_home.path());
        CHECK(taken.size() == 1 && has(taken, "dc", "picture.resolution", "3"));
        CHECK(settings.IsSet(QStringLiteral("picture.resolution"), QStringLiteral("dc")));
        CHECK(!settings.IsSet(QStringLiteral("picture.resolution")));
        Home::PlaySettings later(old_home.filePath(QStringLiteral("settings.json")));
        CHECK(later.OldAppsRead());
        CHECK(Home::OldSettings::ReadOnce(later, old_home.path()).isEmpty());
        for (const auto& [name, before] : {std::pair{"XDG_CONFIG_HOME", config_before}, std::pair{"XDG_DATA_HOME", data_before}}) {
            before.isEmpty() ? qunsetenv(name) : qputenv(name, before);
        }
#endif
    }

#if !defined(_WIN32)
    // The apps' saves where the apps keep them on Linux: Eden Reader's folder in the data home,
    // PPSSPP Reader's memory stick in the config home only when it is the reader app's.
    {
        QTemporaryDir apps;
        const QByteArray config_before = qgetenv("XDG_CONFIG_HOME");
        const QByteArray data_before = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_CONFIG_HOME", apps.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", apps.filePath(QStringLiteral("data")).toUtf8());
        CHECK(Home::AppFolders::Of(QStringLiteral("switch"), {}).saves.isEmpty());
        CHECK(Home::AppFolders::Of(QStringLiteral("psp"), {}).saves.isEmpty());
        const QString eden = apps.filePath(QStringLiteral("data/eden-reader"));
        QDir(eden).mkpath(QStringLiteral("nand"));
        const Home::AppFolders::Folders the_switch = Home::AppFolders::Of(QStringLiteral("switch"), {});
        CHECK(the_switch.system == eden && the_switch.saves == eden + QStringLiteral("/nand"));
        // The program's own Switch folder counts with its keys only: Eden makes nand/ there by
        // itself, and that kept the app's keys away on the maintainer's Z13.
        const QString own = apps.filePath(QStringLiteral("own-switch"));
        QDir(own).mkpath(QStringLiteral("nand"));
        CHECK(!Home::AppFolders::HasSystemFiles(QStringLiteral("switch"), own));
        CHECK(Home::AppFolders::HasSystemFiles(QStringLiteral("ps2"), own));
        QDir(own).mkpath(QStringLiteral("keys"));
        QFile keys(QDir(own).filePath(QStringLiteral("keys/prod.keys")));
        CHECK(keys.open(QIODevice::WriteOnly) && keys.write("header_key = 00\n") > 0);
        keys.close();
        CHECK(Home::AppFolders::HasSystemFiles(QStringLiteral("switch"), own));
        // A plain PPSSPP's memory stick is not taken; the reader app's is.
        const QString stick = apps.filePath(QStringLiteral("config/ppsspp"));
        QDir(stick).mkpath(QStringLiteral("PSP/SAVEDATA"));
        QDir(stick).mkpath(QStringLiteral("PSP/SYSTEM"));
        QFile ini(QDir(stick).filePath(QStringLiteral("PSP/SYSTEM/ppsspp.ini")));
        CHECK(ini.open(QIODevice::WriteOnly));
        ini.write("[General]\nThemeName = Default\n");
        ini.close();
        CHECK(Home::AppFolders::Of(QStringLiteral("psp"), {}).saves.isEmpty());
        CHECK(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ini.write("[General]\nThemeName = PSP\n");
        ini.close();
        CHECK(Home::AppFolders::Of(QStringLiteral("psp"), {}).saves == stick);
        for (const auto& [name, before] : {std::pair{"XDG_CONFIG_HOME", config_before}, std::pair{"XDG_DATA_HOME", data_before}}) {
            before.isEmpty() ? qunsetenv(name) : qputenv(name, before);
        }
    }
#endif

    // The look: CSS values read as a browser reads them, and every theme in themes/ complete,
    // its values readable and its fonts there.
    {
        bool ok = false;
        CHECK(Home::ParseColour(QStringLiteral("rgba(148,144,136,.26)"), &ok) == QColor::fromRgbF(148 / 255.0, 144 / 255.0, 136 / 255.0, 0.26) && ok);
        CHECK(Home::ParseColour(QStringLiteral("#fff")) == QColor(255, 255, 255));
        CHECK(Home::ParseColour(QStringLiteral("#e0463a")) == QColor(0xe0, 0x46, 0x3a));
        (void)Home::ParseColour(QStringLiteral("not a colour"), &ok);
        CHECK(!ok);
        const Home::Fill bar = Home::ParseFill(QStringLiteral("linear-gradient(90deg,rgba(150,236,214,0),rgba(150,236,214,.95) 9%,rgba(172,242,224,0))"));
        CHECK(bar.layers.size() == 1 && bar.layers[0].kind == Home::Layer::Linear && bar.layers[0].angle == 90);
        CHECK(bar.layers.size() == 1 && bar.layers[0].stops.size() == 3 && std::abs(bar.layers[0].stops[1].place - 0.09) < 1e-9);
        const Home::Fill bg = Home::ParseFill(QStringLiteral(
            "repeating-conic-gradient(rgba(0,0,0,.035) 0 25%,rgba(0,0,0,0) 0 50%) 0 0/2px 2px,radial-gradient(ellipse at 40% 28%,#cdc9be 0%,#9f9a8e 100%)"));
        CHECK(bg.layers.size() == 2 && bg.layers[0].kind == Home::Layer::Conic && bg.layers[0].repeating && bg.layers[0].tile_w == 2);
        CHECK(bg.layers.size() == 2 && bg.layers[0].stops.size() == 4); // "colour 0 25%": two stops
        CHECK(bg.layers.size() == 2 && bg.layers[1].kind == Home::Layer::Radial && std::abs(bg.layers[1].at.y() - 0.28) < 1e-9);
        const QList<Home::Shadow> shadows = Home::ParseShadows(QStringLiteral("inset 0 2px 0 rgba(88,84,78,.6),0 14px 26px rgba(70,66,60,.22)"));
        CHECK(shadows.size() == 2 && shadows[0].inset && shadows[0].dy == 2 && !shadows[1].inset && shadows[1].blur == 26);
        const QList<Home::Theme> themes = Home::LoadThemes(QStringLiteral(HOME_SOURCE_DIR "/themes"));
        QStringList names;
        for (const Home::Theme& theme : themes) {
            names.append(theme.Key());
            const QStringList missing = theme.Missing();
            if (!missing.isEmpty()) {
                std::printf("theme %s: %s\n", qPrintable(theme.Key()), qPrintable(missing.join(QStringLiteral(", "))));
            }
            CHECK(missing.isEmpty());
            CHECK(!theme.FillOf(QStringLiteral("bg")).Empty());
            for (const QString& font : theme.Fonts()) {
                if (!QFile::exists(QStringLiteral(HOME_SOURCE_DIR "/fonts/") + font)) {
                    std::printf("theme %s: no font %s in fonts/\n", qPrintable(theme.Key()), qPrintable(font));
                    ++failures;
                }
            }
        }
        CHECK(names.contains(QStringLiteral("slate")) && names.contains(QStringLiteral("periwinkle")) && names.contains(QStringLiteral("silver")));
        CHECK(!themes.isEmpty() && themes.first().Key() == QStringLiteral("slate")); // the first, by "order"
    }

#ifndef READER_NO_SAVE_SYNC
    // A pairing link (docs/pairing.md), as server.py pair writes it, and its https form.
    {
        const QString link = QStringLiteral(
            "readersync://pair?v=1&sync=192.168.1.50:8642,box.example.ts.net:8642,%5Bfd7a%3A%3A1%5D:8642"
            "&code=ABCD-EFGH-JKLM&id=0123456789abcdef&name=My%20server&bridge=http://x:1&token=t");
        QString error;
        const std::optional<Home::Pairing> pairing = Home::ParsePairing(link, &error);
        CHECK(pairing.has_value());
        if (pairing) {
            CHECK(pairing->sync == (QStringList{QStringLiteral("192.168.1.50:8642"), QStringLiteral("box.example.ts.net:8642"),
                                                QStringLiteral("[fd7a::1]:8642")}));
            CHECK(pairing->code == QStringLiteral("ABCD-EFGH-JKLM"));
            CHECK(pairing->id == QStringLiteral("0123456789abcdef"));
            CHECK(pairing->name == QStringLiteral("My server"));
            CHECK(pairing->Addresses() == QStringLiteral("192.168.1.50:8642, box.example.ts.net:8642, [fd7a::1]:8642"));
        }
        CHECK(Home::LooksLikePairing(QStringLiteral("  READERSYNC://pair?v=1")));
        CHECK(!Home::LooksLikePairing(QStringLiteral("/games/a.iso")));
        const auto page = Home::ParsePairing(QStringLiteral("https://box.example.ts.net:8789/pair#v=1&sync=10.0.0.2&code=XY+Z"));
        CHECK(page && page->sync == QStringList{QStringLiteral("10.0.0.2")} && page->code == QStringLiteral("XY Z"));
        for (const char* bad : {"", "hello", "https://example.org/", "readersync://pair?v=2&sync=a&code=b",
                                "readersync://pair?v=1&code=ABCD", "readersync://pair?v=1&sync=a:1", "ftp://x/#v=1&sync=a&code=b"}) {
            if (Home::ParsePairing(QString::fromLatin1(bad))) {
                std::printf("FAILED: %s read as a pairing link\n", bad);
                ++failures;
            }
        }
        CHECK(!Home::ParsePairing(QStringLiteral("readersync://pair?v=7&sync=a&code=b"), &error) && error.contains(QStringLiteral("version 7")));
    }
#endif

    std::printf(failures == 0 ? "All tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
