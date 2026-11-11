// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hook_scripts.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace Home::Hooks::Agent {

namespace {
QString ReadText(const QString& file, qint64 most = -1) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(most < 0 ? in.readAll() : in.read(most));
}

QString PlatformOf(const QString& file) {
    static const QList<QPair<QString, QString>> known{
        {QStringLiteral("NS_"), QStringLiteral("SWITCH")},   {QStringLiteral("PSVita_"), QStringLiteral("PS VITA")}, {QStringLiteral("PSP_"), QStringLiteral("PSP")},
        {QStringLiteral("PS2_"), QStringLiteral("PS2")},     {QStringLiteral("PS3_"), QStringLiteral("PS3")},        {QStringLiteral("3DS_"), QStringLiteral("3DS")},
        {QStringLiteral("PC98_"), QStringLiteral("PC-98")},  {QStringLiteral("PC_"), QStringLiteral("PC")},          {QStringLiteral("Android_"), QStringLiteral("ANDROID")},
    };
    for (const auto& [head, name] : known) {
        if (file.startsWith(head, Qt::CaseInsensitive)) {
            return name;
        }
    }
    return QStringLiteral("OTHER");
}

QString StorageFile(const QString& shared) {
    return shared + QStringLiteral("/program/hooks/storage.json");
}
} // namespace

Script ReadHeader(const QString& file, const QString& source) {
    Script script;
    script.file = file;
    script.platform = PlatformOf(file);
    static const QRegularExpression tag(QStringLiteral("^\\s*//\\s*@(\\w+)\\s+(.*?)\\s*$"), QRegularExpression::MultilineOption);
    const qsizetype end = source.indexOf(QStringLiteral("==/UserScript=="));
    auto each = tag.globalMatch(end > 0 ? source.left(end) : source.left(2000));
    while (each.hasNext()) {
        const QRegularExpressionMatch match = each.next();
        const QString key = match.captured(1), value = match.captured(2);
        if (key == QStringLiteral("name") && script.name.isEmpty()) {
            // "[01002AE00F442000] Flowers": the ID the game has on its console, then its name.
            static const QRegularExpression id(QStringLiteral("^\\[([^\\]]+)\\]\\s*(.*)$"));
            const QRegularExpressionMatch with_id = id.match(value);
            script.id = with_id.hasMatch() ? with_id.captured(1).trimmed() : QString();
            script.name = with_id.hasMatch() && !with_id.captured(2).isEmpty() ? with_id.captured(2).trimmed() : value;
        } else if (key == QStringLiteral("version") && script.version.isEmpty()) {
            script.version = value;
        } else if (key == QStringLiteral("author") && script.author.isEmpty()) {
            script.author = QString(value).remove(QLatin1Char('[')).remove(QLatin1Char(']'));
        } else if (key == QStringLiteral("description") && script.runs_on.isEmpty()) {
            script.runs_on = value;
        }
    }
    return script;
}

QList<Script> List(const QString& folder) {
    QList<Script> scripts;
    const QDir dir(folder);
    for (const QString& file : dir.entryList({QStringLiteral("*.js")}, QDir::Files)) {
        if (file.startsWith(QStringLiteral("lib"), Qt::CaseInsensitive) || file.startsWith(QLatin1Char('_'))) {
            continue;
        }
        const Script script = ReadHeader(file, ReadText(dir.filePath(file), 2000));
        if (!script.name.isEmpty()) {
            scripts.append(script);
        }
    }
    std::sort(scripts.begin(), scripts.end(), [](const Script& a, const Script& b) {
        const int by_name = a.name.compare(b.name, Qt::CaseInsensitive);
        return by_name != 0 ? by_name < 0 : a.file < b.file;
    });
    return scripts;
}

