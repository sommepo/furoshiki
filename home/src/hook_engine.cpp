// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hook_engine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Home::Hooks {

QString KeyOf(Engine engine) {
    return engine == Engine::Agent ? QStringLiteral("agent") : engine == Engine::Luna ? QStringLiteral("luna") : QStringLiteral("textractor");
}

std::optional<Engine> EngineOf(const QString& key) {
    for (const Engine engine : {Engine::Agent, Engine::Luna, Engine::Textractor}) {
        if (KeyOf(engine) == key.toLower()) {
            return engine;
        }
    }
    return std::nullopt;
}

QString LabelOf(Engine engine) {
    return engine == Engine::Agent ? QStringLiteral("AGENT") : engine == Engine::Luna ? QStringLiteral("LUNAHOOK") : QStringLiteral("TEXTRACTOR");
}

QString ThreadKey(const QString& name, const QString& code, quint64 ctx, quint64 ctx2) {
    return QStringLiteral("%1|%2|%3|%4").arg(name, code).arg(ctx & 0xFFFF, 0, 16).arg(ctx2 & 0xFFFF, 0, 16).toUpper();
}

QString CleanText(const QString& text) {
    QString out = text;
    out.remove(QChar(0xFEFF));
    out.remove(QLatin1Char('\r'));
    return out.trimmed();
}

namespace {
Event Of(Event::Kind kind, const QString& text) {
    Event event;
    event.kind = kind;
    event.text = text;
    return event;
}

Event TextOn(const QString& name, const QString& code, quint32 pid, quint64 addr, quint64 ctx, quint64 ctx2, const QString& text) {
    Event event;
    event.kind = Event::Text;
    event.thread.name = name;
    event.thread.code = code;
    event.thread.pid = pid;
    event.thread.addr = addr;
    event.thread.ctx = ctx;
    event.thread.ctx2 = ctx2;
    event.thread.key = ThreadKey(name, code, ctx, ctx2);
    event.text = text;
    return event;
}
} // namespace

std::optional<Event> ReadInjectLine(const QByteArray& raw) {
    const QByteArray line = raw.trimmed();
    if (line.isEmpty()) {
        return std::nullopt;
    }
    QJsonParseError parse{};
    const QJsonDocument document = line.startsWith('{') ? QJsonDocument::fromJson(line, &parse) : QJsonDocument();
    if (!document.isObject()) {
        // What the script wrote with console.log; frida-inject colours warnings and errors.
        static const QRegularExpression colour(QStringLiteral("\x1b\\[[0-9;]*m"));
        const QString said = QString::fromUtf8(line).remove(colour).trimmed();
        return said.isEmpty() ? std::nullopt : std::optional<Event>(Of(Event::Said, said));
    }
    const QJsonObject message = document.object();
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("error")) {
        QString what = message.value(QStringLiteral("description")).toString();
        if (const int at = message.value(QStringLiteral("lineNumber")).toInt(); at > 0) {
            what += QStringLiteral(" (%1:%2)").arg(message.value(QStringLiteral("fileName")).toString()).arg(at);
        }
        return Of(Event::Fault, what);
    }
    if (type == QStringLiteral("log")) {
        return Of(Event::Said, message.value(QStringLiteral("payload")).toString());
    }
    if (type != QStringLiteral("send")) {
        return std::nullopt;
    }
    const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
    const QString cmd = payload.value(QStringLiteral("cmd")).toString();
    if (cmd == QStringLiteral("copy")) {
        const QJsonValue text = payload.value(QStringLiteral("text"));
        if (!text.isString()) {
            return std::nullopt; // a handler that had nothing to say
        }
        Event event;
        event.kind = Event::Text;
        event.thread.key = AgentThread;
        event.thread.name = QStringLiteral("Script");
        event.text = text.toString();
        return event;
    }
    if (cmd == QStringLiteral("furoshiki_storage")) {
        Event event;
        event.kind = Event::Storage;
        event.which = payload.value(QStringLiteral("which")).toString();
        event.op = payload.value(QStringLiteral("op")).toString();
        for (const QJsonValue& value : payload.value(QStringLiteral("args")).toArray()) {
            event.args.append(value.toString());
        }
        return event;
    }
    if (cmd == QStringLiteral("detach")) {
        return Of(Event::Gone, QStringLiteral("the script let go"));
    }
    return std::nullopt;
}

std::optional<Event> ReadLunaLine(const QByteArray& raw) {
    const QJsonDocument document = QJsonDocument::fromJson(raw.trimmed());
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject line = document.object();
    if (line.contains(QStringLiteral("said"))) {
        return Of(Event::Said, line.value(QStringLiteral("said")).toString());
    }
    if (line.contains(QStringLiteral("fault"))) {
        return Of(Event::Fault, line.value(QStringLiteral("fault")).toString());
    }
    if (line.contains(QStringLiteral("attached"))) {
        return Of(Event::Attached, QString::number(line.value(QStringLiteral("attached")).toInteger()));
    }
    if (line.contains(QStringLiteral("gone"))) {
        return Of(Event::Gone, QString::number(line.value(QStringLiteral("gone")).toInteger()));
    }
    if (!line.contains(QStringLiteral("text")) && !line.contains(QStringLiteral("thread"))) {
        return std::nullopt;
    }
    const auto hex = [&](const char* name) { return line.value(QLatin1String(name)).toString().toULongLong(nullptr, 16); };
    Event event = TextOn(line.value(QStringLiteral("name")).toString(), line.value(QStringLiteral("code")).toString(),
                         static_cast<quint32>(line.value(QStringLiteral("pid")).toInteger()), hex("addr"), hex("ctx"), hex("ctx2"),
                         line.value(QStringLiteral("text")).toString());
    return event;
}

