// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// core-hooks: converts LunaHook's emulator tables and 0xDC00's Agent scripts into hook
// descriptions (core_hooks.h) and says what converted and why the rest did not.
//   core-hooks count LUNA_EMULATORS_FOLDER AGENT_SCRIPTS_FOLDER
//   core-hooks profile LUNA_EMULATORS_FOLDER AGENT_SCRIPTS_FOLDER CONSOLE ID   (every description of a game)
//   core-hooks why LUNA AGENT ID                                         (why its LunaHook rows did not convert)
//   core-hooks json LUNA_EMULATORS_FOLDER AGENT_SCRIPTS_FOLDER OUT.json

#include <cstdio>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include "core_hooks.h"

using namespace Home::CoreHooks;

namespace {
void Say(const char* what, const Count& count) {
    std::printf("%s: %d of %d converted\n", what, count.converted, count.entries);
    for (auto it = count.games.cbegin(); it != count.games.cend(); ++it) {
        std::printf("  games on %s: %d\n", qPrintable(it.key()), it.value());
    }
    for (auto it = count.why_not.cbegin(); it != count.why_not.cend(); ++it) {
        std::printf("  not: %-60s %d\n", qPrintable(it.key()), it.value());
    }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4) {
        std::fprintf(stderr, "core-hooks count|profile|json LUNA AGENT [...]\n");
        return 64;
    }
    Count luna, agent;
    QList<Game> games = ConvertAgent(args[3], &agent);
    games += ConvertLuna(args[2], &luna);
    if (args[1] == QStringLiteral("count")) {
        Say("LunaHook (hooks)", luna);
        Say("Agent (scripts)", agent);
        return 0;
    }
    if (args[1] == QStringLiteral("profile") && args.size() >= 6) {
        for (const Game& game : Find(games, args[4], args[5])) {
            std::printf("%s\n", Profile(game, args[5], game.name).toUtf8().constData());
        }
        return 0;
    }
    if (args[1] == QStringLiteral("why") && args.size() >= 5) {
        // Why a game's rows did not convert: each table with only that game's rows left in it.
        for (const QString& name : {QStringLiteral("ppsspp_1"), QStringLiteral("PCSX2_1"), QStringLiteral("vita3k_1"), QStringLiteral("yuzu_1")}) {
            QFile in(args[2] + u'/' + name + QStringLiteral(".cpp"));
            if (!in.open(QIODevice::ReadOnly)) {
                continue;
            }
            QStringList kept;
            for (const QString& line : QString::fromUtf8(in.readAll()).split(u'\n')) {
                if (!line.trimmed().startsWith(QStringLiteral("{0x")) || line.contains(args[4])) {
                    kept.append(line);
                }
            }
            Count count;
            ConvertLunaSource(name, kept.join(u'\n'), &count);
            if (count.entries > 0) {
                Say(qPrintable(name), count);
            }
        }
        return 0;
    }
    if (args[1] == QStringLiteral("json") && args.size() >= 5) {
        QFile out(args[4]);
        return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(ToJson(games)).toJson()) > 0 ? 0 : 1;
    }
    return 64;
}
