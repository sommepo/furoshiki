// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PCSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "reader/desktop/script_index.h"
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;
using namespace ReaderApp::Scripts;

namespace {

const QByteArray MadeUp = "-- reader: no hooks\n-- reader: title 作り話\n"
                          "-- reader: reads The message window\n"
                          "-- reader: version " + QByteArray(Reader::ConsoleTraits::Test::ScriptGame) +
                          "\nlocal x = 1\n";
const QByteArray MadeUpNewer = MadeUp + "local y = 2\n";
const QByteArray Table = "あいうえお\n";

QJsonObject FileJson(const QString& name, const QByteArray& bytes) {
    return {{QStringLiteral("file"), name},
            {QStringLiteral("sha256"), Sha256(bytes)},
            {QStringLiteral("size"), bytes.size()}};
}

QByteArray IndexJson(const QByteArray& script, bool with_table = false, int api = 1) {
    QJsonObject entry = FileJson((TestScriptGame + QStringLiteral(".lua")), script);
    entry.insert(QStringLiteral("id"), TestScriptGame);
    entry.insert(QStringLiteral("title"), Q(u"作り話"));
    entry.insert(QStringLiteral("reads"), QStringLiteral("The message window"));
    entry.insert(QStringLiteral("version"), TestScriptGame);
    entry.insert(QStringLiteral("api"), api);
    if (with_table) {
        entry.insert(QStringLiteral("data"),
                     QJsonArray{FileJson((TestScriptGame + QStringLiteral(".charset")), Table)});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                     {QStringLiteral("system"), QString::fromLatin1(Reader::ConsoleTraits::ScriptSystem)},
                                     {QStringLiteral("scripts"), QJsonArray{entry}}})
        .toJson();
}

Entry EntryOf(const QByteArray& json) {
    const auto index = ParseIndex(json);
    REQUIRE(index.has_value());
    REQUIRE(index->entries.size() == 1);
    return index->entries.first();
}

void WriteTo(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

} // namespace

TEST_CASE("Scripts: the published list is read", "[reader][scripts]") {
    const Entry entry = EntryOf(IndexJson(MadeUp, true));
    REQUIRE(entry.id == TestScriptGame);
    REQUIRE(entry.title == Q(u"作り話"));
    REQUIRE(entry.script.file == (TestScriptGame + QStringLiteral(".lua")));
    REQUIRE(entry.script.sha256 == Sha256(MadeUp));
    REQUIRE(entry.script.size == MadeUp.size());
    REQUIRE(entry.data.size() == 1);
    REQUIRE(entry.data.first().file == (TestScriptGame + QStringLiteral(".charset")));

    QString error;
    REQUIRE_FALSE(ParseIndex("not json", &error).has_value());
    REQUIRE_FALSE(error.isEmpty());
    // A format from a later app is refused, not half read.
    REQUIRE_FALSE(ParseIndex(R"({"format": 2, "scripts": []})", &error).has_value());

    // An entry whose file is not its game's script, or a path, is passed over.
    QJsonObject root = QJsonDocument::fromJson(IndexJson(MadeUp)).object();
    QJsonArray scripts = root.value(QStringLiteral("scripts")).toArray();
    QJsonObject wrong = scripts.first().toObject();
    wrong.insert(QStringLiteral("file"), (QStringLiteral("../") + TestScriptGame + QStringLiteral(".lua")));
    QJsonObject other = scripts.first().toObject();
    other.insert(QStringLiteral("id"), TestOtherScriptGame);
    root.insert(QStringLiteral("scripts"), QJsonArray{wrong, other});
    const auto index = ParseIndex(QJsonDocument(root).toJson());
    REQUIRE(index.has_value());
    REQUIRE(index->entries.isEmpty());
}

TEST_CASE("Scripts: which files a script may be or ask for", "[reader][scripts]") {
    REQUIRE(IdOfFile((TestScriptGame + QStringLiteral(".lua"))) == TestScriptGame);
    REQUIRE(IdOfFile((TestScriptGame + QStringLiteral(".charset"))) == TestScriptGame);
    REQUIRE(IdOfFile((TestScriptGame + QStringLiteral(".table.bin"))) == TestScriptGame);
    REQUIRE_FALSE(IdOfFile(QStringLiteral(".installed.json")).has_value());
    REQUIRE_FALSE(IdOfFile((TestScriptGame + QStringLiteral("..lua"))).has_value());
    REQUIRE_FALSE(IdOfFile((QStringLiteral("../") + TestScriptGame + QStringLiteral(".lua"))).has_value());
    REQUIRE_FALSE(IdOfFile(TestScriptGame).has_value());
    REQUIRE_FALSE(IdOfFile(QStringLiteral("notes.txt")).has_value());
}