QList<Script> Search(const QList<Script>& scripts, const QString& typed) {
    const QStringList words = typed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return scripts;
    }
    QList<Script> found;
    for (const Script& script : scripts) {
        const QString all = script.name + QLatin1Char(' ') + script.id + QLatin1Char(' ') + script.platform + QLatin1Char(' ') + script.file + QLatin1Char(' ') + script.runs_on;
        if (std::all_of(words.begin(), words.end(), [&](const QString& word) { return all.contains(word, Qt::CaseInsensitive); })) {
            found.append(script);
        }
    }
    return found;
}

namespace {
/// Letters and figures only, folded: "Chaos;Child" and "chaoschild" alike.
QString Compact(const QString& text) {
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_KC).toCaseFolded()) {
        if (c.isLetterOrNumber()) {
            out.append(c);
        }
    }
    return out;
}

/// The words of a name that say which game it is.
QSet<QString> WordsOf(const QString& text) {
    static const QSet<QString> plain{QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("game"), QStringLiteral("launcher"), QStringLiteral("x64"),
                                     QStringLiteral("x86"), QStringLiteral("win"), QStringLiteral("win32"), QStringLiteral("win64"), QStringLiteral("windows"),
                                     QStringLiteral("steam"), QStringLiteral("exe"), QStringLiteral("bin"), QStringLiteral("app"), QStringLiteral("data"),
                                     QStringLiteral("common"), QStringLiteral("steamapps"), QStringLiteral("program"), QStringLiteral("files"), QStringLiteral("dmm"),
                                     QStringLiteral("edition"), QStringLiteral("version"), QStringLiteral("ver"), QStringLiteral("for"), QStringLiteral("of")};
    QSet<QString> words;
    // A name's words, its camel case parted too: "ChaosChild" -> chaos, child.
    QString spaced;
    const QString normal = text.normalized(QString::NormalizationForm_KC);
    for (int i = 0; i < normal.size(); ++i) {
        if (i > 0 && normal[i].isUpper() && normal[i - 1].isLower()) {
            spaced.append(QLatin1Char(' '));
        }
        spaced.append(normal[i].isLetterOrNumber() ? normal[i] : QLatin1Char(' '));
    }
    for (const QString& word : spaced.toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if ((word.size() >= 3 || (word.size() >= 1 && word.at(0).unicode() >= 0x2E80)) && !plain.contains(word)) {
            words.insert(word);
        }
    }
    return words;
}

/// The console an emulator plays, as the scripts' platforms are named; empty for a PC game.
QString EmulatorPlatform(const QString& program) {
    static const QList<QPair<QString, QString>> known{
        {QStringLiteral("yuzu"), QStringLiteral("SWITCH")},   {QStringLiteral("suyu"), QStringLiteral("SWITCH")},     {QStringLiteral("sudachi"), QStringLiteral("SWITCH")},
        {QStringLiteral("citron"), QStringLiteral("SWITCH")}, {QStringLiteral("ryujinx"), QStringLiteral("SWITCH")},  {QStringLiteral("eden"), QStringLiteral("SWITCH")},
        {QStringLiteral("ppsspp"), QStringLiteral("PSP")},    {QStringLiteral("pcsx2"), QStringLiteral("PS2")},       {QStringLiteral("vita3k"), QStringLiteral("PS VITA")},
        {QStringLiteral("rpcs3"), QStringLiteral("PS3")},     {QStringLiteral("citra"), QStringLiteral("3DS")},       {QStringLiteral("azahar"), QStringLiteral("3DS")},
        {QStringLiteral("lime3ds"), QStringLiteral("3DS")},
    };
    const QString name = program.toLower();
    for (const auto& [head, platform] : known) {
        if (name.startsWith(head)) {
            return platform;
        }
    }
    return {};
}
} // namespace

