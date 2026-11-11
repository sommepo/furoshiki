// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "line_titles.h"

#include <cmath>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>
#include "line_feed.h"

namespace Home::Lines {

namespace {
bool IsWay(const QString& by) {
    return by == QStringLiteral("websocket") || by == QStringLiteral("clipboard") || by == QStringLiteral("ocr") || by == QStringLiteral("hook");
}

bool IsEngine(const QString& engine) {
    return engine == QStringLiteral("agent") || engine == QStringLiteral("luna") || engine == QStringLiteral("textractor");
}

QStringList Strings(const QJsonValue& value) {
    QStringList out;
    for (const QJsonValue& one : value.toArray()) {
        if (!one.toString().isEmpty()) {
            out.append(one.toString());
        }
    }
    return out;
}

QJsonObject Read(const QString& file) {
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
}

bool Write(const QString& file, const QJsonObject& root) {
    QDir().mkpath(QFileInfo(file).path());
    QSaveFile out(file);
    return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(root).toJson()) > 0 && out.commit();
}
} // namespace

Sources Load(const QString& folder) {
    Sources sources;
    const QJsonObject root = Read(folder + QStringLiteral("/program/sources.json"));
    const QString address = root.value(QStringLiteral("address")).toString().trimmed();
    if (!address.isEmpty()) {
        sources.address = address;
    }
    const QJsonObject helper = root.value(QStringLiteral("ocr")).toObject();
    sources.ocr.address = CleanHelperAddress(helper.value(QStringLiteral("address")).toString());
    sources.ocr.token = helper.value(QStringLiteral("token")).toString();
    sources.ocr.program = helper.value(QStringLiteral("program")).toString();
    if (const QString engine = helper.value(QStringLiteral("engine")).toString(); OcrEngines.contains(engine)) {
        sources.ocr.engine = engine;
    }
    if (const QString source = helper.value(QStringLiteral("source")).toString().trimmed(); !source.isEmpty()) {
        sources.ocr.source = source;
    }
    for (const QJsonValue& value : root.value(QStringLiteral("titles")).toArray()) {
        const QJsonObject entry = value.toObject();
        Title title{entry.value(QStringLiteral("id")).toString().toUpper(), entry.value(QStringLiteral("title")).toString(),
                    entry.value(QStringLiteral("by")).toString(), entry.value(QStringLiteral("window")).toString(), {}};
        const QJsonArray area = entry.value(QStringLiteral("area")).toArray();
        if (area.size() == 4) {
            title.area = CleanArea(QRectF(area[0].toDouble(), area[1].toDouble(), area[2].toDouble(), area[3].toDouble()));
        }
        if (title.by == QStringLiteral("ocr") && !title.area.isValid()) {
            continue; // a window read by OCR is nothing without its area
        }
        for (const QJsonValue& other : entry.value(QStringLiteral("areas")).toArray()) {
            const QJsonArray rect = other.toObject().value(QStringLiteral("rect")).toArray();
            const QRectF box = rect.size() == 4 ? CleanArea(QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble())) : QRectF();
            if (box.isValid()) {
                title.more.append({box, other.toObject().value(QStringLiteral("read")).toString() == QStringLiteral("ask")});
            }
        }
        if (title.by == QStringLiteral("hook")) {
            title.engine = entry.value(QStringLiteral("engine")).toString();
            title.script = entry.value(QStringLiteral("script")).toString();
            title.process = entry.value(QStringLiteral("process")).toString();
            title.codes = Strings(entry.value(QStringLiteral("codes")));
            title.threads = Strings(entry.value(QStringLiteral("threads")));
            title.hook_auto = entry.value(QStringLiteral("auto")).toBool(true);
            title.path = entry.value(QStringLiteral("path")).toString();
            if (!IsEngine(title.engine) || title.process.isEmpty() || (title.engine == QStringLiteral("agent") && title.script.isEmpty())) {
                continue; // a hook is nothing without its engine and the program it is put in
            }
        }
        title.console = entry.value(QStringLiteral("console")).toString();
        if (!title.id.isEmpty() && !title.title.isEmpty() && IsWay(title.by)) {
            sources.titles.append(title);
        }
    }
    return sources;
}