TEST_CASE("Scripts: the API a script needs", "[reader][scripts]") {
    REQUIRE(ApiOf(QString::fromUtf8(MadeUp)) == 1);
    REQUIRE(ApiOf(QStringLiteral("-- reader: title x\n-- reader: api 3\nlocal x\n")) == 3);
    // Only the comments at the top say anything.
    REQUIRE(ApiOf(QStringLiteral("local x\n-- reader: api 3\n")) == 1);
}

TEST_CASE("Scripts: how the list's script compares with what the app has", "[reader][scripts]") {
    const Entry entry = EntryOf(IndexJson(MadeUpNewer));
    const QString old_sha = Sha256(MadeUp);
    REQUIRE(StateOf(entry, std::nullopt, std::nullopt, {}) == State::NotInstalled);
    REQUIRE(StateOf(entry, MadeUpNewer, std::nullopt, {}) == State::Installed);
    REQUIRE(StateOf(entry, std::nullopt, MadeUpNewer, {}) == State::Installed);
    // The app comes with an older one; one installed from the list earlier, unchanged.
    REQUIRE(StateOf(entry, std::nullopt, MadeUp, {}) == State::Update);
    REQUIRE(StateOf(entry, MadeUp, std::nullopt, old_sha) == State::Update);
    // The user's own, or one of the list's that the user changed.
    REQUIRE(StateOf(entry, MadeUp, std::nullopt, {}) == State::Own);
    REQUIRE(StateOf(entry, MadeUp + "-- mine\n", std::nullopt, old_sha) == State::Own);
    REQUIRE(StateOf(entry, MadeUp + "-- mine\n", MadeUpNewer, {}) == State::Own);
    REQUIRE(StateOf(EntryOf(IndexJson(MadeUp, false, Api + 1)), std::nullopt, std::nullopt, {}) ==
            State::TooNew);
}

