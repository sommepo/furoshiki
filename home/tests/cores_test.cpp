// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The program's side of cores/reader_core.h, with the made-up console of cores/test: a core is
// found, loaded and checked; its console becomes the reader's; its game runs on its thread and
// a profile reads it, from its frame callback and from a hook with the core's registers.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "core_library.h"
#include "core_session.h"
#include "reader/core/console.h"
#include "reader/core/registers.h"

namespace {

int failures = 0;
#define CHECK(what)                                                                     \
    do {                                                                                \
        if (!(what)) {                                                                  \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what);              \
            ++failures;                                                                 \
        }                                                                               \
    } while (false)

const char* const Profile = R"(-- the made-up console's game
local seen = ""
reader.on_frame(function()
    local line = mem.cstr(0x1000, 64)
    if line ~= "" and line ~= seen then
        seen = line
        reader.emit{text = line, window = "main"}
    end
end)
local ok, why = pcall(reader.hook, 0x2002, function() end)
reader.emit{text = "unaligned: " .. tostring(why), channel = "check"}
local once = false
reader.hook(0x2000, function(cpu)
    if once then return end
    once = true
    reader.emit{text = mem.cstr(cpu.w1, 64), channel = "hooked",
                speaker = string.format("%X %X %g %X %s", cpu.a0, cpu.w1, cpu.f0, cpu.pc, tostring(cpu.zz))}
end)
)";

struct Seen {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Reader::TextEvent> events;

    bool WaitFor(std::size_t count) {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds(5), [&] { return events.size() >= count; });
    }
};

/// What the core told, in order, from whatever thread it told it on.
struct Told {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> what;
    void Add(std::string line) {
        std::scoped_lock lock{mutex};
        what.push_back(std::move(line));
        changed.notify_all();
    }
    bool WaitFor(const std::string& line) {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds(5), [&] {
            return std::find(what.begin(), what.end(), line) != what.end();
        });
    }
    std::ptrdiff_t Index(const std::string& line) {
        std::scoped_lock lock{mutex};
        const auto at = std::find(what.begin(), what.end(), line);
        return at == what.end() ? -1 : at - what.begin();
    }
};