QList<Script> Suggest(const QList<Script>& scripts, const QString& program, const QString& path, const QString& window, int most) {
    const QString emulator = EmulatorPlatform(program);
    // The game's IDs its window shows: a Switch title's sixteen figures, a Sony disc's "ULJM05887".
    QSet<QString> ids;
    static const QRegularExpression switch_id(QStringLiteral("\\b([0-9A-Fa-f]{16})\\b")), disc_id(QStringLiteral("\\b([A-Z]{4})[-_ ]?(\\d{5})\\b"));
    for (auto each = switch_id.globalMatch(window); each.hasNext();) {
        ids.insert(each.next().captured(1).toUpper());
    }
    for (auto each = disc_id.globalMatch(window); each.hasNext();) {
        const auto match = each.next();
        ids.insert(match.captured(1) + match.captured(2));
    }
    // What says the game's name: its file, its folders (not the plain ones), its window's title.
    QStringList names{QFileInfo(program).completeBaseName(), window};
    const QStringList parts = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = static_cast<int>(parts.size()) - 2, taken = 0; i >= 0 && taken < 2; --i) {
        if (WordsOf(parts[i]).isEmpty()) {
            continue;
        }
        names.append(parts[i]);
        ++taken;
        if (parts.value(i - 1).compare(QStringLiteral("common"), Qt::CaseInsensitive) == 0) {
            break; // steamapps/common/<the game>: that is its name
        }
    }
    QSet<QString> words;
    QStringList compacts;
    for (const QString& name : names) {
        words.unite(WordsOf(name));
        if (const QString c = Compact(name); c.size() >= 4) {
            compacts.append(c);
        }
    }
    QList<QPair<int, Script>> scored;
    for (const Script& script : scripts) {
        // A PC game's: the PC's scripts and those of no console; an emulator's: its console's.
        if (emulator.isEmpty() ? script.platform != QStringLiteral("PC") && script.platform != QStringLiteral("OTHER") : script.platform != emulator) {
            continue;
        }
        if (script.file.compare(QStringLiteral("PC_AIO.js"), Qt::CaseInsensitive) == 0) {
            continue; // the one that tries every game
        }
        int score = 0;
        if (!script.id.isEmpty() && ids.contains(QString(script.id).remove(QLatin1Char('-')).toUpper())) {
            score += 100;
        }
        const QString own = Compact(script.name);
        for (const QString& c : compacts) {
            if (c == own) {
                score += 50;
            } else if (own.size() >= 5 && (c.contains(own) || own.contains(c))) {
                score += 20;
            }
        }
        const QSet<QString> theirs = WordsOf(script.name) + WordsOf(QString(script.file).section(QLatin1Char('.'), 0, -2));
        const int shared = static_cast<int>((theirs & words).size());
        score += shared * 6;
        if (score >= 12) {
            scored.append({score, script});
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QList<Script> found;
    for (int i = 0; i < scored.size() && i < most; ++i) {
        found.append(scored[i].second);
    }
    return found;
}

bool HasRuntime(const QString& folder) {
    return QFileInfo::exists(folder + QLatin1Char('/') + Runtime);
}

QString CleanPath(const QString& path) {
    QStringList out;
    for (const QString& part : QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')).split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QStringLiteral(".")) {
            continue;
        }
        if (part == QStringLiteral("..")) {
            if (out.isEmpty()) {
                return {};
            }
            out.removeLast();
        } else {
            out.append(part);
        }
    }
    return out.join(QLatin1Char('/'));
}

QStringList Requires(const QString& source) {
    QStringList files;
    static const QRegularExpression call(QStringLiteral("require\\(\\s*(['\"`])(\\.{1,2}/[^'\"`]+)\\1\\s*\\)"));
    auto each = call.globalMatch(source);
    while (each.hasNext()) {
        const QString file = each.next().captured(2);
        if (!files.contains(file)) {
            files.append(file);
        }
    }
    return files;
}

