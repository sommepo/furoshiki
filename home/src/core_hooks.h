// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Games of our cores read by hook (docs/core-hooks.md): what LunaHook (LunaTranslator's) and
// 0xDC00's Agent scripts know of a game on an emulator (an address of the game's code, the
// register its text is in, the encoding, its filters), turned into a hook description our
// engine runs itself (reader.hooks, shared/reader/core/hook_description.inc), with no Frida and
// no other program. Neither project's files are in this repository: they are fetched when the
// user asks (hook_fetch.h: "scripts" for Agent's, "luna-tables" for LunaHook's tables) into
// <shared folder>/program/hooks, and turned here into <shared folder>/program/hooks/core-hooks.json.
// A game read this way gets <ID>.lua in the user's profiles folder: a profile like any other.
//
// Qt Core only: tested by reader-home-tests with made-up tables and scripts; the tool
// core-hooks (tools/core_hooks_main.cpp) converts a folder and counts.

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Home::CoreHooks {

/// One hook of a description (reader.hooks' fields).
struct Hook {
    QString name;
    quint64 address{};
    bool read{};      ///< Memory looked at every frame (LunaHook's DIRECT_READ), not a hook
    bool relative{};  ///< The address counts from the game's executable (base = "main")
    QString from;     ///< The register its text's pointer is in: "r4", "x0"
    bool wide{};      ///< A 64-bit pointer
    qint64 add{};     ///< Added to the pointer
    QString text{QStringLiteral("sjis")}; ///< "sjis", "utf8", "utf16"
    bool whole{};     ///< Each call is a whole line (not pieces of one)
    QList<QStringList> filters; ///< {"cut", what}, {"re", pattern, replacement, flags}…
};

/// What is known of one game: its hooks, from one source.
struct Game {
    QString console;  ///< The library's key: "psp", "ps2", "psvita", "switch"
    QStringList ids;  ///< As our cores give them: "ULJM05887", "SLPM-66302", "0100F3F01D6EE000"
    QString version;  ///< The game's version the addresses are for, where one is said
    QString name;     ///< Its name where the source says one (Agent's @name)
    QString source;   ///< "LunaHook" or "Agent"
    QString from;     ///< The file it was read from
    QList<Hook> hooks;
};

/// What a conversion found, honestly: what converted and why the rest did not.
struct Count {
    int entries{};    ///< Hooks (LunaHook's table rows) or scripts (Agent's) looked at
    int converted{};  ///< Of those, converted
    QMap<QString, int> why_not; ///< Why the others were not, by reason
    QMap<QString, int> games;   ///< Games with at least one hook, by console
};

/// LunaHook's emulator tables: the folder holding ppsspp_1.cpp, PCSX2_1.cpp, vita3k_1.cpp,
/// yuzu_1.cpp (LunaTranslator's src/NativeImpl/LunaHook/LunaHook/emulators).
[[nodiscard]] QList<Game> ConvertLuna(const QString& folder, Count* count = nullptr);
/// 0xDC00's scripts: the folder of PSP_*.js, PSVita_*.js, NS_*.js, PS2_*.js.
[[nodiscard]] QList<Game> ConvertAgent(const QString& folder, Count* count = nullptr);

/// One file's table or script, for tests: `console` is LunaHook's file's ("ppsspp", "PCSX2",
/// "vita3k", "yuzu"); an Agent script's file name says its console.
[[nodiscard]] QList<Game> ConvertLunaSource(const QString& file_console, const QString& source, Count* count = nullptr);
[[nodiscard]] QList<Game> ConvertAgentSource(const QString& file, const QString& source, Count* count = nullptr);

/// Where the descriptions are kept: <shared folder>/program/hooks/core-hooks.json.
[[nodiscard]] QString IndexFile(const QString& shared);
[[nodiscard]] QJsonObject ToJson(const QList<Game>& games);
[[nodiscard]] QList<Game> FromJson(const QJsonObject& json);
/// Converts what was fetched (LunaHook's tables, Agent's scripts) into the index. Returns how
/// many games it knows; `said` is what to tell the user.
int Rebuild(const QString& shared, QString& said);
[[nodiscard]] QList<Game> Load(const QString& shared);
/// The descriptions known for a game of a console, Agent's first (the maintainer: "agent scripts
/// take priority").
[[nodiscard]] QList<Game> Find(const QList<Game>& games, const QString& console, const QString& id);

/// The profile that reads the game by its description: "-- reader: by hook" and reader.hooks{…}.
/// `only`: the hooks in use (all for none).
[[nodiscard]] QString Profile(const Game& game, const QString& id, const QString& title, const QStringList& only = {});
/// Writes it as <ID>.lua in the user's profiles folder. False with `error` when it cannot be, or
/// when a profile that is not a description is there already (a script of ours or the user's).
bool Install(const Game& game, const QString& id, const QString& title, const QString& profiles, QString& error);
/// Whether the user's profile of a game is a description (by hook), which may be replaced.
[[nodiscard]] bool IsDescription(const QString& source);
/// Changes which hooks of an installed description are in use (empty: all).
bool Choose(const QString& profiles, const QString& id, const QStringList& only, QString& error);

} // namespace Home::CoreHooks