bool Save(const QString& folder, const Sources& sources) {
    QJsonArray titles;
    for (const Title& title : sources.titles) {
        QJsonObject entry{{QStringLiteral("id"), title.id}, {QStringLiteral("title"), title.title}, {QStringLiteral("by"), title.by}};
        if (title.by == QStringLiteral("ocr")) {
            entry.insert(QStringLiteral("window"), title.window);
            entry.insert(QStringLiteral("area"), QJsonArray{title.area.x(), title.area.y(), title.area.width(), title.area.height()});
            QJsonArray areas;
            for (const Box& box : title.more) {
                areas.append(QJsonObject{{QStringLiteral("rect"), QJsonArray{box.rect.x(), box.rect.y(), box.rect.width(), box.rect.height()}},
                                         {QStringLiteral("read"), box.ask ? QStringLiteral("ask") : QStringLiteral("always")}});
            }
            if (!areas.isEmpty()) {
                entry.insert(QStringLiteral("areas"), areas);
            }
        }
        if (title.by == QStringLiteral("hook")) {
            entry.insert(QStringLiteral("engine"), title.engine);
            entry.insert(QStringLiteral("process"), title.process);
            if (!title.script.isEmpty()) {
                entry.insert(QStringLiteral("script"), title.script);
            }
            if (!title.codes.isEmpty()) {
                entry.insert(QStringLiteral("codes"), QJsonArray::fromStringList(title.codes));
            }
            if (!title.threads.isEmpty()) {
                entry.insert(QStringLiteral("threads"), QJsonArray::fromStringList(title.threads));
            }
            entry.insert(QStringLiteral("auto"), title.hook_auto);
            if (!title.path.isEmpty()) {
                entry.insert(QStringLiteral("path"), title.path);
            }
        }
        if (!title.console.isEmpty()) {
            entry.insert(QStringLiteral("console"), title.console);
        }
        titles.append(entry);
    }
    QJsonObject helper{{QStringLiteral("address"), sources.ocr.address},
                       {QStringLiteral("source"), sources.ocr.source},
                       {QStringLiteral("token"), sources.ocr.token}};
    if (!sources.ocr.program.isEmpty()) {
        helper.insert(QStringLiteral("program"), sources.ocr.program);
    }
    if (!sources.ocr.engine.isEmpty()) {
        helper.insert(QStringLiteral("engine"), sources.ocr.engine);
    }
    return Write(folder + QStringLiteral("/program/sources.json"),
                 QJsonObject{{QStringLiteral("address"), sources.address},
                             {QStringLiteral("ocr"), helper},
                             {QStringLiteral("titles"), titles}});
}

QRectF CleanArea(const QRectF& area) {
    const QRectF whole(0, 0, 1, 1);
    QRectF in = area.normalized().intersected(whole);
    const auto round = [](double v) { return std::round(v * 1e5) / 1e5; };
    in = QRectF(round(in.x()), round(in.y()), round(in.width()), round(in.height()));
    return in.width() >= 0.01 && in.height() >= 0.01 ? in : QRectF();
}

QString CleanHelperAddress(const QString& said) {
    QString address = said.trimmed();
    while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
    }
    const QUrl url(address);
    const bool web = url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https");
    return url.isValid() && web && !url.host().isEmpty() ? address : QString();
}

QString OcrEngineName(const QString& engine) {
    return engine == QStringLiteral("oneocr")     ? QStringLiteral("OneOCR")
           : engine == QStringLiteral("screenai") ? QStringLiteral("Screen AI")
           : engine == QStringLiteral("glens")    ? QStringLiteral("Google Lens")
           : engine == QStringLiteral("meikiocr") ? QStringLiteral("meikiocr")
                                                  : QStringLiteral("As the helper has it");
}

QString AddWindow(Sources& sources, const QString& title, const QString& window, const QRectF& area, const QString& record_key,
                  const QList<Box>& more) {
    const QRectF in = CleanArea(area);
    if (!in.isValid()) {
        return {};
    }
    const QString id = Add(sources, title, QStringLiteral("ocr"), record_key);
    for (Title& known : sources.titles) {
        if (known.id == id) {
            known.window = window;
            known.area = in;
            known.more.clear();
            for (const Box& box : more) {
                if (const QRectF clean = CleanArea(box.rect); clean.isValid()) {
                    known.more.append({clean, box.ask});
                }
            }
        }
    }
    return id;
}

QString AddHook(Sources& sources, const QString& title, const Title& hook, const QString& record_key) {
    if (!IsEngine(hook.engine) || hook.process.trimmed().isEmpty() || (hook.engine == QStringLiteral("agent") && hook.script.isEmpty())) {
        return {};
    }
    const QString id = Add(sources, title, QStringLiteral("hook"), record_key);
    for (Title& known : sources.titles) {
        if (known.id == id) {
            known.engine = hook.engine;
            known.script = hook.script;
            known.process = hook.process.trimmed();
            known.codes = hook.codes;
            known.threads = hook.threads;
            known.hook_auto = hook.hook_auto;
            known.path = hook.path;
        }
    }
    return id;
}

QList<Title> HookedIn(const Sources& sources, const QString& process) {
    QList<Title> found;
    for (const Title& title : sources.titles) {
        if (title.by == QStringLiteral("hook") && title.process.compare(process, Qt::CaseInsensitive) == 0) {
            found.append(title);
        }
    }
    if (found.size() != 1 || !found.first().hook_auto) {
        found.clear();
    }
    return found;
}

bool Remove(Sources& sources, const QString& id) {
    return sources.titles.removeIf([&](const Title& title) { return title.id == id; }) > 0;
}

QString Add(Sources& sources, const QString& title_, const QString& by, const QString& record_key) {
    const QString title = title_.trimmed();
    if (title.isEmpty() || !IsWay(by)) {
        return {};
    }
    const QString id = GameId(title, record_key);
    for (Title& known : sources.titles) {
        if (known.id == id) {
            known.by = by;
            return id;
        }
    }
    sources.titles.append({id, title, by});
    return id;
}