QString Bundle(const QString& folder, const QString& script, const QString& host, const QJsonObject& storage, QString& error) {
    const QDir dir(folder);
    const QString mission = CleanPath(script);
    if (mission.isEmpty() || !QFileInfo(dir.filePath(mission)).isFile()) {
        error = QStringLiteral("The script %1 is not among the fetched scripts").arg(script);
        return {};
    }
    const QString runtime = ReadText(dir.filePath(Runtime));
    if (runtime.isEmpty()) {
        error = QStringLiteral("Agent's runtime (%1) is not among the fetched scripts: fetch them again").arg(Runtime);
        return {};
    }
    // The script and everything it requires, each under its path within the folder. A file
    // asked for as "./x" may be x, x.js or x/index.js, as the runtime looks for it.
    QJsonObject files;
    QStringList todo{mission, QStringLiteral("libInit_global.js")};
    QSet<QString> seen;
    while (!todo.isEmpty()) {
        const QString name = todo.takeFirst();
        if (name.isEmpty() || seen.contains(name)) {
            continue;
        }
        seen.insert(name);
        const QFileInfo info(dir.filePath(name));
        if (!info.isFile() || info.size() > 4 * 1024 * 1024) {
            continue;
        }
        const QString source = ReadText(info.filePath());
        files.insert(name, source);
        if (!name.endsWith(QStringLiteral(".js")) && !name.endsWith(QStringLiteral(".cjs"))) {
            continue;
        }
        const QString base = name.contains(QLatin1Char('/')) ? name.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/') : QString();
        for (const QString& wanted : Requires(source)) {
            const QString plain = CleanPath(base + wanted);
            todo << plain << plain + QStringLiteral(".js") << plain + QStringLiteral(".json") << plain + QStringLiteral("/index.js");
        }
    }
    const QJsonObject stores{{QStringLiteral("sessionStorage"), storage.value(QStringLiteral("sessionStorage")).toObject()},
                             {QStringLiteral("localStorage"), storage.value(QStringLiteral("localStorage")).toObject()}};
    QString out;
    out.reserve(runtime.size() + host.size() + 65536);
    out += QStringLiteral("const FILES = ") + QString::fromUtf8(QJsonDocument(files).toJson(QJsonDocument::Compact)) + QStringLiteral(";\n");
    out += QStringLiteral("const MISSION = ") + QString::fromUtf8(QJsonDocument(QJsonArray{mission}).toJson(QJsonDocument::Compact)) + QStringLiteral("[0];\n");
    out += QStringLiteral("const STORAGE = ") + QString::fromUtf8(QJsonDocument(stores).toJson(QJsonDocument::Compact)) + QStringLiteral(";\n");
    out += host;
    // Agent's runtime, with Furoshiki's stand-ins as its send and recv.
    out += QStringLiteral("\n(function (send, recv) {\n") + runtime + QStringLiteral("\n}).call(globalThis, __furoshiki.send, __furoshiki.recv);\n");
    return out;
}

QJsonObject LoadStorage(const QString& shared, const QString& title, qint64 pid) {
    QFile in(StorageFile(shared));
    const QJsonObject all = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
    QJsonObject mine = all.value(title).toObject();
    if (mine.value(QStringLiteral("pid")).toInteger() != pid) {
        mine.insert(QStringLiteral("sessionStorage"), QJsonObject());
    }
    mine.insert(QStringLiteral("pid"), pid);
    return mine;
}

void ApplyStorage(QJsonObject& storage, const QString& which, const QString& op, const QStringList& args) {
    if (which != QStringLiteral("sessionStorage") && which != QStringLiteral("localStorage")) {
        return;
    }
    QJsonObject store = storage.value(which).toObject();
    if (op == QStringLiteral("setItem") && args.size() >= 2) {
        store.insert(args[0], args[1]);
    } else if (op == QStringLiteral("removeItem") && !args.isEmpty()) {
        store.remove(args[0]);
    } else if (op == QStringLiteral("clear")) {
        store = QJsonObject();
    }
    storage.insert(which, store);
}

bool SaveStorage(const QString& shared, const QString& title, qint64 pid, const QJsonObject& storage) {
    const QString file = StorageFile(shared);
    QFile in(file);
    QJsonObject all = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
    in.close();
    QJsonObject mine = storage;
    mine.insert(QStringLiteral("pid"), pid);
    all.insert(title, mine);
    QDir().mkpath(QFileInfo(file).path());
    QSaveFile out(file);
    return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(all).toJson(QJsonDocument::Compact)) > 0 && out.commit();
}

} // namespace Home::Hooks::Agent