std::string MemoryText(uint64_t address) {
    const std::string bytes = Reader::Engine::Instance().ReadMemory(address, 256);
    return bytes.substr(0, bytes.find('\0'));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir folder;

    // Found by its name, loaded, checked.
    const QString built = QStringLiteral(HOME_TEST_CORE);
    const QString suffix = QFileInfo(built).suffix();
    CHECK(QFile::copy(built, folder.filePath(QStringLiteral("reader_core_test.") + suffix)));
    QFile::copy(built, folder.filePath(QStringLiteral("other_library.") + suffix));
    const QStringList found = Home::FindCores(folder.path());
    CHECK(found.size() == 1);
    Home::Core core;
    QString error;
    std::string session_error;
    CHECK(!Home::LoadCore(folder.filePath(QStringLiteral("nothing.so")), core, error));
    CHECK(!error.isEmpty());
    CHECK(Home::LoadCore(found.value(0), core, error));
    if (core.api == nullptr) {
        std::printf("FAILED: no core (%s)\n", qPrintable(error));
        return 1;
    }
    CHECK(QString::fromUtf8(core.info->system) == QStringLiteral("Test Console"));
    CHECK(Home::CheckCore(*core.api, *core.info).isEmpty());
    // What a careless core might say is refused.
    {
        reader_core_info wrong = *core.info;
        wrong.key = "";
        CHECK(!Home::CheckCore(*core.api, wrong).isEmpty());
        wrong = *core.info;
        reader_core_register_name names[] = {{"r9", 9, READER_CORE_REGISTER_INTEGER, 64}};
        wrong.cpu.names = names;
        wrong.cpu.name_count = 1;
        CHECK(!Home::CheckCore(*core.api, wrong).isEmpty());
        wrong = *core.info;
        wrong.cpu.hook_alignment = 3;
        CHECK(!Home::CheckCore(*core.api, wrong).isEmpty());
        wrong = *core.info;
        wrong.api_version = READER_CORE_API_VERSION + 1;
        CHECK(!Home::CheckCore(*core.api, wrong).isEmpty());
        reader_core_api short_api = *core.api;
        short_api.struct_size = 8;
        CHECK(!Home::CheckCore(short_api, *core.info).isEmpty());
        // A core that says it can do more than it has (discs, without disc_count) is refused;
        // one built before the additions (a shorter description) has none of them, and is fine.
        reader_core_api no_discs = *core.api;
        no_discs.disc_count = nullptr;
        CHECK(!Home::CheckCore(no_discs, *core.info).isEmpty());
        wrong = *core.info;
        wrong.struct_size = offsetof(reader_core_info, features);
        CHECK(Home::CheckCore(*core.api, wrong).isEmpty());
        CHECK(Home::Features(wrong) == 0 && Home::PortCount(wrong) == 1);
    }
    // What it can do beyond the first version.
    CHECK(Home::Features(*core.info) == 127u);
    CHECK(Home::PortCount(*core.info) == 2);
    CHECK(Home::SettingFlags(*core.info, 1) == READER_CORE_SETTING_AT_START);
    CHECK(Home::SettingFlags(*core.info, 2) == READER_CORE_SETTING_PER_GAME);
    CHECK(Home::SettingFlags(*core.info, 9) == 0);

    // Its console becomes the reader's.
    Home::CoreSession session(core);
    const QByteArray data = folder.filePath(QStringLiteral("data")).toUtf8();
    const reader_core_paths paths{sizeof(reader_core_paths), data.constData(), data.constData(), data.constData(),
                                  data.constData()};
    CHECK(session.Start(paths, session_error));
    CHECK(std::strcmp(Reader::ConsoleTraits::AppKey, "test-reader") == 0);
    CHECK(Reader::ConsoleTraits::LowerScreenWidth == 320.0f);
    CHECK(Reader::ConsoleTraits::ButtonPressMs == 50);
    CHECK(Reader::Registers::HighestAddress == 0xFFFFFFFFULL);

    // Its game, read by a profile.
    Seen seen;
    Reader::Engine::Instance().SetEventCallback([&](const Reader::TextEvent& event) {
        std::scoped_lock lock{seen.mutex};
        seen.events.push_back(event);
        seen.changed.notify_all();
    });
    session.SetProfileSource([](const std::string& id) -> std::optional<std::string> {
        return id == "TEST-00001" ? std::optional<std::string>(Profile) : std::nullopt;
    });
    session.SetPadSource([](uint32_t port, reader_core_pad& pad) {
        pad.connected = 1;
        pad.buttons = port == 0 ? 2 : 8;
    });
    // The additions' callbacks, each noted in order.
    Told told;
    std::string opened_path;
    session.SetSavesOpeningCallback([&](const Home::CoreSession::SavesOpening& saves) {
        if (saves.locations.size() == 1) {
            opened_path = saves.locations[0].path;
        }
        told.Add("opening " + saves.game_id + " " + std::to_string(saves.locations.size()));
        return 0;
    });
    session.SetGameListener([&](const std::string& id) { told.Add("loaded " + id); });
    session.SetSavesWrittenCallback([&](const std::string& id, const std::string& path, uint32_t kind) {
        told.Add("written " + id + " " + std::to_string(kind) + (path == opened_path ? "" : " elsewhere"));
    });
    session.SetRumbleCallback([&](uint32_t port, uint16_t strong, uint16_t weak) {
        told.Add("rumble " + std::to_string(port) + " " + std::to_string(strong) + " " + std::to_string(weak));
    });
    session.SetDiscCallback([&](int32_t index, int32_t result) {
        told.Add("disc " + std::to_string(index) + " " + std::to_string(result));
    });
    session.SetStateCallback([&](uint32_t what, int32_t slot, int32_t result, const std::string& message) {
        told.Add("state " + std::to_string(what) + " " + std::to_string(slot) + " " + std::to_string(result) +
                 (message.empty() ? "" : " " + message));
    });
    // A game's file not there is said before any core has it (MainWindow::Play asks first): a
    // path, a link to a folder since moved (a test link left after the forks' old input folder
    // went, which a core took and the program then crashed on, 2026-11-08), a file that cannot
    // be read. A name with no folder in it may be an installed title's ID, and a folder a game.
    {
        const auto missing = [&](const QString& path) {
            return Home::CoreSession::MissingGame(path.toStdString());
        };
        CHECK(missing(folder.filePath(QStringLiteral("no-such-game.iso"))).find("not there") != std::string::npos);
        CHECK(!missing(QString()).empty());
        CHECK(missing(QStringLiteral("game.test")).empty());
        CHECK(missing(QStringLiteral("PCSG00488")).empty());
        CHECK(missing(folder.path()).empty());
        QFile there(folder.filePath(QStringLiteral("ゲーム (Japan).iso")));
        CHECK(there.open(QIODevice::WriteOnly) && there.write("CD001", 5) == 5);
        there.close();
        CHECK(missing(there.fileName()).empty());
#if !defined(_WIN32)
        const QString dangling = folder.filePath(QStringLiteral("SteinsGate.iso"));
        CHECK(QFile::link(folder.filePath(QStringLiteral("moved/Steins;Gate (Japan).iso")), dangling));
        const std::string said = missing(dangling);
        CHECK(said.find("link") != std::string::npos && said.find("Steins;Gate (Japan).iso") != std::string::npos);
        const QString linked = folder.filePath(QStringLiteral("linked.iso"));
        CHECK(QFile::link(there.fileName(), linked));
        CHECK(missing(linked).empty());
        CHECK(there.setPermissions(QFileDevice::Permissions{}));
        if (!QFileInfo(there.fileName()).isReadable()) { // not as root, who reads everything
            CHECK(missing(there.fileName()).find("cannot be read") != std::string::npos);
        }
        there.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    }
    CHECK(!session.LoadGame("missing", session_error));
    CHECK(session.LoadGame("game.test", session_error));
    CHECK(seen.WaitFor(3));
    {
        std::scoped_lock lock{seen.mutex};
        bool line = false, hooked = false, unaligned = false;
        for (const Reader::TextEvent& event : seen.events) {
            if (event.channel == "check") {
                unaligned = event.text.find("not a multiple of 4") != std::string::npos;
            } else if (event.channel == "hooked") {
                hooked = event.text == "フックの行" && event.speaker == "ABCD00003000 3000 0.5 2000 nil";
                if (!hooked) {
                    std::printf("hooked: %s / %s\n", event.text.c_str(), event.speaker.c_str());
                }
            } else {
                line = event.text == "テストの行";
            }
        }
        CHECK(line);
        CHECK(hooked);
        CHECK(unaligned);
    }
    CHECK(session.Running());
    CHECK(session.Title() == "Test Game");
    CHECK(Reader::Engine::Instance().GameId() == "TEST-00001");
    CHECK(Reader::Engine::Instance().HookCount() == 1);
    CHECK(Reader::Engine::Instance().MemoryRegions().size() == 1);
    // The program's controller, and a button the reader presses, reach the console.
    core.api->press_button(0, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const std::string pad = Reader::Engine::Instance().ReadMemory(0x4000, 8);
    CHECK(pad.size() == 8 && static_cast<unsigned char>(pad[0]) == 3);
    core.api->press_button(0, 0);
    // Settings as text, by key.
    const reader_core_setting_value values[] = {{"test.speed", "fast"}, {READER_CORE_SETTING_VOLUME, "40"}};
    core.api->apply_settings(values, 2);
    CHECK(MemoryText(0x5000) == "test.speed=fast\nsound.volume=40\n");

    // The save: asked for before the game was loaded, its writes told.
    CHECK(told.WaitFor("written TEST-00001 0"));
    CHECK(told.Index("opening TEST-00001 1") >= 0 && told.Index("opening TEST-00001 1") < told.Index("loaded TEST-00001"));
    CHECK(QFileInfo(QString::fromStdString(opened_path)).fileName() == QStringLiteral("TEST-00001"));
    CHECK(session.SaveLocations("TEST-00001").size() == 1 && session.SaveLocations("TEST-00001")[0].path == opened_path);
    CHECK(session.SaveLocations("OTHER-00002").empty());
    // The second controller, and the first's rumble while B is held.
    CHECK(told.WaitFor("rumble 0 65535 32768"));
    const std::string second = Reader::Engine::Instance().ReadMemory(0x4008, 8);
    CHECK(second.size() == 8 && static_cast<unsigned char>(second[0]) == 8);
    CHECK(session.SetPortDevice(1, 2));
    CHECK(!session.SetPortDevice(0, 2));
    // Discs.
    CHECK(session.DiscCount() == 2 && session.DiscCurrent() == 0 && session.DiscLabel(1) == "Disc 2");
    CHECK(session.InsertDisc(1));
    CHECK(told.WaitFor("disc 1 0"));
    CHECK(session.DiscCurrent() == 1);
    // The pointer, on the lower screen.
    session.Pointer(1, 0, READER_CORE_POINTER_PRESSED, 0.25f, 0.5f);
    {
        const std::string at = Reader::Engine::Instance().ReadMemory(0x7000, 16);
        int32_t values4[4] = {};
        std::memcpy(values4, at.data(), std::min<std::size_t>(at.size(), sizeof(values4)));
        CHECK(values4[0] == 1 && values4[1] == 1 && values4[2] == 250 && values4[3] == 500);
    }
    // Savestates, answered.
    CHECK(session.StateInfo(3).has_value() && !session.StateInfo(3)->exists);
    CHECK(session.SaveState(3));
    CHECK(told.WaitFor("state 0 3 0"));
    CHECK(session.StateInfo(3).has_value() && session.StateInfo(3)->exists && session.StateInfo(3)->saved_ms > 0);
    CHECK(session.LoadState(3));
    CHECK(told.WaitFor("state 1 3 0"));
    CHECK(session.LoadState(4));
    CHECK(told.WaitFor("state 1 4 1 nothing in that slot"));
    // Patches.
    const auto patches = session.Patches("TEST-00001");
    CHECK(patches.size() == 2 && patches[0].name == "widescreen" && patches[0].on_by_default && !patches[1].on_by_default);
    CHECK(session.Patches("OTHER-00002").empty());

    session.Stop();
    CHECK(!session.Running());
    // What the emulator wrote of its own at the end was told, before the game stopped.
    CHECK(told.Index("written TEST-00001 1") > told.Index("written TEST-00001 0"));
    Reader::Engine::Instance().SetEventCallback(nullptr);

    // A real core, where one is named: Station's (READER_HOME_STATION_CORE, built by
    // ../station/tools/reader/build-core.sh). It is loaded beside the test core, its description
    // checked and made the reader's; a game it cannot draw (there is no window here) ends at
    // once and says so. Its games themselves are played with tools/desktop.sh (AGENTS.md).
    if (const QString station = qEnvironmentVariable("READER_HOME_STATION_CORE"); !station.isEmpty()) {
        Home::Core ps1;
        CHECK(Home::LoadCore(station, ps1, error));
        if (ps1.api != nullptr) {
            CHECK(std::strcmp(ps1.info->key, "ps1") == 0);
            CHECK(std::strcmp(ps1.info->app_key, "station-reader") == 0);
            CHECK(ps1.info->cpu.name_count == 66 && ps1.info->cpu.value_count == 34);
            CHECK(ps1.info->button_count == 16 && ps1.info->file_type_count > 0);
            CHECK(ps1.api->grab != nullptr);
            Home::CoreSession ps1_session(ps1);
            const QByteArray ps1_data = folder.filePath(QStringLiteral("ps1")).toUtf8();
            const reader_core_paths ps1_paths{sizeof(reader_core_paths), ps1_data.constData(), ps1_data.constData(),
                                              ps1_data.constData(), ps1_data.constData()};
            CHECK(ps1_session.Start(ps1_paths, session_error));
            CHECK(std::strcmp(Reader::ConsoleTraits::AppKey, "station-reader") == 0);
            CHECK(Reader::ConsoleTraits::ButtonYes == 13);
            CHECK(Reader::Registers::Cpu.hook_alignment == 4);
            CHECK(ps1_session.LoadGame("no-such-disc.cue", session_error));
            for (int i = 0; i < 250 && !ps1_session.Ended(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            CHECK(ps1_session.Ended());
            char word[4];
            CHECK(ps1_session.Api().read_memory(0x80010000, word, sizeof(word)) == 0);
            ps1_session.Stop();
            std::printf("Station's core checked: %s\n", qPrintable(station));
        } else {
            std::printf("FAILED: Station's core did not load (%s)\n", qPrintable(error));
        }
    }
    // Flycast's core (READER_HOME_FLYCAST_CORE, ../flycast/tools/reader/build-core.sh), made from
    // its libretro build with the shared libretro host: its description, and a game it cannot
    // draw (no window here) ending at once.
    if (const QString flycast = qEnvironmentVariable("READER_HOME_FLYCAST_CORE"); !flycast.isEmpty()) {
        Home::Core dc;
        CHECK(Home::LoadCore(flycast, dc, error));
        if (dc.api != nullptr) {
            CHECK(std::strcmp(dc.info->key, "dc") == 0);
            CHECK(std::strcmp(dc.info->app_key, "flycast-reader") == 0);
            CHECK(dc.info->button_yes == 0); // A, as libretro's joypad has it
            CHECK(dc.api->identify != nullptr);
            Home::CoreSession dc_session(dc);
            const QByteArray dc_data = folder.filePath(QStringLiteral("dc")).toUtf8();
            const reader_core_paths dc_paths{sizeof(reader_core_paths), dc_data.constData(), dc_data.constData(),
                                             dc_data.constData(), dc_data.constData()};
            CHECK(dc_session.Start(dc_paths, session_error));
            char id[64] = {}, title[128] = {};
            CHECK(dc.api->identify("no-such-disc.chd", id, sizeof(id), title, sizeof(title)) != 0);
            CHECK(std::strcmp(Reader::ConsoleTraits::AppKey, "flycast-reader") == 0);
            CHECK(dc_session.LoadGame("no-such-disc.chd", session_error));
            for (int i = 0; i < 250 && !dc_session.Ended(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            CHECK(dc_session.Ended());
            dc_session.Stop();
            std::printf("Flycast's core checked: %s\n", qPrintable(flycast));
        } else {
            std::printf("FAILED: Flycast's core did not load (%s)\n", qPrintable(error));
        }
    }
    // Every core in a folder (READER_HOME_CORE_FOLDER: tools/desktop.sh links the built ones into
    // private/desktop/cores), loaded side by side in this one process: each describes itself
    // whole, is started and made the reader's, says a missing file is no game of its, takes a
    // game it cannot load (there is no window here either) and ends it, and stops.
    if (const QString cores = qEnvironmentVariable("READER_HOME_CORE_FOLDER"); !cores.isEmpty()) {
        const QStringList found = Home::FindCores(cores);
        CHECK(!found.isEmpty());
        std::vector<std::unique_ptr<Home::Core>> loaded; // kept loaded until the end, as in the program
        for (const QString& path : found) {
            auto real = std::make_unique<Home::Core>();
            if (!Home::LoadCore(path, *real, error) || real->api == nullptr) {
                std::printf("FAILED: %s did not load (%s)\n", qPrintable(path), qPrintable(error));
                ++failures;
                continue;
            }
            const reader_core_info& info = *real->info;
            const bool described = info.key != nullptr && *info.key != 0 && info.app_key != nullptr &&
                                   info.name != nullptr && info.screen_width > 0 && info.screen_height > 0 &&
                                   info.button_count > 0 && info.file_type_count > 0 && info.cpu.family != nullptr &&
                                   info.setting_count > 0; // registers only where hooks are (not Flycast's)
            CHECK(described);
            CHECK(real->api->identify != nullptr);
            Home::CoreSession real_session(*real);
            const QByteArray data = folder.filePath(QString::fromUtf8(info.key)).toUtf8();
            const reader_core_paths paths{sizeof(reader_core_paths), data.constData(), data.constData(),
                                          data.constData(), data.constData()};
            const bool started = real_session.Start(paths, session_error);
            CHECK(started);
            if (started) {
                CHECK(std::strcmp(Reader::ConsoleTraits::AppKey, info.app_key) == 0);
                const QByteArray missing = folder.filePath(QStringLiteral("no-such-game.") +
                                                           QString::fromUtf8(info.file_types[0])).toUtf8();
                char id[64] = {}, title[128] = {};
                CHECK(real->api->identify(missing.constData(), id, sizeof(id), title, sizeof(title)) != 0);
                if (real_session.LoadGame(missing.constData(), session_error)) {
                    for (int i = 0; i < 500 && !real_session.Ended(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                    CHECK(real_session.Ended());
                }
                real_session.Stop();
            }
            std::printf("%s's core checked (%s, %u settings, %u buttons): %s\n", info.name, info.key,
                        info.setting_count, info.button_count, qPrintable(path));
            loaded.push_back(std::move(real));
        }
    }
    std::printf(failures == 0 ? "All tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