QString CoverOf(const QString& folder, const QString& id) {
    return folder + QStringLiteral("/program/covers/%1.jpg").arg(id);
}

QString PathOf(const Title& title) {
    // A hook says its engine too: "lines://hook-agent/<ID>".
    return QStringLiteral("lines://%1/%2").arg(title.by == QStringLiteral("hook") ? QStringLiteral("hook-") + title.engine : title.by, title.id);
}

QString WayOf(const QString& path) {
    const QString head = QStringLiteral("lines://");
    return path.startsWith(head) ? path.mid(head.size()).section(QLatin1Char('/'), 0, 0).toUpper() : QString();
}

QString ReadByOf(const QString& path) {
    const QString way = WayOf(path);
    if (way.isEmpty()) {
        return {};
    }
    if (way == QStringLiteral("OCR")) {
        return QStringLiteral("WINDOW  ·  OCR");
    }
    if (way.startsWith(QStringLiteral("HOOK-"))) {
        const QString engine = way.mid(5);
        return QStringLiteral("HOOK  ·  ") + (engine == QStringLiteral("LUNA") ? QStringLiteral("LUNAHOOK") : engine);
    }
    return QStringLiteral("LINES  ·  ") + way;
}

QString EmulatorConsole(const QString& name) {
    static const QList<QPair<QString, QString>> known{
        {QStringLiteral("yuzu"), QStringLiteral("Nintendo Switch")},     {QStringLiteral("suyu"), QStringLiteral("Nintendo Switch")},
        {QStringLiteral("sudachi"), QStringLiteral("Nintendo Switch")},  {QStringLiteral("citron"), QStringLiteral("Nintendo Switch")},
        {QStringLiteral("ryujinx"), QStringLiteral("Nintendo Switch")},  {QStringLiteral("eden"), QStringLiteral("Nintendo Switch")},
        {QStringLiteral("ppsspp"), QStringLiteral("PlayStation Portable")}, {QStringLiteral("pcsx2"), QStringLiteral("PlayStation 2")},
        {QStringLiteral("vita3k"), QStringLiteral("PlayStation Vita")},  {QStringLiteral("rpcs3"), QStringLiteral("PlayStation 3")},
        {QStringLiteral("citra"), QStringLiteral("Nintendo 3DS")},       {QStringLiteral("azahar"), QStringLiteral("Nintendo 3DS")},
        {QStringLiteral("lime3ds"), QStringLiteral("Nintendo 3DS")},     {QStringLiteral("duckstation"), QStringLiteral("PlayStation")},
        {QStringLiteral("flycast"), QStringLiteral("Dreamcast")},        {QStringLiteral("redream"), QStringLiteral("Dreamcast")},
    };
    const QString lower = name.trimmed().toLower();
    for (const auto& [head, console] : known) {
        if (lower.startsWith(head)) {
            return console;
        }
    }
    return {};
}

QString ConsoleOf(const Title& title) {
    if (!title.console.isEmpty()) {
        return title.console;
    }
    const QString emulator = EmulatorConsole(title.by == QStringLiteral("hook") ? title.process : title.by == QStringLiteral("ocr") ? title.window : QString());
    return emulator.isEmpty() ? System : emulator;
}

bool WriteApp(const QString& folder, const Sources& sources, const QString& program, const QJsonObject* stats) {
    const QString file = folder + QStringLiteral("/apps/%1.json").arg(AppKey);
    const QJsonObject before = Read(file);
    QJsonArray games;
    for (const Title& title : sources.titles) {
        // Its cover, where it has one: a window's picture as it was when the title was added.
        const QString cover = CoverOf(folder, title.id);
        QJsonObject game{{QStringLiteral("id"), title.id}, {QStringLiteral("title"), title.title},
                         {QStringLiteral("path"), PathOf(title)}, {QStringLiteral("cover"), QFileInfo::exists(cover) ? cover : QString()},
                         {QStringLiteral("readable"), true}};
        // A game of an emulator here stands on that console's shelf (library.cpp, "system").
        if (const QString console = ConsoleOf(title); console != System) {
            game.insert(QStringLiteral("system"), console);
        }
        games.append(game);
    }
    // The file every reader app writes (library.h): its console is the PC, after the consoles.
    QJsonObject root{{QStringLiteral("format"), 1},
                     {QStringLiteral("app"), AppKey},
                     {QStringLiteral("name"), QStringLiteral("Furoshiki")},
                     {QStringLiteral("system"), System},
                     {QStringLiteral("short"), QStringLiteral("PC")},
                     {QStringLiteral("order"), 90},
                     {QStringLiteral("program"), program},
                     {QStringLiteral("game_args"), QJsonArray{QStringLiteral("--lines"), QStringLiteral("%ID%")}},
                     {QStringLiteral("menu_args"), QJsonArray()},
                     {QStringLiteral("written"), QDateTime::currentMSecsSinceEpoch()},
                     {QStringLiteral("games"), games},
                     {QStringLiteral("stats"), stats != nullptr ? *stats : before.value(QStringLiteral("stats")).toObject()}};
    return Write(file, root);
}

} // namespace Home::Lines