void TextractorReader::Reset() {
    odd.clear();
    partial.clear();
    waits.reset();
}

QList<Event> TextractorReader::Take(const QByteArray& bytes) {
    QList<Event> out;
    odd += bytes;
    const qsizetype whole = odd.size() - odd.size() % 2;
    partial += QString::fromUtf16(reinterpret_cast<const char16_t*>(odd.constData()), whole / 2);
    odd.remove(0, whole);
    qsizetype end;
    while ((end = partial.indexOf(QLatin1Char('\n'))) >= 0) {
        Line(partial.left(end), out);
        partial.remove(0, end + 1);
    }
    return out;
}

void TextractorReader::Line(const QString& said, QList<Event>& out) {
    QString line = said;
    line.remove(QChar(0xFEFF));
    if (line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    // [handle:pid:addr:ctx:ctx2:name:hookcode] text. The name has no colon; the hook code may
    // (a module's name after its address), but no "] ".
    static const QRegularExpression head(QStringLiteral("^\\[([0-9A-Fa-f]+):([0-9A-Fa-f]+):([0-9A-Fa-f]+):([0-9A-Fa-f]+):([0-9A-Fa-f]+):([^:\\]]*):([^\\]]*)\\] (.*)$"),
                                         QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = head.match(line);
    if (!match.hasMatch()) {
        if (line.startsWith(QStringLiteral("Usage: "))) {
            return;
        }
        // A sentence's next line.
        if (waits) {
            waits->text += QLatin1Char('\n') + line;
        }
        return;
    }
    if (waits) {
        out.append(*waits);
        waits.reset();
    }
    const quint64 handle = match.captured(1).toULongLong(nullptr, 16);
    const quint32 pid = match.captured(2).toUInt(nullptr, 16);
    const QString name = match.captured(6), text = match.captured(8);
    if (pid == 0 && name == QStringLiteral("Clipboard")) {
        return; // everything copied on this computer: not the game's
    }
    if (pid == 0 || (handle == 0 && name == QStringLiteral("Console"))) {
        // What the engine itself says.
        Event event = Of(Event::Said, text);
        if (text.contains(QStringLiteral("pipe connected"), Qt::CaseInsensitive)) {
            event.kind = Event::Attached;
        } else if (text.contains(QStringLiteral("architecture mismatch"), Qt::CaseInsensitive) || text.contains(QStringLiteral("couldn't inject"), Qt::CaseInsensitive) ||
                   text.contains(QStringLiteral("could not inject"), Qt::CaseInsensitive) || text.contains(QStringLiteral("invalid process"), Qt::CaseInsensitive)) {
            event.kind = Event::Fault;
        } else if (text.contains(QStringLiteral("process disconnected"), Qt::CaseInsensitive)) {
            event.kind = Event::Gone;
        }
        waits = event;
        return;
    }
    waits = TextOn(name, match.captured(7), pid, match.captured(3).toULongLong(nullptr, 16), match.captured(4).toULongLong(nullptr, 16),
                   match.captured(5).toULongLong(nullptr, 16), text);
}

std::optional<Event> TextractorReader::Rest() {
    std::optional<Event> out;
    out.swap(waits);
    return out;
}

QByteArray TextractorCommands(qint64 pid, const QStringList& codes) {
    QString text = QStringLiteral("attach -P%1\n").arg(pid);
    for (const QString& code : codes) {
        // A line the CLI cannot read ends it: only what looks like a hook code is sent.
        if (LooksLikeHookCode(code)) {
            text += QStringLiteral("%1 -P%2\n").arg(code.trimmed()).arg(pid);
        }
    }
    return QByteArray(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
}

bool LooksLikeHookCode(const QString& said) {
    const QString code = said.trimmed();
    static const QRegularExpression form(QStringLiteral("^/?[HRE][A-Za-z0-9#<+*:@!$._\\-/\\\\]*@[A-Za-z0-9#<+*:@!$._\\-/\\\\]+$"));
    return code.size() <= 400 && form.match(code).hasMatch();
}

const Thread& Threads::Note(const Event& event) {
    for (Thread& thread : threads) {
        if (thread.key == event.thread.key) {
            if (event.kind == Event::Text && !event.text.isEmpty()) {
                thread.latest = CleanText(event.text);
                ++thread.count;
            }
            return thread;
        }
    }
    Thread thread = event.thread;
    if (event.kind == Event::Text && !event.text.isEmpty()) {
        thread.latest = CleanText(event.text);
        thread.count = 1;
    }
    threads.append(thread);
    return threads.last();
}

const Thread* Threads::Find(const QString& key) const {
    for (const Thread& thread : threads) {
        if (thread.key == key) {
            return &thread;
        }
    }
    return nullptr;
}

} // namespace Home::Hooks
