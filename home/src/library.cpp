// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include "library.h"

namespace Home {

namespace {

/// The consoles Furoshiki knows, in the order they are shown, with their labels. An app's
/// own "order" and "short" come first; a console known to neither goes after these, by name.
struct KnownSystem {
    const char* name;
    const char* label;
};
constexpr KnownSystem KnownSystems[] = {
    {"Nintendo Switch", "SWITCH"}, {"Nintendo 3DS", "3DS"},        {"PlayStation", "PS1"},
    {"PlayStation Portable", "PSP"}, {"PlayStation Vita", "VITA"}, {"PlayStation 2", "PS2"},
    {"Dreamcast", "DREAMCAST"},
};
constexpr int KnownCount = static_cast<int>(sizeof(KnownSystems) / sizeof(KnownSystems[0]));

int SystemOrder(const App& app) {
    if (app.order >= 0) {
        return app.order;
    }
    for (int i = 0; i < KnownCount; ++i) {
        if (app.system == QLatin1String(KnownSystems[i].name)) {
            return (i + 1) * 10;
        }
    }
    return (KnownCount + 1) * 10;
}

QStringList Strings(const QJsonValue& value) {
    QStringList out;
    for (const QJsonValue& item : value.toArray()) {
        out.append(item.toString());
    }
    return out;
}

} // namespace

QString SharedFolder() {
    const QString set = qEnvironmentVariable("READER_HOME_DIR");
    if (!set.isEmpty()) {
        return set;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/reader-home");
}

Library Load(const QString& folder) {
    Library library;
    QMap<QString, Day> days;
    const QDir apps(folder + QStringLiteral("/apps"));
    for (const QFileInfo& info : apps.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            library.problems.append(QStringLiteral("%1: cannot be opened").arg(info.fileName()));
            continue;
        }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        const QJsonObject root = document.object();
        if (error.error != QJsonParseError::NoError || root.value(QStringLiteral("app")).toString().isEmpty()) {
            library.problems.append(QStringLiteral("%1: not a reader app's file").arg(info.fileName()));
            continue;
        }
        if (root.value(QStringLiteral("format")).toInt(1) > AppFileFormat) {
            library.problems.append(QStringLiteral("%1: written by a newer reader app (format %2): update Furoshiki")
                                        .arg(info.fileName())
                                        .arg(root.value(QStringLiteral("format")).toInt()));
            continue;
        }
        App app;
        app.app = root.value(QStringLiteral("app")).toString();
        app.name = root.value(QStringLiteral("name")).toString(app.app);
        app.system = root.value(QStringLiteral("system")).toString();
        app.program = root.value(QStringLiteral("program")).toString();
        app.game_args = Strings(root.value(QStringLiteral("game_args")));
        if (app.game_args.isEmpty()) {
            app.game_args = {QStringLiteral("%ROM%")};
        }
        app.menu_args = Strings(root.value(QStringLiteral("menu_args")));
        app.fullscreen_args = Strings(root.value(QStringLiteral("fullscreen_args")));
        app.short_name = root.value(QStringLiteral("short")).toString();
        app.order = root.value(QStringLiteral("order")).toInt(-1);
        app.program_found = !app.program.isEmpty() && QFileInfo(app.program).isFile();
        app.written = static_cast<qint64>(root.value(QStringLiteral("written")).toDouble());

        // The statistics: every reading day of every game.
        struct Read {
            qint64 chars{}, active_ms{};
            QString last_day;
        };
        QMap<QString, Read> read;
        const QJsonObject stats = root.value(QStringLiteral("stats")).toObject();
        for (const QJsonValue& value : stats.value(QStringLiteral("days")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString day = entry.value(QStringLiteral("day")).toString();
            const qint64 chars = static_cast<qint64>(entry.value(QStringLiteral("chars")).toDouble());
            const qint64 active = static_cast<qint64>(entry.value(QStringLiteral("active_ms")).toDouble());
            if (day.isEmpty()) {
                continue;
            }
            Read& game = read[entry.value(QStringLiteral("game")).toString()];
            game.chars += chars;
            game.active_ms += active;
            game.last_day = std::max(game.last_day, day);
            Day& total = days[day];
            total.day = day;
            total.chars += chars;
            total.active_ms += active;
            app.chars += chars;
            app.active_ms += active;
        }
        for (const QJsonValue& value : root.value(QStringLiteral("games")).toArray()) {
            const QJsonObject entry = value.toObject();
            // The apps list what they can read; one that says otherwise of a game is believed.
            if (!entry.value(QStringLiteral("readable")).toBool(true)) {
                continue;
            }
            Game game;
            game.app = app.app;
            game.system = app.system;
            game.id = entry.value(QStringLiteral("id")).toString();
            game.path = entry.value(QStringLiteral("path")).toString();
            game.title = entry.value(QStringLiteral("title")).toString();
            if (game.title.isEmpty()) {
                game.title = game.path.isEmpty() ? game.id : QFileInfo(game.path).completeBaseName();
            }
            game.cover = entry.value(QStringLiteral("cover")).toString();
            if (game.id.isEmpty() || (game.path.isEmpty() && NeedsFile(app))) {
                continue;
            }
            const auto found = read.constFind(game.id);
            if (found != read.constEnd()) {
                game.chars = found->chars;
                game.active_ms = found->active_ms;
                game.last_day = found->last_day;
            }
            app.games.append(game);
        }
        // Two files of one name (a game's discs, two copies): each says which it is.
        QMap<QString, int> named;
        for (const Game& game : app.games) {
            ++named[game.title];
        }
        for (Game& game : app.games) {
            if (named.value(game.title) > 1) {
                const QString file = game.path.isEmpty() ? game.id : QFileInfo(game.path).completeBaseName();
                static const QRegularExpression disc(QStringLiteral("\\((Disc|Disk|CD) ?(\\d+)\\)"),
                                                     QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch match = disc.match(file);
                game.title += match.hasMatch() ? QStringLiteral(" · Disc %1").arg(match.captured(2))
                                               : QStringLiteral(" · %1").arg(file);
            }
        }
        library.chars += app.chars;
        library.active_ms += app.active_ms;
        library.games.append(app.games);
        library.apps.append(app);
    }
    std::stable_sort(library.apps.begin(), library.apps.end(), [](const App& a, const App& b) {
        const int x = SystemOrder(a), y = SystemOrder(b);
        if (x != y) {
            return x < y;
        }
        return a.system != b.system ? a.system < b.system : a.name < b.name;
    });
    std::stable_sort(library.games.begin(), library.games.end(), [](const Game& a, const Game& b) {
        if (a.last_day != b.last_day) {
            return a.last_day > b.last_day; // read lately first; never read ("") last
        }
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    library.days = days.values();
    return library;
}

QJsonObject LoadShared(const QString& folder) {
    QFile file(folder + QStringLiteral("/settings.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool SaveShared(const QString& folder, const QJsonObject& settings) {
    if (!QDir().mkpath(folder)) {
        return false;
    }
    QSaveFile file(folder + QStringLiteral("/settings.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool NeedsFile(const App& app) {
    for (const QString& argument : app.game_args) {
        if (argument.contains(QStringLiteral("%ROM%"))) {
            return true;
        }
    }
    return false;
}

QStringList WithoutFullScreen(const App& app, QStringList arguments) {
    // The app's own words for full screen, taken out where they stand together ("-config
    // window:fullscreen=yes" is two words that only mean it side by side).
    const QStringList& own = app.fullscreen_args;
    if (!own.isEmpty()) {
        for (int at = 0; at + own.size() <= arguments.size();) {
            if (arguments.mid(at, own.size()) == own) {
                arguments.remove(at, own.size());
            } else {
                ++at;
            }
        }
    }
    // And the words the apps have used for it.
    for (const char* full : {"-f", "--fullscreen", "-fullscreen"}) {
        arguments.removeAll(QLatin1String(full));
    }
    return arguments;
}

QStringList GameArguments(const App& app, const Game& game, bool windowed) {
    QStringList given = app.game_args;
    if (windowed) {
        given = WithoutFullScreen(app, given);
    } else if (!app.fullscreen_args.isEmpty() && WithoutFullScreen(app, given).size() == given.size()) {
        // Its own words, unless its game arguments have them: before the game's own arguments,
        // as after a "--" a program reads no more options.
        given = app.fullscreen_args + given;
    }
    QStringList out;
    for (QString argument : given) {
        argument.replace(QStringLiteral("%ROM%"), game.path);
        argument.replace(QStringLiteral("%ID%"), game.id);
        out.append(argument);
    }
    return out;
}

QString ShortSystem(const Library& library, const QString& system) {
    for (const App& app : library.apps) {
        if (app.system == system && !app.short_name.isEmpty()) {
            return app.short_name;
        }
    }
    for (const KnownSystem& known : KnownSystems) {
        if (system == QLatin1String(known.name)) {
            return QLatin1String(known.label);
        }
    }
    return system.toUpper();
}

QList<Game> DistinctGames(const QList<Game>& games) {
    QList<Game> out;
    QSet<QString> seen;
    for (const Game& game : games) {
        const QString key = game.app + QLatin1Char('\n') + game.id;
        if (!seen.contains(key)) {
            seen.insert(key);
            out.append(game);
        }
    }
    return out;
}

QString TimeText(qint64 active_ms) {
    const qint64 minutes = active_ms / 60000;
    if (minutes < 1) {
        return active_ms > 0 ? QStringLiteral("under a minute") : QStringLiteral("not yet");
    }
    if (minutes < 60) {
        return QStringLiteral("%1m").arg(minutes);
    }
    return QStringLiteral("%1h %2m").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString CountText(qint64 count) {
    return QLocale(QLocale::English).toString(count);
}

} // namespace Home