TEST_CASE("Scripts: installing from the list, updating, removing", "[reader][scripts]") {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/profiles"); // made when first needed
    Folder folder(path);
    const Entry entry = EntryOf(IndexJson(MadeUp, true));
    QString error;

    // A file that is not the one the list names is refused, and nothing is written.
    REQUIRE_FALSE(folder.Install(entry,
                                 {{(TestScriptGame + QStringLiteral(".lua")), MadeUpNewer},
                                  {(TestScriptGame + QStringLiteral(".charset")), Table}},
                                 &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(folder.Script(TestScriptGame).has_value());
    // So is a list without the data file fetched.
    REQUIRE_FALSE(folder.Install(entry, {{(TestScriptGame + QStringLiteral(".lua")), MadeUp}}, &error));

    REQUIRE(folder.Install(entry,
                           {{(TestScriptGame + QStringLiteral(".lua")), MadeUp},
                            {(TestScriptGame + QStringLiteral(".charset")), Table}},
                           &error));
    REQUIRE(folder.Script(TestScriptGame) == MadeUp);
    REQUIRE(QFile::exists(path + (QStringLiteral("/") + TestScriptGame + QStringLiteral(".charset"))));
    REQUIRE(folder.Recorded(TestScriptGame) == Sha256(MadeUp));
    REQUIRE(StateOf(entry, folder.Script(entry.id), std::nullopt, folder.Recorded(entry.id)) ==
            State::Installed);

    // The list moves on: an update, and installing it.
    const Entry newer = EntryOf(IndexJson(MadeUpNewer));
    REQUIRE(StateOf(newer, folder.Script(entry.id), std::nullopt, folder.Recorded(entry.id)) ==
            State::Update);
    REQUIRE(folder.Install(newer, {{(TestScriptGame + QStringLiteral(".lua")), MadeUpNewer}}, &error));
    REQUIRE(folder.Script(entry.id) == MadeUpNewer);

    // Removing takes the script and what came with it from the list.
    REQUIRE(folder.Install(entry,
                           {{(TestScriptGame + QStringLiteral(".lua")), MadeUp},
                            {(TestScriptGame + QStringLiteral(".charset")), Table}},
                           &error));
    REQUIRE(folder.Remove(entry.id));
    REQUIRE_FALSE(folder.Script(entry.id).has_value());
    REQUIRE_FALSE(QFile::exists(path + (QStringLiteral("/") + TestScriptGame + QStringLiteral(".charset"))));
    REQUIRE(folder.Recorded(entry.id).isEmpty());
    REQUIRE_FALSE(folder.Remove(entry.id));
}

TEST_CASE("Scripts: a game's names dictionary comes with its script", "[reader][scripts]") {
    QTemporaryDir dir;
    Folder folder(dir.path());
    const QByteArray names = "PK made-up names";
    QJsonObject index = QJsonDocument::fromJson(IndexJson(MadeUp)).object();
    QJsonArray scripts = index.value(QStringLiteral("scripts")).toArray();
    QJsonObject script = scripts.at(0).toObject();
    script.insert(QStringLiteral("names"), FileJson(QStringLiteral("names-made-up.zip"), names));
    scripts[0] = script;
    index.insert(QStringLiteral("scripts"), scripts);
    const Entry entry = EntryOf(QJsonDocument(index).toJson());
    REQUIRE(entry.names.has_value());
    REQUIRE(entry.names->file == QStringLiteral("names-made-up.zip"));
    REQUIRE(folder.NeedsNames(entry));

    // Without the names fetched, the script goes in all the same.
    QString error;
    REQUIRE(folder.Install(entry, {{(TestScriptGame + QStringLiteral(".lua")), MadeUp}}, &error));
    REQUIRE(folder.NeedsNames(entry));
    // Names that are not the list's are refused; the list's are written.
    REQUIRE_FALSE(folder.InstallNames(entry, "other", &error));
    REQUIRE(folder.InstallNames(entry, names, &error));
    REQUIRE_FALSE(folder.NeedsNames(entry));
    REQUIRE(QFile::exists(dir.path() + QStringLiteral("/names-made-up.zip")));

    // A names file that is not one is passed over; the script is still listed.
    script.insert(QStringLiteral("names"), FileJson(QStringLiteral("../names.zip"), names));
    scripts[0] = script;
    index.insert(QStringLiteral("scripts"), scripts);
    REQUIRE_FALSE(EntryOf(QJsonDocument(index).toJson()).names.has_value());
    REQUIRE(IsNamesFile(QStringLiteral("names-steins-gate.zip")));
    REQUIRE_FALSE(IsNamesFile(QStringLiteral("names-Steins.zip")));
    REQUIRE_FALSE(IsNamesFile(QStringLiteral("ULJM05718.zip")));
}

TEST_CASE("Scripts: installing files the user chose", "[reader][scripts]") {
    QTemporaryDir source;
    QTemporaryDir dir;
    Folder folder(dir.path());
    const QString script = source.path() + (QStringLiteral("/") + TestScriptGame + QStringLiteral(".lua"));
    const QString table = source.path() + (QStringLiteral("/") + TestScriptGame + QStringLiteral(".charset"));
    const QString stray = source.path() + QStringLiteral("/my script.lua");
    const QString binary = source.path() + (QStringLiteral("/") + TestOtherScriptGame + QStringLiteral(".lua"));
    WriteTo(script, MadeUp);
    WriteTo(table, Table);
    WriteTo(stray, MadeUp);
    WriteTo(binary, QByteArray("\xff\xfe\x00\x01", 4));

    // One that came from the list first: choosing a file makes it the user's own.
    const Entry entry = EntryOf(IndexJson(MadeUpNewer));
    QString error;
    REQUIRE(folder.Install(entry, {{(TestScriptGame + QStringLiteral(".lua")), MadeUpNewer}}, &error));

    const QStringList installed = folder.InstallFiles({script, table, stray, binary}, &error);
    REQUIRE(installed == QStringList{TestScriptGame});
    REQUIRE(error.contains(QStringLiteral("my script.lua")));
    REQUIRE(error.contains((TestOtherScriptGame + QStringLiteral(".lua"))));
    REQUIRE(folder.Script(TestScriptGame) == MadeUp);
    REQUIRE(QFile::exists(dir.path() + (QStringLiteral("/") + TestScriptGame + QStringLiteral(".charset"))));
    REQUIRE_FALSE(QFile::exists(dir.path() + (QStringLiteral("/") + TestOtherScriptGame + QStringLiteral(".lua"))));
    REQUIRE(folder.Recorded(TestScriptGame).isEmpty());
    REQUIRE(StateOf(entry, folder.Script(entry.id), std::nullopt, folder.Recorded(entry.id)) ==
            State::Own);
}
