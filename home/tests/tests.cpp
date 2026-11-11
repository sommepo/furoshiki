// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// What the reader apps' files amount to, with made-up files; the sounds' form; the one
// program's settings and their layers; the window's themes as their files say them; the game
// add-ons.

#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include "addons.h"
#include "app_icon.h"
#include "app_folders.h"
#include "crt_layers.h"
#include "library.h"
#include "hook_engine.h"
#include "hook_fetch.h"
#include "hook_processes.h"
#include "hook_runner.h"
#include "hook_scripts.h"
#include "line_feed.h"
#include "line_titles.h"
#include "ocr_client.h"
#include "ocr_helper.h"
#include "window_list.h"
#include <QImage>
#include <QUrl>
#include "ws_client.h"
#include "look.h"
#include "old_settings.h"
#ifndef READER_NO_SAVE_SYNC
#include "pairing.h"
#include "services.h"
#include "updater.h"
#endif
#include "play_settings.h"
#include "popup_controls.h"
#include "popup_design.h"
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
    {
        // Days read in a row: today need not be read yet, a day missed ends the run.
        const QList<Home::Day> days{{QStringLiteral("2026-11-05"), 900, 0}, {QStringLiteral("2026-11-07"), 10, 0},
                                    {QStringLiteral("2026-11-08"), 0, 0},   {QStringLiteral("2026-11-09"), 1200, 0},
                                    {QStringLiteral("2026-11-10"), 300, 0}};
        CHECK(Home::CharsOn(days, QStringLiteral("2026-11-09")) == 1200);
        CHECK(Home::CharsOn(days, QStringLiteral("2026-11-06")) == 0);
        CHECK(Home::Streak(days, QStringLiteral("2026-11-10")) == 2);
        CHECK(Home::Streak(days, QStringLiteral("2026-11-11")) == 2); // not read yet today
        CHECK(Home::Streak(days, QStringLiteral("2026-11-12")) == 0);
        CHECK(Home::Streak(days, QStringLiteral("2026-11-08")) == 1); // the 7th; the 8th read nothing
        CHECK(Home::Streak({}, QStringLiteral("2026-11-10")) == 0);
    }
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
        // A design over Yomitan dark (popup_design.h): its values and its order as the page's CSS.
        Home::PopupDesign::Design mine{QStringLiteral("yomitan-dark-mine"), QStringLiteral("yomitan-dark"),
                                       QJsonObject{{QStringLiteral("headword.size"), 31}, {QStringLiteral("glosses.colour"), QStringLiteral("#ff00ff")},
                                                   {QStringLiteral("popup.radius"), 99}, {QStringLiteral("no.such"), 1}},
                                       {QStringLiteral("senses"), QStringLiteral("head")}, {}};
        mine = Home::PopupDesign::FromJson(Home::PopupDesign::ToJson(mine)); // what is no setting of an element, or out of bounds, is left or brought in
        CHECK(!mine.values.contains(QStringLiteral("no.such")) && mine.values.value(QStringLiteral("popup.radius")).toDouble() == 28);
        CHECK(Home::PopupDesign::Save(folder, mine));
        const QString css = Home::PopupDesign::Css(mine);
        CHECK(css.contains(QStringLiteral(".headword { font-size: calc(31px * var(--scale)) !important; }")));
        CHECK(css.contains(QStringLiteral("color: #ff00ff !important")) && css.contains(QStringLiteral(".entry > .head { order: 2 !important; }")));
        CHECK(Home::PopupDesign::FreeName(folder, QStringLiteral("yomitan-dark")) == QStringLiteral("yomitan-dark-mine-2"));
        CHECK(Home::PopupDesign::Shown(mine.name, mine.base) == QStringLiteral("Yomitan dark · mine"));
        CHECK(Home::PopupDesign::InFolder(folder, QStringLiteral("paper")).isEmpty() && Home::PopupDesign::InFolder(folder, QStringLiteral("yomitan-dark")).size() == 1);
        const QList<Home::PlaySetting> popup = Home::PopupStyle::Settings(folder);
        CHECK(popup.size() == 9 && popup[0].Common() && popup[1].key == QStringLiteral("popup.design") && popup[1].choices.contains(mine.name));
        Home::PlaySettings settings(program.filePath(QStringLiteral("settings.json")));
        const auto layered = [&](const char* key, const char* console = "", const char* game = "") {
            return Home::PopupStyle::Layered(settings, folder, QString::fromLatin1(key), QString::fromLatin1(console),
                                             QString::fromLatin1(game));
        };
        CHECK(!layered("reader_popup_theme").has_value());
        settings.Set(QStringLiteral("popup.theme"), QStringLiteral("paper"));
        settings.Set(QStringLiteral("popup.theme"), QStringLiteral("yomitan-dark"), QStringLiteral("ps2"));
        settings.Set(QStringLiteral("popup.design"), mine.name, QStringLiteral("ps2"), QStringLiteral("SLPM-65550"));
        settings.Set(QStringLiteral("popup.design"), mine.name, QStringLiteral("ps1"));
        settings.Set(QStringLiteral("popup.mark_unknown"), QStringLiteral("0"), QStringLiteral("ps1"));
        CHECK(layered("reader_popup_theme", "ps1") == QStringLiteral("paper"));
        CHECK(layered("reader_popup_theme", "ps2") == QStringLiteral("yomitan-dark"));
        // Drawn over its own look only: on the PS1 the look is Paper, and nothing is drawn over it;
        // with no design the reader's CSS of before is not drawn either.
        CHECK(layered("reader_popup_css", "ps2", "SLPM-65550") == css);
        CHECK(layered("reader_popup_css", "ps1") == QString());
        CHECK(layered("reader_popup_css", "ps2") == QString());
        CHECK(layered("reader_popup_mark_unknown", "ps1") == QStringLiteral("false"));
        CHECK(!layered("reader_crt").has_value());
        // No core is handed the popup's.
        const Home::PlayConsole ps2{QStringLiteral("ps2"), QStringLiteral("PS2"), QStringLiteral("pcsx2-reader"), popup};
        CHECK(settings.ValuesFor(ps2, QStringLiteral("SLPM-65550")).isEmpty());
        // Out as one file (its design whole, its stylesheet as "css" for the Android apps), and in
        // again for another console, the design a file of its own.
        const QJsonObject style = Home::PopupStyle::Export(settings, folder, QStringLiteral("ps2"), QStringLiteral("SLPM-65550"),
                                                           [](const QString&) { return QStringLiteral("count"); });
        CHECK(style.value(QStringLiteral("theme")).toString() == QStringLiteral("yomitan-dark"));
        CHECK(style.value(QStringLiteral("css")).toString() == css && style.value(QStringLiteral("design")).toObject().value(QStringLiteral("base")).toString() == QStringLiteral("yomitan-dark"));
        CHECK(style.value(QStringLiteral("lookups")).toString() == QStringLiteral("count"));
        CHECK(Home::PopupStyle::Import(style, settings, folder, QStringLiteral("from ps2!"), QStringLiteral("psp"), {}).isEmpty());
        CHECK(layered("reader_popup_theme", "psp") == QStringLiteral("yomitan-dark"));
        CHECK(settings.Raw(QStringLiteral("popup.design"), QStringLiteral("psp")) == QStringLiteral("from-ps2-"));
        CHECK(layered("reader_popup_css", "psp") == Home::PopupDesign::Css(*Home::PopupDesign::Load(folder, QStringLiteral("from-ps2-"))));
        CHECK(!Home::PopupStyle::Import(QJsonObject{{QStringLiteral("theme"), QStringLiteral("paper")}}, settings, folder,
                                        QStringLiteral("x"), {}, {}).isEmpty());
        // The look's own settings (how solid, the corners, how many senses, a name's picture) are
        // layered and travel in the file as "look"; a number is said whole ("70").
        settings.Set(QStringLiteral("popup.opacity"), QStringLiteral("70"), QStringLiteral("ps2"));
        settings.Set(QStringLiteral("popup.portrait"), QStringLiteral("large"), QStringLiteral("ps2"));
        CHECK(layered("reader_popup_opacity", "ps2") == QStringLiteral("70"));
        CHECK(!layered("reader_popup_radius", "ps2").has_value());
        const QJsonObject look = Home::PopupStyle::Export(settings, folder, QStringLiteral("ps2"), {}, [](const QString&) {
                                     return QString();
                                 }).value(QStringLiteral("look")).toObject();
        CHECK(look.value(QStringLiteral("opacity")).toInt() == 70);
        CHECK(look.value(QStringLiteral("radius")).toInt() == -1);
        CHECK(look.value(QStringLiteral("portrait")).toString() == QStringLiteral("large"));
        CHECK(!look.value(QStringLiteral("hideTags")).toBool());
        const QJsonObject styled{{QStringLiteral("format"), 1},
                                 {QStringLiteral("kind"), QStringLiteral("reader-popup-style")},
                                 {QStringLiteral("theme"), QStringLiteral("glass")},
                                 {QStringLiteral("look"), QJsonObject{{QStringLiteral("opacity"), 55},
                                                                      {QStringLiteral("compact"), 3},
                                                                      {QStringLiteral("radius"), 99},
                                                                      {QStringLiteral("hideTags"), true}}}};
        CHECK(Home::PopupStyle::Import(styled, settings, folder, QStringLiteral("y"), QStringLiteral("3ds"), {}).isEmpty());
        CHECK(layered("reader_popup_theme", "3ds") == QStringLiteral("glass"));
        CHECK(layered("reader_popup_opacity", "3ds") == QStringLiteral("55"));
        CHECK(layered("reader_popup_compact", "3ds") == QStringLiteral("3"));
        CHECK(layered("reader_popup_radius", "3ds") == QStringLiteral("28"));
        CHECK(layered("reader_popup_hide_tags", "3ds") == QStringLiteral("true"));
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

    // The icon: every colour in icon/icon.json drawn, the default white on Kon's blue until one is chosen.
    {
        const QList<Home::IconColours>& choices = Home::IconChoices();
        CHECK(choices.size() == 37);
        CHECK(Home::ChosenIcon() == QStringLiteral("37"));
        CHECK(Home::IconLabel(QStringLiteral("30")) == QStringLiteral("30 Solarized"));
        for (const Home::IconColours& colours : choices) {
            const QImage image = Home::IconImage(colours.key, 64);
            CHECK(image.pixelColor(2, 2) == colours.ground);   // the corner, off the mark
            CHECK(image.pixelColor(40, 37) == colours.ink);    // inside ふ's long stroke
        }
    }

    // The window's first size: the Switch's picture, smaller only where the screen is.
    CHECK(Home::FirstWindowSize(QSize(2560, 1400)) == QSize(1920, 1080));
    CHECK(Home::FirstWindowSize(QSize(1920, 1080)) == QSize(1920, 1080));
    CHECK(Home::FirstWindowSize(QSize(1920, 1009)) == QSize(1793, 1009));
    CHECK(Home::FirstWindowSize(QSize(1280, 770)) == QSize(1280, 720));

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
        // The user's other services a link may name; what it does not know is left alone.
        const auto with = Home::ParsePairing(QStringLiteral(
            "readersync://pair?v=1&sync=10.0.0.2&code=X&tracker=http%3A//10.0.0.5%3A8788&furochibi=https://box.example.ts.net:8794/&bridge=x"));
        CHECK(with.has_value());
        CHECK(with->services.size() == 2);
        CHECK(with->services.value(QStringLiteral("tracker")) == QStringLiteral("http://10.0.0.5:8788"));
        CHECK(Home::Services::FromPairAnswer(R"({"v":1,"services":{"guides":"http://h:8100/","nonsense":"x","anki":""}})")
                  .value(QStringLiteral("guides")) == QStringLiteral("http://h:8100"));
        CHECK(Home::Services::FromPairAnswer("not json").isEmpty());
        // A build offered from the Files page's index: another file than this one, whole address.
        const QByteArray index = R"({"items":[{"id":"other","sha256":"x","url":"/o"},
            {"id":"prog","sha256":"AB12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12","url":"/downloads/data/p.bin",
             "version":"3","changes":["First","Second"]}]})";
        const Home::Updater::Offer offer = Home::Updater::Read(index, QStringLiteral("https://box:8789"), QStringLiteral("prog"), QStringLiteral("ffff"));
        CHECK(offer.url == QStringLiteral("https://box:8789/downloads/data/p.bin"));
        CHECK(offer.changes == QStringLiteral("First"));
        CHECK(Home::Updater::Read(index, QStringLiteral("https://box:8789"), QStringLiteral("prog"),
                                  QStringLiteral("ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12")).IsNone());
        CHECK(Home::Updater::Read(index, QStringLiteral("https://box:8789"), QStringLiteral("none"), QString()).IsNone());
        CHECK(Home::Updater::Read(index, QStringLiteral("https://box:8789"), QStringLiteral("other"), QString()).IsNone()); // no real hash
        // Which other device read a game last: this install's own days left out.
        const auto read = Home::Services::ReadElsewhere(R"({"devices":[
            {"id":"aaaa","device":"Z13","days":[{"day":"2026-11-10","game":"G1","chars":50}]},
            {"id":"bbbb","device":"Thor","days":[{"day":"2026-11-08","game":"G1","chars":900},{"day":"2026-11-09","game":"G2","chars":10},
                                                  {"day":"2026-11-10","game":"G3","chars":0}]},
            {"id":"cccc","app":"reader-home","days":[{"day":"2026-11-09","game":"G1","chars":5}]}]})", QStringLiteral("AAAA"));
        CHECK(read.size() == 2);
        CHECK(read.value(QStringLiteral("G1")).device == QStringLiteral("reader-home"));
        CHECK(read.value(QStringLiteral("G1")).day == QStringLiteral("2026-11-09"));
        CHECK(read.value(QStringLiteral("G2")).device == QStringLiteral("Thor"));
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

    // The folder of the program's former name becomes Furoshiki's, once, all of it.
    {
        QTemporaryDir data;
        const QByteArray was = qgetenv("XDG_DATA_HOME");
        const QByteArray dir = qgetenv("READER_HOME_DIR");
        qputenv("XDG_DATA_HOME", data.path().toUtf8());
        qunsetenv("READER_HOME_DIR");
        QDir().mkpath(data.filePath(QStringLiteral("reader-home/program")));
        Write(data.filePath(QStringLiteral("reader-home/program/settings.json")), "{}");
        CHECK(Home::SharedFolder() == data.filePath(QStringLiteral("reader-home")));
        CHECK(Home::MoveOldFolder());
        CHECK(Home::SharedFolder() == data.filePath(QStringLiteral("Furoshiki")));
        CHECK(QFile::exists(data.filePath(QStringLiteral("Furoshiki/program/settings.json"))));
        CHECK(!QFile::exists(data.filePath(QStringLiteral("reader-home"))));
        CHECK(!Home::MoveOldFolder());
        qputenv("XDG_DATA_HOME", was);
        if (!dir.isEmpty()) {
            qputenv("READER_HOME_DIR", dir);
        }
    }

    // The CRT picture in the layers: each layer keeps what differs from the ones under it, the
    // reader's own values (reader.ini) under all of them.
    {
        using namespace Home::CrtLayers;
        QTemporaryDir crt_folder;
        Home::PlaySettings play(crt_folder.filePath(QStringLiteral("settings.json")));
        const QString base = QStringLiteral("enabled=1.000;warp=0.200;frame=2.000");
        Values all = Resolved(play, base, {});
        CHECK(all.size() == Initial().size());
        CHECK(std::fabs(all.value(QStringLiteral("warp")) - 0.2) < 1e-6);
        CHECK(std::fabs(all.value(QStringLiteral("frame")) - 2) < 1e-6);
        CHECK(Target(play, QStringLiteral("ps1"), QStringLiteral("SLPS-00001")).console.isEmpty());
        // All consoles: the scan lines changed; only that is kept.
        all.insert(QStringLiteral("scanlines"), 0.3);
        Store(play, base, all, {});
        CHECK(play.Raw(Key) == QStringLiteral("scanlines=0.300"));
        // The PS1: no frame; it still follows all consoles' scan lines.
        Values ps1 = Resolved(play, base, QStringLiteral("ps1"));
        ps1.insert(QStringLiteral("frame"), 0);
        Store(play, base, ps1, QStringLiteral("ps1"));
        CHECK(play.Raw(Key, QStringLiteral("ps1")) == QStringLiteral("frame=0.000"));
        all.insert(QStringLiteral("scanlines"), 0.6);
        Store(play, base, all, {});
        CHECK(std::fabs(Resolved(play, base, QStringLiteral("ps1")).value(QStringLiteral("scanlines")) - 0.6) < 1e-6);
        CHECK(std::fabs(Resolved(play, base, QStringLiteral("dc")).value(QStringLiteral("frame")) - 2) < 1e-6);
        // One game: the CRT off; the console's other games keep theirs.
        Values game = Resolved(play, base, QStringLiteral("ps1"), QStringLiteral("SLPS-00001"));
        game.insert(QStringLiteral("enabled"), 0);
        Store(play, base, game, QStringLiteral("ps1"), QStringLiteral("SLPS-00001"));
        CHECK(Resolved(play, base, QStringLiteral("ps1"), QStringLiteral("SLPS-00001")).value(QStringLiteral("enabled")) == 0);
        CHECK(Resolved(play, base, QStringLiteral("ps1"), QStringLiteral("SLPS-00002")).value(QStringLiteral("enabled")) == 1);
        CHECK(Target(play, QStringLiteral("ps1"), QStringLiteral("SLPS-00001")).game == QStringLiteral("SLPS-00001"));
        CHECK(Target(play, QStringLiteral("ps1"), QStringLiteral("SLPS-00002")).console == QStringLiteral("ps1"));
        CHECK(Target(play, QStringLiteral("ps1"), QStringLiteral("SLPS-00002")).game.isEmpty());
        // Back to what is under it: the layer's own is gone.
        Store(play, base, Resolved(play, base, QStringLiteral("ps1")), QStringLiteral("ps1"), QStringLiteral("SLPS-00001"));
        CHECK(!play.IsSet(Key, QStringLiteral("ps1"), QStringLiteral("SLPS-00001")));
        CHECK(Parse(QStringLiteral("warp=0.5;nonsense=3;glow=x;;frame")) == (Values{{QStringLiteral("warp"), 0.5}}));
        CHECK(play.Save());
        Home::PlaySettings again(crt_folder.filePath(QStringLiteral("settings.json")));
        CHECK(again.Raw(Key, QStringLiteral("ps1")) == QStringLiteral("frame=0.000"));
    }

    // The game add-ons: found in the shared folder's addons and beside the program's folder,
    // their scripts copied in (a user's own kept once), their settings put in once.
    {
        QTemporaryDir root;
        const QString shared = root.filePath(QStringLiteral("shared"));
        const QString program = root.filePath(QStringLiteral("Readers/ReaderHome"));
        QDir().mkpath(program);
        QDir().mkpath(shared + QStringLiteral("/addons/Kakuremino/profile"));
        Write(shared + QStringLiteral("/addons/Kakuremino/addon.json"),
              R"({"format":1,"name":"Kakuremino","console":"ps2","games":["SLPM-67002"],"scripts":"profile",
                  "settings":{"picture.aspect":"stretch"},
                  "companion":{"windows":["python/python.exe","-I","s.py"],"linux":["python3","-I","s.py"],
                               "env":{"KAKUREMINO_SOCKET":"{control}"},"url":"http://127.0.0.1:8813/"}})");
        Write(shared + QStringLiteral("/addons/Kakuremino/profile/SLPM-67002.lua"), "-- the add-on's\n");
        Write(shared + QStringLiteral("/addons/Kakuremino/profile/SLPM-67002.charset"), "table");
        QDir().mkpath(root.filePath(QStringLiteral("Readers/Karakuri")));
        Write(root.filePath(QStringLiteral("Readers/Karakuri/addon.json")),
              R"({"format":1,"name":"Karakuri","console":"dc","games":["HDR-0164"]})");
        QDir().mkpath(root.filePath(QStringLiteral("Readers/Other")));  // no addon.json: not one
        QDir().mkpath(root.filePath(QStringLiteral("Readers/Broken")));
        Write(root.filePath(QStringLiteral("Readers/Broken/addon.json")), "{ not json");
        QStringList problems;
        const QList<Home::Addon> found = Home::FindAddons(Home::AddonFolders(shared, program), &problems);
        CHECK(found.size() == 2);
        CHECK(problems.size() == 1);
        const Home::Addon* kakuremino = found.isEmpty() ? nullptr : &found[0];
        CHECK(kakuremino != nullptr && kakuremino->name == QStringLiteral("Kakuremino"));
        if (kakuremino != nullptr) {
            CHECK(kakuremino->For(QStringLiteral("ps2"), QStringLiteral("SLPM-67002")));
            CHECK(!kakuremino->For(QStringLiteral("ps1"), QStringLiteral("SLPM-67002")));
            CHECK(!kakuremino->For(QStringLiteral("ps2"), QString()));
            CHECK(kakuremino->companion.size() == 3);
            CHECK(kakuremino->url == QStringLiteral("http://127.0.0.1:8813/"));
            const QString profiles = root.filePath(QStringLiteral("profiles"));
            const QString state = root.filePath(QStringLiteral("program/addons.json"));
            QDir().mkpath(profiles);
            Write(profiles + QStringLiteral("/SLPM-67002.lua"), "-- the user's own\n");
            CHECK(Home::InstallScripts(*kakuremino, profiles, state).size() == 2);
            QFile copied(profiles + QStringLiteral("/SLPM-67002.lua"));
            CHECK(copied.open(QIODevice::ReadOnly) && copied.readAll() == "-- the add-on's\n");
            QFile kept(profiles + QStringLiteral("/SLPM-67002.lua.before-addon"));
            CHECK(kept.open(QIODevice::ReadOnly) && kept.readAll() == "-- the user's own\n");
            CHECK(Home::InstallScripts(*kakuremino, profiles, state).isEmpty());  // the same: nothing
            Home::PlaySettings play(root.filePath(QStringLiteral("program/settings.json")));
            CHECK(Home::ApplySettings(*kakuremino, play, QStringLiteral("ps2"), QStringLiteral("SLPM-67002"), state));
            CHECK(play.Raw(QStringLiteral("picture.aspect"), QStringLiteral("ps2"), QStringLiteral("SLPM-67002")) ==
                  QStringLiteral("stretch"));
            // The user takes it out: it is not put back.
            play.Clear(QStringLiteral("picture.aspect"), QStringLiteral("ps2"), QStringLiteral("SLPM-67002"));
            CHECK(!Home::ApplySettings(*kakuremino, play, QStringLiteral("ps2"), QStringLiteral("SLPM-67002"), state));
            CHECK(!play.IsSet(QStringLiteral("picture.aspect"), QStringLiteral("ps2"), QStringLiteral("SLPM-67002")));
            CHECK(!Home::ApplySettings(*kakuremino, play, QStringLiteral("ps2"), QStringLiteral("SLPS-00001"), state));
        }
        const Home::CompanionChannel channel = Home::MakeCompanionChannel(root.filePath(QStringLiteral("program")));
        CHECK(!channel.listen.isEmpty() && !channel.address.isEmpty());
    }

    // Lines from a source that is no core (docs/furoshiki-3.md, "Sources"). What makes text a
    // line: Furochibi's cleaner's rules (its test_compare), as far as they hold for text sent.
    {
        namespace Lines = Home::Lines;
        using Lines::Verdict;
        const auto Q = [](const char8_t* text) { return QString::fromUtf8(reinterpret_cast<const char*>(text)); };
        CHECK(Lines::Compare(QString(), Q(u8"こんにちは")) == Verdict::New);
        CHECK(Lines::Compare(Q(u8"こんにちは"), Q(u8"こんにちは")) == Verdict::Same);
        CHECK(Lines::Compare(Q(u8"こんにちは"), Q(u8"こんにちは。")) == Verdict::Same);
        CHECK(Lines::Compare(Q(u8"この先の森"), Q(u8"この先の森には近づくな")) == Verdict::CarriesOn);
        CHECK(Lines::Compare(Q(u8"この先の森には近づくな"), Q(u8"この先の森")) == Verdict::PartOf);
        CHECK(Lines::Compare(Q(u8"はい"), Q(u8"いいえ")) == Verdict::New);
        CHECK(Lines::Compare(Q(u8"はい"), Q(u8"▼")) == Verdict::Junk);
        CHECK(Lines::Compare(Q(u8"はい"), QStringLiteral("Yes")) == Verdict::Junk);
        CHECK(Lines::Compare(Q(u8"はい"), QString()) == Verdict::Junk);
        // Sent text is exact: a line one character from the last is another line (OCR's rule is not used).
        CHECK(Lines::Compare(Q(u8"青ルピーを手に入れた"), Q(u8"緑ルピーを手に入れた")) == Verdict::New);
        CHECK(Lines::Compare(Q(u8"ありがとうございます"), Q(u8"本当にありがとうございます")) == Verdict::New);
        // Layout's spaces and line breaks go; a space between Latin words stays.
        CHECK(Lines::Normalise(Q(u8"  この先の\n森には　近づくな ")) == Q(u8"この先の森には近づくな"));
        CHECK(Lines::Normalise(Q(u8"Steins Gate の 世界")) == Q(u8"Steins Gateの世界"));
        CHECK(Lines::IsJunk(QStringLiteral("123 abc")) && !Lines::IsJunk(Q(u8"ｱ")) && !Lines::IsJunk(Q(u8"𠮟る")));
        CHECK(Lines::Key(Q(u8"「はい、行こう！」")) == Q(u8"はい行こう"));
        // A texthooker's messages: the text itself, or a JSON object's "sentence" or "text".
        CHECK(Lines::MessageText(Q(u8"ただの行")) == Q(u8"ただの行"));
        CHECK(Lines::MessageText(Q(u8"{\"sentence\":\"文です\",\"process_path\":\"C:/game.exe\"}")) == Q(u8"文です"));
        CHECK(Lines::MessageText(Q(u8" {\"text\":\"行です\"}")) == Q(u8"行です"));
        CHECK(Lines::MessageText(QStringLiteral("{\"type\":\"status\"}")).isEmpty());
        CHECK(Lines::MessageText(Q(u8"{かっこで始まる行")) == Q(u8"{かっこで始まる行"));
        CHECK(Lines::GameId(QStringLiteral("anything"), QStringLiteral("steins-gate")) == QStringLiteral("LINES-STEINS-GATE"));
        // A hooked line laid over what OCR saw: each of its characters given the box of the same
        // one there, none for one OCR missed or misread; full-width and half-width alike.
        {
            QList<QRectF> seen_boxes;
            for (int i = 0; i < 6; ++i) {
                seen_boxes.append(QRectF(0.1 * i, 0.8, 0.09, 0.1));
            }
            int matched = 0;
            const QList<QRectF> placed = Lines::PlaceAlong(QStringLiteral("「雨だ」ＡＢ"), QStringLiteral("雨た」AB!"), seen_boxes, &matched);
            CHECK(placed.size() == 6 && matched == 4);
            CHECK(placed[0].isEmpty() && placed[1] == seen_boxes[0] && placed[2].isEmpty() && placed[3] == seen_boxes[2] && placed[4] == seen_boxes[3] && placed[5] == seen_boxes[4]);
        }
        CHECK(Lines::GameId(Q(u8"ある物語"), QString()) == Lines::GameId(Q(u8" ある物語 "), QString()));
        CHECK(Lines::GameId(Q(u8"ある物語"), QString()).size() == 14);
        CHECK(Lines::SourceId(Q(u8"はい、行こう")) == Lines::SourceId(Q(u8"はい行こう。")));

        // In order: a line waits a moment for more of itself, and is then a line.
        Lines::Feed feed;
        CHECK(feed.Take(Q(u8"この先"), 1000).isEmpty() && feed.HasPending());
        CHECK(feed.Take(Q(u8"この先の森には"), 1040).isEmpty());
        CHECK(feed.Take(Q(u8"この先の森には近づくな"), 1080).isEmpty());
        auto out = feed.Settle(1230);
        CHECK(out && out->text == Q(u8"この先の森には近づくな") && !out->takes_back);
        CHECK(!feed.HasPending() && !feed.Settle(1300));
        // The same again, and its beginning again, are dropped; English and marks too.
        CHECK(feed.Take(Q(u8"この先の森には近づくな。"), 1500).isEmpty() && !feed.HasPending());
        CHECK(feed.Take(Q(u8"この先の森"), 1600).isEmpty() && !feed.HasPending());
        CHECK(feed.Take(QStringLiteral("Loading..."), 1700).isEmpty() && !feed.HasPending());
        // More of a line already handed over: the whole takes the part back.
        CHECK(feed.Take(Q(u8"この先の森には近づくな、と彼は言った"), 2000).isEmpty() && feed.HasPending());
        out = feed.Settle(2150);
        CHECK(out && out->takes_back && *out->takes_back == Q(u8"この先の森には近づくな"));
        // Another line while one waits: the one that waited is handed over at once.
        CHECK(feed.Take(Q(u8"はい、行こう"), 3000).isEmpty());
        const QList<Lines::Feed::Out> at_once = feed.Take(Q(u8"いいえ"), 3050);
        CHECK(at_once.size() == 1 && at_once[0].text == Q(u8"はい、行こう"));
        out = feed.Settle(3200);
        CHECK(out && out->text == Q(u8"いいえ"));
        // Long after, a line's beginning is a line of its own (はい after はい、行こう).
        CHECK(feed.Take(Q(u8"はい、行こう"), 4000).isEmpty());
        CHECK(feed.Settle(4150).has_value());
        CHECK(feed.Take(Q(u8"はい"), 4150 + Lines::Feed::CarryMs + 1).isEmpty() && feed.HasPending());
        out = feed.Settle(20000);
        CHECK(out && out->text == Q(u8"はい") && !out->takes_back);

        // The titles read from lines, and their file among the apps'.
        QTemporaryDir root;
        Lines::Sources sources = Lines::Load(root.path());
        CHECK(sources.address == Lines::DefaultAddress && sources.titles.isEmpty());
        const QString first = Lines::Add(sources, Q(u8" ある物語 "), QStringLiteral("websocket"), QString());
        CHECK(first.startsWith(QStringLiteral("LINES-")) && sources.titles.size() == 1 && sources.titles[0].title == Q(u8"ある物語"));
        CHECK(Lines::Add(sources, QStringLiteral("STEINS;GATE"), QStringLiteral("clipboard"), QStringLiteral("steins-gate")) ==
              QStringLiteral("LINES-STEINS-GATE"));
        CHECK(Lines::Add(sources, Q(u8"ある物語"), QStringLiteral("clipboard"), QString()) == first && sources.titles.size() == 2);
        CHECK(sources.titles[0].by == QStringLiteral("clipboard")); // the same title, read another way
        CHECK(Lines::Add(sources, QStringLiteral("  "), QStringLiteral("websocket"), QString()).isEmpty());
        CHECK(Lines::Add(sources, QStringLiteral("X"), QStringLiteral("telepathy"), QString()).isEmpty());
        sources.address = QStringLiteral("ws://127.0.0.1:9001");
        CHECK(Lines::Save(root.path(), sources));
        const Lines::Sources again = Lines::Load(root.path());
        CHECK(again.address == sources.address && again.titles.size() == 2 && again.titles[1].id == QStringLiteral("LINES-STEINS-GATE"));
        CHECK(Lines::WayOf(Lines::PathOf(again.titles[1])) == QStringLiteral("CLIPBOARD"));
        CHECK(Lines::WayOf(QStringLiteral("/games/one.chd")).isEmpty());
        const QJsonObject read{{QStringLiteral("days"), QJsonArray{QJsonObject{{QStringLiteral("day"), QStringLiteral("2026-11-10")},
                                                                               {QStringLiteral("game"), first},
                                                                               {QStringLiteral("chars"), 40},
                                                                               {QStringLiteral("active_ms"), 9000}}}}};
        CHECK(Lines::WriteApp(root.path(), again, QStringLiteral("/nowhere/reader-home"), &read));
        CHECK(Lines::WriteApp(root.path(), again, QStringLiteral("/nowhere/reader-home"), nullptr)); // the figures stay
        const Home::Library shelf = Home::Load(root.path());
        CHECK(shelf.apps.size() == 1 && shelf.apps[0].system == Lines::System && shelf.games.size() == 2);
        for (const Home::Game& game : shelf.games) {
            CHECK(game.app == Lines::AppKey && !Lines::WayOf(game.path).isEmpty());
            CHECK(game.id != first || (game.chars == 40 && game.last_day == QStringLiteral("2026-11-10")));
        }
        CHECK(Home::ShortSystem(shelf, Lines::System) == QStringLiteral("PC"));
    }

    // A window read by OCR (docs/furoshiki-3.md, 8): its title's window and area kept, the
    // helper's settings, the stand-in window list, and the helper's client against a stand-in
    // helper made here on a loopback port of the system's choosing.
    {
        namespace Lines = Home::Lines;
        const auto Q = [](const char8_t* text) { return QString::fromUtf8(reinterpret_cast<const char*>(text)); };
        const auto until = [](const std::function<bool()>& done, int within_ms = 4000) {
            QElapsedTimer timer;
            timer.start();
            while (!done() && timer.elapsed() < within_ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            return done();
        };
        CHECK(Lines::CleanArea(QRectF(0.1, 0.7, 0.8, 0.25)) == QRectF(0.1, 0.7, 0.8, 0.25));
        CHECK(Lines::CleanArea(QRectF(0.9, 0.9, 0.5, 0.5)) == QRectF(0.9, 0.9, 0.1, 0.1)); // kept within the picture
        CHECK(Lines::CleanArea(QRectF(0.5, 0.5, -0.2, -0.2)) == QRectF(0.3, 0.3, 0.2, 0.2)); // drawn from its other corner
        CHECK(!Lines::CleanArea(QRectF(0.5, 0.5, 0.001, 0.4)).isValid());
        CHECK(Lines::CleanHelperAddress(QStringLiteral(" http://127.0.0.1:8794/ ")) == QStringLiteral("http://127.0.0.1:8794"));
        CHECK(Lines::CleanHelperAddress(QStringLiteral("https://helper.example:8794")) == QStringLiteral("https://helper.example:8794"));
        CHECK(Lines::CleanHelperAddress(QStringLiteral("ws://127.0.0.1:6677")).isEmpty());
        CHECK(Lines::CleanHelperAddress(QStringLiteral("127.0.0.1:8794")).isEmpty());

        QTemporaryDir root;
        Lines::Sources sources = Lines::Load(root.path());
        CHECK(sources.ocr.address.isEmpty() && sources.ocr.source == QStringLiteral("furoshiki")); // no address is built in
        const QString id = Lines::AddWindow(sources, Q(u8"森の物語"), QStringLiteral("Mori - Game Window"), QRectF(0.08, 0.7, 0.84, 0.25), QString());
        CHECK(id.startsWith(QStringLiteral("LINES-")) && sources.titles.size() == 1 && sources.titles[0].by == QStringLiteral("ocr"));
        CHECK(Lines::AddWindow(sources, QStringLiteral("STEINS;GATE"), QStringLiteral("STEINS;GATE"), QRectF(0, 0.6, 1, 0.4), QStringLiteral("steins-gate")) ==
              QStringLiteral("LINES-STEINS-GATE"));
        CHECK(Lines::AddWindow(sources, QStringLiteral("X"), QStringLiteral("X"), QRectF(0, 0, 0.001, 0.001), QString()).isEmpty() && sources.titles.size() == 2);
        // The same title again: another window and area, not a second title.
        CHECK(Lines::AddWindow(sources, Q(u8"森の物語"), QStringLiteral("Mori"), QRectF(0.1, 0.7, 0.8, 0.25), QString()) == id && sources.titles.size() == 2);
        sources.ocr = {QStringLiteral("http://127.0.0.1:18794"), QStringLiteral("z13"), QStringLiteral("a-token-of-the-helper")};
        CHECK(Lines::Save(root.path(), sources));
        const Lines::Sources again = Lines::Load(root.path());
        CHECK(again.titles.size() == 2 && again.titles[0].window == QStringLiteral("Mori") && again.titles[0].area == QRectF(0.1, 0.7, 0.8, 0.25));
        CHECK(again.ocr.address == sources.ocr.address && again.ocr.source == QStringLiteral("z13") && again.ocr.token == sources.ocr.token);
        CHECK(Lines::PathOf(again.titles[0]) == QStringLiteral("lines://ocr/") + id && Lines::WayOf(Lines::PathOf(again.titles[0])) == QStringLiteral("OCR"));
        Lines::Sources fewer = again;
        CHECK(Lines::Remove(fewer, id) && fewer.titles.size() == 1 && !Lines::Remove(fewer, id));

        // The stand-in window list: picture files named by the environment, read anew each time.
        const QString picture_file = root.filePath(QStringLiteral("game.png"));
        QImage game(64, 36, QImage::Format_RGB32);
        game.fill(Qt::darkBlue);
        CHECK(game.save(picture_file));
        qputenv("READER_HOME_FAKE_WINDOWS", (QStringLiteral("A Game=") + picture_file + QDir::listSeparator() + root.filePath(QStringLiteral("other.png"))).toUtf8());
        const QList<Home::Windows::Entry> windows = Home::Windows::List();
        CHECK(windows.size() >= 2 && windows[windows.size() - 2].title == QStringLiteral("A Game") && windows.last().title == QStringLiteral("other"));
        const Home::Windows::Entry found = Home::Windows::Find(QStringLiteral("A Game"));
        CHECK(found.title == QStringLiteral("A Game") && Home::Windows::Grab(found).size() == QSize(64, 36));
        CHECK(Home::Windows::Find(QStringLiteral("No Such Window")).title.isEmpty());
        CHECK(Home::Windows::Grab(Home::Windows::Find(QStringLiteral("other"))).isNull()); // the file is not there
        game = QImage(80, 45, QImage::Format_RGB32);
        game.fill(Qt::darkGreen);
        CHECK(game.save(picture_file) && Home::Windows::Grab(found).size() == QSize(80, 45));
        CHECK(Home::Windows::Place(found).isEmpty());
        qunsetenv("READER_HOME_FAKE_WINDOWS");

        // The games document: this title made the game played, with its area; the others as they were.
        const QJsonObject games = QJsonDocument::fromJson(R"({"active": "g1", "games": [
            {"id": "g1", "name": "Other", "page": "", "source": "batocera", "areas": [{"id": "a1", "name": "Dialogue", "rect": [0, 0, 1, 1]}], "active_area": "a1"},
            {"id": "g2", "name": "Mori", "page": "p", "areas": [{"id": "m1", "name": "Menu", "rect": [0, 0, 0.5, 0.5]},
                                                               {"id": "furoshiki", "name": "Furoshiki", "rect": [0, 0, 0.1, 0.1]}], "active_area": "m1"}]})").object();
        QJsonObject with = Home::OcrClient::WithGame(games, QStringLiteral("Mori"), QRectF(0.1, 0.7, 0.8, 0.25), QStringLiteral("furoshiki"));
        const QJsonObject mori = with.value(QStringLiteral("games")).toArray()[1].toObject();
        CHECK(with.value(QStringLiteral("active")).toString() == QStringLiteral("g2") && with.value(QStringLiteral("games")).toArray().size() == 2);
        CHECK(with.value(QStringLiteral("games")).toArray()[0].toObject() == games.value(QStringLiteral("games")).toArray()[0].toObject());
        CHECK(mori.value(QStringLiteral("areas")).toArray().size() == 2 && mori.value(QStringLiteral("active_area")).toString() == QStringLiteral("furoshiki"));
        CHECK(mori.value(QStringLiteral("areas")).toArray()[1].toObject().value(QStringLiteral("rect")).toArray() == QJsonArray({0.1, 0.7, 0.8, 0.25}));
        CHECK(mori.value(QStringLiteral("page")).toString() == QStringLiteral("p") && mori.value(QStringLiteral("source")).toString() == QStringLiteral("furoshiki"));
        with = Home::OcrClient::WithGame(games, Q(u8"新しい"), QRectF(0, 0.5, 1, 0.5), QStringLiteral("furoshiki"));
        const QJsonArray three = with.value(QStringLiteral("games")).toArray();
        CHECK(three.size() == 3 && three[2].toObject().value(QStringLiteral("name")).toString() == Q(u8"新しい"));
        CHECK(with.value(QStringLiteral("active")).toString() == three[2].toObject().value(QStringLiteral("id")).toString());
        CHECK(Home::OcrClient::WithGame(games, Q(u8"新しい"), QRectF(0, 0.5, 1, 0.5), QStringLiteral("furoshiki")) == with); // the same ID next time
        const Home::OcrClient::Read read = Home::OcrClient::ReadOf(
            QJsonDocument::fromJson(Q(u8R"({"id": 12, "text": "森へ", "boxes": [[0.1, 0.8, 0.02, 0.05], null, [0.14, 0.8, 0.02, 0.05]]})").toUtf8()).object());
        CHECK(read.id == 12 && read.text == Q(u8"森へ") && read.boxes.size() == 3 && read.boxes[1].isEmpty() && read.boxes[2].x() == 0.14);

        // A stand-in helper: enough of Furochibi's API for the client, what was asked kept.
        struct Asked {
            QString verb, path, token;
            QByteArray body;
        };
        QList<Asked> asked;
        QTcpServer helper;
        CHECK(helper.listen(QHostAddress::LocalHost, 0));
        QList<QTcpSocket*> held; // long polls with nothing to say are left open
        const QByteArray token = "stand-in-token-0123456789";
        QObject::connect(&helper, &QTcpServer::newConnection, [&] {
            while (QTcpSocket* socket = helper.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, [&, socket, buffer] {
                    buffer->append(socket->readAll());
                    const qsizetype head = buffer->indexOf("\r\n\r\n");
                    if (head < 0) {
                        return;
                    }
                    qsizetype length = 0;
                    Asked one;
                    const QList<QByteArray> lines = buffer->left(head).split('\n');
                    one.verb = QString::fromLatin1(lines[0].split(' ').value(0));
                    one.path = QString::fromLatin1(lines[0].split(' ').value(1));
                    for (const QByteArray& line : lines.mid(1)) {
                        const QByteArray name = line.left(line.indexOf(':')).trimmed().toLower(), value = line.mid(line.indexOf(':') + 1).trimmed();
                        if (name == "content-length") {
                            length = value.toLongLong();
                        } else if (name == "authorization") {
                            one.token = QString::fromLatin1(value.mid(7));
                        }
                    }
                    if (buffer->size() < head + 4 + length) {
                        return;
                    }
                    one.body = buffer->mid(head + 4, length);
                    buffer->clear();
                    asked.append(one);
                    int status = 200;
                    QByteArray answer = "{}";
                    const QString path = one.path;
                    if (path == QStringLiteral("/api/source") && one.verb == QStringLiteral("GET")) {
                        answer = R"({"source": "batocera", "sources": [{"id": "batocera", "name": "Box", "push": false},
                                    {"id": "furoshiki", "name": "Furoshiki: a window", "push": true}]})";
                    } else if (path == QStringLiteral("/api/regions") && one.verb == QStringLiteral("GET")) {
                        answer = R"({"active": "g1", "games": [{"id": "g1", "name": "Other", "areas": [], "active_area": null}]})";
                    } else if (path.startsWith(QStringLiteral("/api/feed/"))) {
                        if (one.token.toLatin1() != token) {
                            status = 403;
                            answer = "that is not this picture source's token";
                        } else {
                            answer = one.body.startsWith("\xff\xd8") && one.body.endsWith("\xff\xd9") ? R"({"taken": true, "in_use": true})" : R"({"error": "a frame is one JPEG picture"})";
                        }
                    } else if (path == QStringLiteral("/api/ocr/test")) {
                        answer = Q(u8R"({"text": "試しに読んだ行", "engine": "meikiocr", "ms": 40, "raw": []})").toUtf8();
                    } else if (path.startsWith(QStringLiteral("/api/lines?since=0&limit=1"))) {
                        // What was read before this reading began is not handed over.
                        answer = Q(u8R"({"lines": [{"id": 7, "text": "前の行", "source": "furoshiki", "boxes": []}], "replaced": [], "upto": 7})").toUtf8();
                    } else if (path.startsWith(QStringLiteral("/api/lines?since=7&"))) {
                        answer = Q(u8R"({"lines": [{"id": 9, "text": "この先の", "source": "furoshiki", "boxes": [[0.1, 0.8, 0.02, 0.05]]}], "replaced": [], "upto": 9})").toUtf8();
                    } else if (path.startsWith(QStringLiteral("/api/lines?since=9&"))) {
                        // Line 8 was another source's: passed over there, `upto` goes past it.
                        answer = Q(u8R"({"lines": [{"id": 11, "text": "この先の森には近づくな", "source": "furoshiki", "boxes": []}], "replaced": [9], "upto": 11})").toUtf8();
                    } else if (path.startsWith(QStringLiteral("/api/lines?since=11&"))) {
                        held.append(socket);
                        return;
                    } else if (path == QStringLiteral("/api/overlay?source=furoshiki")) {
                        answer = R"({"seq": 4, "source": "furoshiki", "line": {"id": 11, "source": "furoshiki"}})";
                    } else if (path.startsWith(QStringLiteral("/api/overlay?source=furoshiki&after=4"))) {
                        answer = R"({"seq": 5, "source": "furoshiki", "line": null})";
                    } else if (path.startsWith(QStringLiteral("/api/overlay"))) {
                        held.append(socket);
                        return;
                    }
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Forbidden") +
                                  "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(answer.size()) +
                                  "\r\n\r\n" + answer);
                    socket->disconnectFromHost();
                });
            }
        });
        const QString address = QStringLiteral("http://127.0.0.1:%1").arg(helper.serverPort());
        Home::OcrClient client;
        CHECK(!client.Configured() && !client.Push(game));
        QList<QPair<bool, QString>> answered, chosen, tested;
        QList<Home::OcrClient::Read> lines;
        QList<QList<qint64>> replaced;
        QList<qint64> on_screen;
        QObject::connect(&client, &Home::OcrClient::Answered, [&](bool ok, const QString& what) { answered.append({ok, what}); });
        QObject::connect(&client, &Home::OcrClient::Chosen, [&](bool ok, const QString& what) { chosen.append({ok, what}); });
        QObject::connect(&client, &Home::OcrClient::Tested, [&](bool ok, const QString& what) { tested.append({ok, what}); });
        QObject::connect(&client, &Home::OcrClient::Line, [&](const Home::OcrClient::Read& line, const QList<qint64>& gone) {
            lines.append(line);
            replaced.append(gone);
        });
        QObject::connect(&client, &Home::OcrClient::OnScreen, [&](qint64 line) { on_screen.append(line); });
        // A source the helper does not have, then the one it has.
        client.Set({address, QStringLiteral("nothing"), QString::fromLatin1(token)});
        client.Ask();
        CHECK(until([&] { return answered.size() == 1; }) && !answered[0].first && answered[0].second.contains(QStringLiteral("nothing")));
        client.Set({address + QLatin1Char('/'), QStringLiteral("furoshiki"), QString::fromLatin1(token)});
        client.Ask();
        CHECK(until([&] { return answered.size() == 2; }) && answered[1].first && answered[1].second == QStringLiteral("Furoshiki: a window"));
        // Its source chosen there, the title made the game played with its area.
        asked.clear();
        client.Choose(QStringLiteral("Mori"), QRectF(0.1, 0.7, 0.8, 0.25));
        CHECK(until([&] { return chosen.size() == 1; }) && chosen[0].first && asked.size() == 3);
        if (asked.size() == 3) {
            CHECK(asked[0].verb == QStringLiteral("PUT") && asked[0].path == QStringLiteral("/api/source") &&
                  QJsonDocument::fromJson(asked[0].body).object().value(QStringLiteral("source")).toString() == QStringLiteral("furoshiki"));
            const QJsonObject put = QJsonDocument::fromJson(asked[2].body).object();
            CHECK(asked[2].verb == QStringLiteral("PUT") && asked[2].path == QStringLiteral("/api/regions") && put.value(QStringLiteral("games")).toArray().size() == 2);
            CHECK(put.value(QStringLiteral("active")).toString() == put.value(QStringLiteral("games")).toArray()[1].toObject().value(QStringLiteral("id")).toString());
        }
        // A picture goes as a JPEG with the token; the next waits until that one is answered.
        asked.clear();
        CHECK(client.Push(game) && !client.Push(game));
        CHECK(until([&] { return client.Taken() == 1; }) && client.Sent() == 1 && asked.size() == 1);
        CHECK(!asked.isEmpty() && asked[0].path == QStringLiteral("/api/feed/furoshiki") && asked[0].token.toLatin1() == token && asked[0].body.startsWith("\xff\xd8"));
        CHECK(client.Status().startsWith(QStringLiteral("reading")));
        client.Test(QRectF(0.1, 0.7, 0.8, 0.25));
        CHECK(until([&] { return tested.size() == 1; }) && tested[0].first && tested[0].second == Q(u8"試しに読んだ行"));
        CHECK(QJsonDocument::fromJson(asked.last().body).object().value(QStringLiteral("source")).toString() == QStringLiteral("furoshiki"));
        // Its lines from now on, in order, with what each replaced; and which is on the screen.
        client.Follow();
        CHECK(until([&] { return lines.size() == 2 && on_screen.size() == 2; }));
        if (lines.size() == 2 && on_screen.size() == 2) {
            CHECK(lines[0].id == 9 && lines[0].text == Q(u8"この先の") && lines[0].boxes.size() == 1 && replaced[0].isEmpty());
            CHECK(lines[1].id == 11 && lines[1].text == Q(u8"この先の森には近づくな") && replaced[1] == QList<qint64>{9});
            CHECK(on_screen[0] == 11 && on_screen[1] == -1);
        }
        client.Unfollow();
        // A wrong token is refused, and said.
        client.Set({address, QStringLiteral("furoshiki"), QStringLiteral("another-token-0123456789")});
        CHECK(client.Push(game));
        CHECK(until([&] { return client.Status().startsWith(QStringLiteral("refused")); }) && client.Taken() == 0);
        // No helper there: said, not waited for.
        helper.close();
        client.Set({address, QStringLiteral("furoshiki"), QString::fromLatin1(token)});
        client.Ask();
        CHECK(until([&] { return answered.size() == 3; }) && !answered[2].first && client.Status().startsWith(QStringLiteral("no answer")));
        qDeleteAll(held);
    }

    // A helper installed on this computer (ocr_helper.h): its folder's file read, found beside
    // the program, and, with a stand-in program (Python answering as the helper's health and
    // sources do, ending when its standard input ends), started when nothing answers, used by
    // the client without an address typed, and stopped.
    {
        const auto until = [](const std::function<bool()>& done, int within_ms = 15000) {
            QElapsedTimer timer;
            timer.start();
            while (!done() && timer.elapsed() < within_ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            return done();
        };
        QTemporaryDir root;
        const QString folder = root.path() + QStringLiteral("/Readers/FurochibiHelper");
        QDir().mkpath(folder);
        QDir().mkpath(root.path() + QStringLiteral("/Readers/Furoshiki"));
        QString problem;
        CHECK(!Home::ReadLocalHelper(folder, &problem).Valid() && problem.contains(QStringLiteral("not there")));
        CHECK(!Home::FindLocalHelper(root.path() + QStringLiteral("/Readers/Furoshiki")).Valid());
        quint16 port = 0;
        {
            QTcpServer free;
            free.listen(QHostAddress::LocalHost, 0);
            port = free.serverPort();
        }
        const QString address = QStringLiteral("http://127.0.0.1:%1").arg(port);
        const char* stand_in = R"PY(
import json, os, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
open("started", "a").write(os.environ.get("HELPER_SAYS", "") + "\n")
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps({"status": "ok", "role": "helper"} if self.path == "/api/health" else
                          {"source": "furoshiki", "sources": [{"id": "furoshiki", "name": "A window", "push": True}]}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def log_message(self, *a): pass
server = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler)
def gone():
    sys.stdin.buffer.read()
    open("stopped", "a").write("stdin ended\n")
    os._exit(0)
threading.Thread(target=gone, daemon=True).start()
server.serve_forever()
)PY";
        const auto write = [&](const QJsonObject& object) {
            QFile file(folder + QStringLiteral("/helper.json"));
            CHECK(file.open(QIODevice::WriteOnly));
            file.write("\xef\xbb\xbf" + QJsonDocument(object).toJson()); // as PowerShell writes it
        };
        QJsonObject said{{QStringLiteral("format"), 1}, {QStringLiteral("name"), QStringLiteral("Stand-in helper")},
                         {QStringLiteral("address"), address + QLatin1Char('/')}, {QStringLiteral("source"), QStringLiteral("furoshiki")},
                         {QStringLiteral("program"), QJsonArray{QStringLiteral("python3"), QStringLiteral("-c"), QString::fromUtf8(stand_in), QString::number(port)}},
                         {QStringLiteral("env"), QJsonObject{{QStringLiteral("HELPER_SAYS"), QStringLiteral("in {folder}")}}}};
        write(said);
        CHECK(!Home::ReadLocalHelper(folder, &problem).Valid() && problem.contains(QStringLiteral("lacks"))); // no token
        said.insert(QStringLiteral("token"), QStringLiteral("the-installed-helper-token"));
        write(said);
        const Home::LocalHelper local = Home::FindLocalHelper(root.path() + QStringLiteral("/Readers/Furoshiki"));
        CHECK(local.Valid() && local.folder == QDir(folder).absolutePath() && local.address == address && local.program.size() == 4);
        const Home::Lines::Helper setting = Home::SettingOf(local);
        CHECK(setting.address == address && setting.source == QStringLiteral("furoshiki") && setting.program == local.folder &&
              setting.token == QStringLiteral("the-installed-helper-token"));
        // The device's setting keeps the folder; a setting without one is written as it was.
        Home::Lines::Sources sources = Home::Lines::Load(root.path());
        sources.ocr = setting;
        CHECK(Home::Lines::Save(root.path(), sources) && Home::Lines::Load(root.path()).ocr.program == local.folder);
        // Installed again with another token: the helper's own file is what counts.
        Home::Lines::Helper stale = setting;
        stale.token = QStringLiteral("an-older-token-0123456789");
        CHECK(Home::Refreshed(stale).token == setting.token);
        Home::Lines::Helper elsewhere{QStringLiteral("http://helper.example:8794"), QStringLiteral("z13"), QStringLiteral("a-token-of-the-helper")};
        CHECK(Home::Refreshed(elsewhere).address == elsewhere.address && Home::Refreshed(elsewhere).program.isEmpty());

        Home::OcrHelper& helper = Home::OcrHelper::Instance();
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) {
            std::fprintf(stderr, "no python3 here: the helper's start is not tried\n");
        } else {
            // Nothing answers: the client's first question starts it, and is answered.
            Home::OcrClient client;
            QList<QPair<bool, QString>> answered;
            QObject::connect(&client, &Home::OcrClient::Answered, [&](bool ok, const QString& what) { answered.append({ok, what}); });
            client.Set(stale);
            CHECK(!helper.Runs());
            client.Ask();
            CHECK(until([&] { return helper.Starting(); }, 3000));
            CHECK(until([&] { return answered.size() == 1; }) && answered.value(0).first && answered.value(0).second == QStringLiteral("A window"));
            CHECK(helper.Runs() && !helper.Starting());
            QFile started(folder + QStringLiteral("/started"));
            CHECK(started.open(QIODevice::ReadOnly) && started.readAll() == QByteArray("in ") + QDir(folder).absolutePath().toUtf8() + "\n");
            // Asked again while it runs: used as it is, not started twice.
            client.Ask();
            CHECK(until([&] { return answered.size() == 2; }) && answered.value(1).first);
            started.seek(0);
            CHECK(started.readAll().count('\n') == 1);
            helper.Stop();
            CHECK(!helper.Runs() && QFile::exists(folder + QStringLiteral("/stopped"))); // by its standard input's end
            // A program that is not there: said, with nothing left running.
            said.insert(QStringLiteral("program"), QJsonArray{QStringLiteral("no-such-program-of-the-helper")});
            write(said);
            client.Set(setting);
            client.Ask();
            CHECK(until([&] { return answered.size() == 3; }) && !answered.value(2).first && answered.value(2).second.contains(QStringLiteral("no-such-program")));
            CHECK(!helper.Runs() && client.Status().startsWith(QStringLiteral("no answer")));
            // One that ends as it starts: said too, with where its log is.
            said.insert(QStringLiteral("program"), QJsonArray{QStringLiteral("python3"), QStringLiteral("-c"), QStringLiteral("raise SystemExit('cannot')")});
            write(said);
            client.Ask();
            CHECK(until([&] { return answered.size() == 4; }) && !answered.value(3).first && answered.value(3).second.contains(QStringLiteral("furoshiki.log")));
            QFile log(helper.LogFile());
            CHECK(log.open(QIODevice::ReadOnly) && log.readAll().contains("cannot"));
        }
    }

    // The WebSocket lines come over, against a server made here on a loopback port of the
    // system's choosing: the handshake, text frames plain, long and in pieces, a ping answered,
    // and the server found again after it went away.
    {
        const auto until = [](const std::function<bool()>& done, int within_ms = 4000) {
            QElapsedTimer timer;
            timer.start();
            while (!done() && timer.elapsed() < within_ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            return done();
        };
        const auto frame = [](quint8 first, const QByteArray& payload) {
            QByteArray out(1, static_cast<char>(first));
            if (payload.size() < 126) {
                out.append(static_cast<char>(payload.size()));
            } else {
                out.append(static_cast<char>(126));
                out.append(static_cast<char>(payload.size() >> 8));
                out.append(static_cast<char>(payload.size() & 0xff));
            }
            return out + payload;
        };
        QTcpServer server;
        CHECK(server.listen(QHostAddress::LocalHost, 0));
        const quint16 port = server.serverPort();
        QTcpSocket* peer = nullptr;
        QByteArray request, received;
        bool shaken = false;
        QString asked_path;
        const auto accept = [&] {
            peer = server.nextPendingConnection();
            request.clear();
            received.clear();
            shaken = false;
            QObject::connect(peer, &QTcpSocket::readyRead, &server, [&] {
                if (shaken) {
                    received.append(peer->readAll());
                    return;
                }
                request.append(peer->readAll());
                if (!request.contains("\r\n\r\n")) {
                    return;
                }
                asked_path = QString::fromLatin1(request.left(request.indexOf('\r'))).section(QLatin1Char(' '), 1, 1);
                const qsizetype at = request.indexOf("Sec-WebSocket-Key: ") + 19;
                const QByteArray key = request.mid(at, request.indexOf('\r', at) - at);
                peer->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nsec-websocket-accept: " +
                            QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64() +
                            "\r\n\r\n");
                shaken = true;
            });
        };
        QObject::connect(&server, &QTcpServer::newConnection, &server, accept);

        Home::WsClient client;
        client.retry_ms = 60;
        QStringList texts;
        QObject::connect(&client, &Home::WsClient::Text, &server, [&](const QString& text) { texts.append(text); });
        CHECK(!client.Open(QStringLiteral("http://127.0.0.1:1")) && !client.Open(QStringLiteral("localhost")));
        CHECK(client.Open(QStringLiteral("ws://127.0.0.1:%1/lines?x=1").arg(port)));
        CHECK(until([&] { return client.Connected(); }));
        CHECK(asked_path == QStringLiteral("/lines?x=1"));
        CHECK(client.Status().startsWith(QStringLiteral("connected  ·  ws://127.0.0.1")));
        const QByteArray plain = QStringLiteral("一行目です").toUtf8();
        const QByteArray longer = QStringLiteral("長い行です。").repeated(30).toUtf8(); // more than 125 bytes
        const QByteArray json = QStringLiteral("{\"sentence\":\"三行目\"}").toUtf8();
        // Two frames in one write, then a ping between the two pieces of a third message.
        peer->write(frame(0x81, plain) + frame(0x81, longer) + frame(0x01, json.left(9)) + frame(0x89, "hi") + frame(0x80, json.mid(9)));
        CHECK(until([&] { return texts.size() == 3; }));
        CHECK(texts.value(0) == QString::fromUtf8(plain) && texts.value(1) == QString::fromUtf8(longer));
        CHECK(Home::Lines::MessageText(texts.value(2)) == QStringLiteral("三行目"));
        // The pong: masked, as a client's frames must be, with the ping's own bytes.
        CHECK(until([&] { return received.size() >= 8; }));
        CHECK(static_cast<quint8>(received[0]) == 0x8a && static_cast<quint8>(received[1]) == 0x82);
        CHECK(static_cast<char>(received[6] ^ received[2]) == 'h' && static_cast<char>(received[7] ^ received[3]) == 'i');
        // The server goes away and comes back: the client finds it again by itself.
        peer->abort();
        server.close();
        CHECK(until([&] { return !client.Connected(); }));
        CHECK(client.Status().startsWith(QStringLiteral("waiting  ·  ")));
        CHECK(server.listen(QHostAddress::LocalHost, port));
        CHECK(until([&] { return client.Connected(); }));
        peer->write(frame(0x81, plain));
        CHECK(until([&] { return texts.size() == 4; }));
        // A server that is no WebSocket is let go, and tried again.
        client.Close();
        CHECK(!client.Connected() && client.Status() == QStringLiteral("not connected"));
    }

    // Hooks started by the program (docs/hooks.md). What the engines write is made up here
    // from their documented forms: neither Textractor nor LunaHook was run.
    {
        using namespace Home;
        using namespace Home::Hooks;
        const auto U = [](const char8_t* text) { return QString::fromUtf8(reinterpret_cast<const char*>(text)); };
        CHECK(EngineOf(QStringLiteral("agent")) == Engine::Agent && EngineOf(QStringLiteral("LUNA")) == Engine::Luna && !EngineOf(QStringLiteral("ocr")));
        CHECK(KeyOf(Engine::Textractor) == QStringLiteral("textractor") && LabelOf(Engine::Luna) == QStringLiteral("LUNAHOOK"));
        // A thread's key keeps what stays from one start of a game to the next.
        CHECK(ThreadKey(QStringLiteral("KiriKiri"), QStringLiteral("HQ@1:game.exe"), 0x7FF6001B2A, 0) == QStringLiteral("KIRIKIRI|HQ@1:GAME.EXE|1B2A|0"));
        CHECK(ThreadKey(QStringLiteral("KiriKiri"), QStringLiteral("HQ@1:game.exe"), 0x7FF7AA1B2A, 0) == ThreadKey(QStringLiteral("KiriKiri"), QStringLiteral("HQ@1:game.exe"), 0x7FF6001B2A, 0));

        // frida-inject with Agent's runtime: a line of text, a script's log, its error, its storage.
        auto event = ReadInjectLine(QByteArray(reinterpret_cast<const char*>(u8"{\"type\":\"send\",\"payload\":{\"cmd\":\"copy\",\"text\":\"雨が降り始めた。\"}}")));
        CHECK(event && event->kind == Event::Text && event->thread.key == AgentThread && event->text == U(u8"雨が降り始めた。"));
        CHECK(!ReadInjectLine("{\"type\":\"send\",\"payload\":{\"cmd\":\"copy\",\"text\":null}}"));
        event = ReadInjectLine("[+] Agent loaded: ./NS_x.js");
        CHECK(event && event->kind == Event::Said && event->text == QStringLiteral("[+] Agent loaded: ./NS_x.js"));
        event = ReadInjectLine("\x1b[0;33mPlease update the game\x1b[0m");
        CHECK(event && event->kind == Event::Said && event->text == QStringLiteral("Please update the game"));
        event = ReadInjectLine("{\"type\":\"error\",\"description\":\"Error: unable to find export\",\"fileName\":\"/scripts/x.js\",\"lineNumber\":13}");
        CHECK(event && event->kind == Event::Fault && event->text == QStringLiteral("Error: unable to find export (/scripts/x.js:13)"));
        event = ReadInjectLine("{\"type\":\"send\",\"payload\":{\"cmd\":\"furoshiki_storage\",\"which\":\"sessionStorage\",\"op\":\"setItem\",\"args\":[\"k\",\"v\"]}}");
        CHECK(event && event->kind == Event::Storage && event->which == QStringLiteral("sessionStorage") && event->args == QStringList({QStringLiteral("k"), QStringLiteral("v")}));
        CHECK(!ReadInjectLine("") && !ReadInjectLine("{\"type\":\"send\",\"payload\":{\"cmd\":\"loadHtml\"}}"));

        // Textractor's CLI: UTF-16LE, in pieces that cut a character and a line in two; a
        // sentence over two lines; its Console and Clipboard threads.
        const QString said = U(u8"﻿Usage: {'attach'|'detach'|hookcode} -Pprocessid\n"
                               u8"[0:0:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:Console:HB0@0] Textractor: pipe connected\n"
                               u8"[1:0:0:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:Clipboard:HB0@0] 写したもの\n"
                               u8"[3:1A2B:7FF61B2F40:7FF6001B2A:0:KiriKiri:HQN-C*0@1B2F40:kaitou.exe] 風が強い。\r\nマントが音を立てる。\n"
                               u8"[2:1A2B:7FFB1A2C0010:4012A0:0:GetGlyphOutlineA:HB4@0:gdi32.dll:GetGlyphOutlineA] 風風がが\n"
                               u8"[0:0:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:Console:HB0@0] Textractor: architecture mismatch: only Textractor x86 can inject this process\n");
        const QByteArray bytes(reinterpret_cast<const char*>(said.utf16()), said.size() * 2);
        TextractorReader reader;
        QList<Event> events;
        for (qsizetype at = 0; at < bytes.size(); at += 7) {
            events += reader.Take(bytes.mid(at, 7));
        }
        CHECK(events.size() == 3); // the last sentence waits for what follows it
        CHECK(events.size() == 3 && events[0].kind == Event::Attached && events[0].text == QStringLiteral("Textractor: pipe connected"));
        CHECK(events.size() == 3 && events[1].kind == Event::Text && events[1].text == U(u8"風が強い。\nマントが音を立てる。") && events[1].thread.pid == 0x1A2B &&
              events[1].thread.name == QStringLiteral("KiriKiri") && events[1].thread.code == QStringLiteral("HQN-C*0@1B2F40:kaitou.exe") &&
              events[1].thread.ctx == 0x7FF6001B2AULL && events[1].thread.key == QStringLiteral("KIRIKIRI|HQN-C*0@1B2F40:KAITOU.EXE|1B2A|0"));
        CHECK(events.size() == 3 && events[2].thread.name == QStringLiteral("GetGlyphOutlineA") && events[2].thread.code == QStringLiteral("HB4@0:gdi32.dll:GetGlyphOutlineA"));
        const auto rest = reader.Rest();
        CHECK(rest && rest->kind == Event::Fault && !reader.Rest());
        // What it is told: UTF-16LE, attach first, then only what is a hook code (any other line ends it).
        const QByteArray commands = TextractorCommands(6699, {QStringLiteral("/HS-4@12345:game.exe"), QStringLiteral("rm -rf"), QStringLiteral("HQN-C*0@1B2F40:kaitou.exe")});
        CHECK(QString::fromUtf16(reinterpret_cast<const char16_t*>(commands.constData()), commands.size() / 2) ==
              QStringLiteral("attach -P6699\n/HS-4@12345:game.exe -P6699\nHQN-C*0@1B2F40:kaitou.exe -P6699\n"));
        CHECK(LooksLikeHookCode(QStringLiteral("/RS@41AF00")) && !LooksLikeHookCode(QStringLiteral("attach")) && !LooksLikeHookCode(QStringLiteral("/H S@1")));

        // Furoshiki's LunaHook host: lines of JSON.
        event = ReadLunaLine(QByteArray(reinterpret_cast<const char*>(u8"{\"text\":\"「今夜の獲物は」\",\"name\":\"KiriKiri\",\"code\":\"HQN-C*0@1B2F40:kaitou.exe\",\"pid\":6699,\"addr\":\"7FF61B2F40\",\"ctx\":\"7FF6001B2A\",\"ctx2\":\"0\"}")));
        CHECK(event && event->kind == Event::Text && event->thread.pid == 6699 && event->thread.addr == 0x7FF61B2F40ULL &&
              event->thread.key == QStringLiteral("KIRIKIRI|HQN-C*0@1B2F40:KAITOU.EXE|1B2A|0") && event->text == U(u8"「今夜の獲物は」"));
        CHECK(ReadLunaLine("{\"attached\":6699}")->kind == Event::Attached && ReadLunaLine("{\"gone\":6699}")->kind == Event::Gone);
        CHECK(ReadLunaLine("{\"fault\":\"LunaHost64.dll could not be loaded\"}")->kind == Event::Fault && ReadLunaLine("{\"said\":\"x\"}")->kind == Event::Said);
        CHECK(!ReadLunaLine("not json") && !ReadLunaLine("{\"other\":1}"));
        // The threads, in the order they came, each with the last thing it said.
        Threads threads;
        threads.Note(events[1]);
        threads.Note(events[2]);
        threads.Note(*event);
        CHECK(threads.All().size() == 2 && threads.All()[0].count == 2 && threads.All()[0].latest == U(u8"「今夜の獲物は」") && threads.All()[1].count == 1);
        CHECK(threads.Find(QStringLiteral("KIRIKIRI|HQN-C*0@1B2F40:KAITOU.EXE|1B2A|0")) != nullptr && threads.Find(QStringLiteral("none")) == nullptr);

        // 0xDC00's scripts: a folder of made-up ones (the authors' are never in this repository).
        QTemporaryDir made;
        const QString folder = made.path() + QStringLiteral("/program/hooks/scripts");
        QDir().mkpath(folder + QStringLiteral("/charsets"));
        Write(folder + QStringLiteral("/NS_0100AAAA00000000_Ame_no_Yamamichi.js"),
              "// ==UserScript==\n// @name         [0100AAAA00000000] Ame no Yamamichi\n// @version      1.0.1\n// @author       [Someone]\n"
              "// @description  Yuzu, Ryujinx\n// * A maker\n// ==/UserScript==\nconst { setHook } = require('./libYuzu.js');\nconst t = require(\"./charsets/table.json\");\n");
        Write(folder + QStringLiteral("/PC_Kaitou.js"), "// ==UserScript==\n// @name         Kaitou no Yoru\n// @version      \n// @author       A, B\n// @description  Steam\n// ==/UserScript==\n");
        Write(folder + QStringLiteral("/libYuzu.js"), "// @name libYuzu\nconst h = require('./libHelper.js');\nmodule.exports = {};\n");
        Write(folder + QStringLiteral("/libHelper.js"), "module.exports = {};\n");
        Write(folder + QStringLiteral("/libUnused.js"), "module.exports = {};\n");
        Write(folder + QStringLiteral("/charsets/table.json"), "{\"a\":1}");
        Write(folder + QStringLiteral("/notes.js"), "// no header\n");
        const QList<Agent::Script> scripts = Agent::List(folder);
        CHECK(scripts.size() == 2);
        CHECK(scripts.size() == 2 && scripts[0].name == QStringLiteral("Ame no Yamamichi") && scripts[0].id == QStringLiteral("0100AAAA00000000") &&
              scripts[0].platform == QStringLiteral("SWITCH") && scripts[0].version == QStringLiteral("1.0.1") && scripts[0].author == QStringLiteral("Someone") &&
              scripts[0].runs_on == QStringLiteral("Yuzu, Ryujinx"));
        CHECK(scripts.size() == 2 && scripts[1].name == QStringLiteral("Kaitou no Yoru") && scripts[1].id.isEmpty() && scripts[1].platform == QStringLiteral("PC"));
        CHECK(Agent::Search(scripts, QStringLiteral("switch yama")).size() == 1 && Agent::Search(scripts, QStringLiteral("0100aaaa")).size() == 1 &&
              Agent::Search(scripts, QStringLiteral("kaitou switch")).isEmpty() && Agent::Search(scripts, QString()).size() == 2);
        // Suggested for the program a game window is of: by its file, folder and title; by the ID
        // an emulator's window shows; nothing for a program no script is like.
        {
            QList<Agent::Script> all = scripts;
            Agent::Script chaos;
            chaos.file = QStringLiteral("PC_Steam_MAGES_Chaos;Child.js");
            chaos.name = QStringLiteral("Chaos;Child");
            chaos.platform = QStringLiteral("PC");
            all.append(chaos);
            const auto first = [&](const QString& program, const QString& path, const QString& window) {
                const QList<Agent::Script> found = Agent::Suggest(all, program, path, window);
                return found.isEmpty() ? QString() : found.first().file;
            };
            CHECK(first(QStringLiteral("ChaosChild.exe"), QStringLiteral("C:/Steam/steamapps/common/CHAOS;CHILD/ChaosChild.exe"), QStringLiteral("CHAOS;CHILD")) == chaos.file);
            CHECK(first(QStringLiteral("kaitou.exe"), QStringLiteral("D:/Games/Kaitou no Yoru/kaitou.exe"), QStringLiteral("怪盗の夜 ver1.02")) == scripts[1].file);
            CHECK(first(QStringLiteral("yuzu.exe"), QStringLiteral("C:/yuzu/yuzu.exe"), QStringLiteral("yuzu 1734 | Ame no Yamamichi (0100AAAA00000000)")) == scripts[0].file);
            CHECK(first(QStringLiteral("notepad.exe"), QStringLiteral("C:/Windows/notepad.exe"), QStringLiteral("Untitled - Notepad")).isEmpty());
            CHECK(first(QStringLiteral("yuzu.exe"), QStringLiteral("C:/yuzu/yuzu.exe"), QStringLiteral("yuzu 1734")).isEmpty()); // no game in it yet
        }
        CHECK(Agent::Requires(QStringLiteral("require('./a.js'); require(\"../b.js\"); require('fs'); require('./a.js')")) == QStringList({QStringLiteral("./a.js"), QStringLiteral("../b.js")}));
        CHECK(Agent::CleanPath(QStringLiteral("./x/../lib.js")) == QStringLiteral("lib.js") && Agent::CleanPath(QStringLiteral("../../etc/passwd")).isEmpty());
        // The bundle: no runtime, no bundle; with it, the script and what it requires and no more.
        QString error;
        CHECK(!Agent::HasRuntime(folder) && Agent::Bundle(folder, scripts[0].file, QStringLiteral("HOST;"), {}, error).isEmpty() && error.contains(QStringLiteral("libLoader.js")));
        Write(folder + QStringLiteral("/libLoader.js"), "RUNTIME();");
        const QJsonObject kept{{QStringLiteral("localStorage"), QJsonObject{{QStringLiteral("seen"), QStringLiteral("1")}}}};
        const QString bundle = Agent::Bundle(folder, scripts[0].file, QStringLiteral("HOST;"), kept, error);
        CHECK(Agent::HasRuntime(folder) && bundle.contains(QStringLiteral("const MISSION = [\"NS_0100AAAA00000000_Ame_no_Yamamichi.js\"][0];")));
        CHECK(bundle.contains(QStringLiteral("\"libYuzu.js\":")) && bundle.contains(QStringLiteral("\"libHelper.js\":")) && bundle.contains(QStringLiteral("\"charsets/table.json\":")) &&
              !bundle.contains(QStringLiteral("libUnused")) && !bundle.contains(QStringLiteral("PC_Kaitou")));
        CHECK(bundle.contains(QStringLiteral("\"localStorage\":{\"seen\":\"1\"}")) && bundle.contains(QStringLiteral("\"sessionStorage\":{}")));
        CHECK(bundle.indexOf(QStringLiteral("HOST;")) > 0 && bundle.indexOf(QStringLiteral("HOST;")) < bundle.indexOf(QStringLiteral("RUNTIME();")) &&
              bundle.contains(QStringLiteral("__furoshiki.send, __furoshiki.recv")));
        CHECK(Agent::Bundle(folder, QStringLiteral("../outside.js"), QStringLiteral("HOST;"), {}, error).isEmpty());
        // What a script stored: kept a title; a session's only for the same process.
        QJsonObject storage = Agent::LoadStorage(made.path(), QStringLiteral("LINES-1"), 100);
        Agent::ApplyStorage(storage, QStringLiteral("sessionStorage"), QStringLiteral("setItem"), {QStringLiteral("blocks"), QStringLiteral("[1]")});
        Agent::ApplyStorage(storage, QStringLiteral("localStorage"), QStringLiteral("setItem"), {QStringLiteral("lang"), QStringLiteral("ja")});
        Agent::ApplyStorage(storage, QStringLiteral("elsewhere"), QStringLiteral("setItem"), {QStringLiteral("x"), QStringLiteral("y")});
        CHECK(Agent::SaveStorage(made.path(), QStringLiteral("LINES-1"), 100, storage));
        CHECK(Agent::LoadStorage(made.path(), QStringLiteral("LINES-1"), 100).value(QStringLiteral("sessionStorage")).toObject().value(QStringLiteral("blocks")).toString() == QStringLiteral("[1]"));
        const QJsonObject later = Agent::LoadStorage(made.path(), QStringLiteral("LINES-1"), 200);
        CHECK(later.value(QStringLiteral("sessionStorage")).toObject().isEmpty() && later.value(QStringLiteral("localStorage")).toObject().value(QStringLiteral("lang")).toString() == QStringLiteral("ja"));
        CHECK(Agent::LoadStorage(made.path(), QStringLiteral("LINES-2"), 100).value(QStringLiteral("localStorage")).toObject().isEmpty());

        // A title read by a hook, in the titles' file.
        Lines::Sources sources;
        Lines::Title hook;
        hook.engine = QStringLiteral("agent");
        hook.process = QStringLiteral("yuzu.exe");
        CHECK(Lines::AddHook(sources, QStringLiteral("Ame"), hook, QString()).isEmpty()); // an Agent hook is nothing without its script
        hook.script = scripts[0].file;
        const QString agent_id = Lines::AddHook(sources, QStringLiteral("Ame"), hook, QString());
        Lines::Title luna;
        luna.engine = QStringLiteral("luna");
        luna.process = QStringLiteral("kaitou.exe");
        luna.threads = {QStringLiteral("KIRIKIRI|HQN-C*0@1B2F40:KAITOU.EXE|1B2A|0")};
        luna.codes = {QStringLiteral("/HS-4@12345:kaitou.exe")};
        const QString luna_id = Lines::AddHook(sources, QStringLiteral("Kaitou"), luna, QString());
        CHECK(!agent_id.isEmpty() && !luna_id.isEmpty() && Lines::Save(made.path(), sources));
        const Lines::Sources read = Lines::Load(made.path());
        CHECK(read.titles.size() == 2);
        CHECK(read.titles.size() == 2 && read.titles[0].by == QStringLiteral("hook") && read.titles[0].engine == QStringLiteral("agent") && read.titles[0].script == scripts[0].file &&
              read.titles[0].process == QStringLiteral("yuzu.exe") && read.titles[0].hook_auto);
        CHECK(read.titles.size() == 2 && read.titles[1].threads == luna.threads && read.titles[1].codes == luna.codes);
        CHECK(Lines::PathOf(read.titles[0]) == QStringLiteral("lines://hook-agent/") + agent_id && Lines::ReadByOf(Lines::PathOf(read.titles[0])) == QStringLiteral("HOOK  ·  AGENT"));
        CHECK(Lines::ReadByOf(Lines::PathOf(read.titles[1])) == QStringLiteral("HOOK  ·  LUNAHOOK") && Lines::ReadByOf(QStringLiteral("lines://websocket/X")) == QStringLiteral("LINES  ·  WEBSOCKET") &&
              Lines::ReadByOf(QStringLiteral("lines://ocr/X")) == QStringLiteral("WINDOW  ·  OCR") && Lines::ReadByOf(QStringLiteral("/games/x.iso")).isEmpty());
        // Read by itself when its program starts: only where one title names that program.
        CHECK(Lines::HookedIn(read, QStringLiteral("KAITOU.EXE")).size() == 1 && Lines::HookedIn(read, QStringLiteral("other.exe")).isEmpty());
        Lines::Sources two = read;
        hook.script = QStringLiteral("PC_Kaitou.js");
        Lines::AddHook(two, QStringLiteral("Another in yuzu"), hook, QString());
        CHECK(Lines::HookedIn(two, QStringLiteral("yuzu.exe")).isEmpty());

        // The programs running, as a test names them; this test itself among the real ones.
        qputenv("READER_HOME_FAKE_PROGRAMS", QByteArray(reinterpret_cast<const char*>(u8"5151|kaitou.exe|怪盗の夜|32;?|no-such-program-runs|x;77|tool")));
        const QList<Program> programs = Programs();
        CHECK(programs.size() == 2 && programs[0].pid == 5151 && programs[0].is32 && programs[0].window == U(u8"怪盗の夜") && programs[1].window.isEmpty());
        CHECK(ProgramNamed(QStringLiteral("KAITOU.EXE")) && !ProgramNamed(QStringLiteral("gone.exe")));
        qunsetenv("READER_HOME_FAKE_PROGRAMS");
        CHECK(Runs(QCoreApplication::applicationPid()) && !Runs(0));
#ifndef _WIN32
        bool has_parent = false;
        for (const Program& program : Programs()) {
            has_parent = has_parent || program.pid == getppid();
        }
        CHECK(has_parent); // this user's processes are listed (the one that started the tests)

        // An engine run against a process: a stand-in that writes what Textractor's CLI writes
        // (a shell's printf of UTF-16LE), read into threads and lines by the runner.
        const QString standin = made.path() + QStringLiteral("/textractor-standin.sh");
        const QString recorded = U(u8"[0:0:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:FFFFFFFFFFFFFFFF:Console:HB0@0] Textractor: pipe connected\n"
                                   u8"[2:1A2B:10:4012A0:0:GetGlyphOutlineA:HB4@0] 警警部部\n"
                                   u8"[3:1A2B:20:7FF6001B2A:0:KiriKiri:HQN-C*0@1B2F40:kaitou.exe] 警部はまだ、何も知らない。\n");
        Write(made.path() + QStringLiteral("/recorded.bin"), QByteArray(reinterpret_cast<const char*>(recorded.utf16()), recorded.size() * 2));
        Write(standin, QStringLiteral("#!/bin/sh\nhead -c 28 > '%1/asked.bin'\ncat '%1/recorded.bin'\nsleep 0.4\n").arg(made.path()).toUtf8());
        QFile::setPermissions(standin, QFile::permissions(standin) | QFileDevice::ExeOwner);
        qputenv("READER_HOME_TEXTRACTOR_CLI", ("/bin/sh|" + standin).toUtf8());
        Runner runner;
        QStringList lines;
        QString ended;
        QObject::connect(&runner, &Runner::Line, [&](const Thread& thread, const QString& text) { lines.append(thread.name + QLatin1Char('=') + text); });
        QObject::connect(&runner, &Runner::Ended, [&](const QString& why) { ended = why; });
        Plan plan;
        plan.engine = Engine::Textractor;
        plan.pid = 6699;
        plan.shared = made.path();
        CHECK(runner.Start(plan, error) && runner.Running());
        QElapsedTimer waited;
        waited.start();
        while (ended.isEmpty() && waited.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        CHECK(lines == QStringList({QStringLiteral("GetGlyphOutlineA=") + U(u8"警警部部"), QStringLiteral("KiriKiri=") + U(u8"警部はまだ、何も知らない。")}));
        CHECK(runner.Threads().size() == 2 && runner.Said().contains(QStringLiteral("Textractor: pipe connected")) && ended == QStringLiteral("the program went") && !runner.Running());
        QFile asked(made.path() + QStringLiteral("/asked.bin"));
        CHECK(asked.open(QIODevice::ReadOnly) && asked.readAll() == TextractorCommands(6699, {}));
        qunsetenv("READER_HOME_TEXTRACTOR_CLI");
        // No engine here: said, not tried.
        plan.engine = Engine::Luna;
        CHECK(!runner.Start(plan, error) && error.contains(QStringLiteral("LunaHook")));
        plan.engine = Engine::Agent;
        plan.script = scripts[0].file;
        qputenv("READER_HOME_FRIDA_INJECT", "/no/such/frida-inject");
        if (FridaInject(made.path()).isEmpty()) {
            CHECK(!runner.Start(plan, error) && error.contains(QStringLiteral("frida-inject")));
        }
        qunsetenv("READER_HOME_FRIDA_INJECT");
#endif
        CHECK(KitOf(QStringLiteral("scripts")).from.contains(QStringLiteral("0xDC00/scripts")) && KitOf(QStringLiteral("luna")).from.contains(LunaVersion) && CanFetch(QStringLiteral("scripts")));
    }

    std::printf(failures == 0 ? "All tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
