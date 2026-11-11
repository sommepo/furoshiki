// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include "reader/desktop/explain.h"

namespace ReaderApp::Explain {

namespace {

// Furochibi's own (explain.py), word for word, so that both explain alike.
const char* const System =
    "You explain one line of Japanese from a game or a show to an English-speaking learner who "
    "reads Japanese games. Split the line into meaningful chunks in order, covering all of it "
    "(a word with its particle, or a grammar construction). For each chunk give: the text exactly "
    "as in the line; its reading in hiragana if it has kanji, else an empty string; a gloss saying "
    "what it means here and what it does grammatically, in one or two plain sentences; and the "
    "grammar points it uses, named as a Japanese grammar dictionary names them (〜ぞ, 〜てしまう, "
    "〜わけではない), or none. Then a natural English translation of the whole line, and a short "
    "note on register, speaker or nuance when it matters (else an empty string). Names listed as "
    "this title's are its people, places and things: keep them as names, never translate them as "
    "ordinary words. Write in English.";

const char* const Schema =
    R"({"type":"object","additionalProperties":false,"required":["translation","segments","note"],)"
    R"("properties":{"translation":{"type":"string"},"note":{"type":"string"},)"
    R"("segments":{"type":"array","items":{"type":"object","additionalProperties":false,)"
    R"("required":["text","reading","gloss","grammar"],"properties":{"text":{"type":"string"},)"
    R"("reading":{"type":"string"},"gloss":{"type":"string"},)"
    R"("grammar":{"type":"array","items":{"type":"string"}}}}}}})";

constexpr int MostKept = 4000;
QMutex cache_mutex;

Answer FromObject(const QJsonObject& object) {
    Answer answer;
    answer.translation = object.value(QStringLiteral("translation")).toString().trimmed();
    answer.note = object.value(QStringLiteral("note")).toString().trimmed();
    for (const QJsonValue& value : object.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject each = value.toObject();
        Segment segment;
        segment.text = each.value(QStringLiteral("text")).toString();
        segment.reading = each.value(QStringLiteral("reading")).toString();
        segment.gloss = each.value(QStringLiteral("gloss")).toString();
        for (const QJsonValue& point : each.value(QStringLiteral("grammar")).toArray()) {
            segment.grammar.append(point.toString());
        }
        if (!segment.text.isEmpty()) {
            answer.segments.append(segment);
        }
    }
    return answer;
}

QJsonObject ToObject(const Answer& answer) {
    QJsonArray segments;
    for (const Segment& segment : answer.segments) {
        segments.append(QJsonObject{{QStringLiteral("text"), segment.text},
                                    {QStringLiteral("reading"), segment.reading},
                                    {QStringLiteral("gloss"), segment.gloss},
                                    {QStringLiteral("grammar"),
                                     QJsonArray::fromStringList(segment.grammar)}});
    }
    return {{QStringLiteral("translation"), answer.translation},
            {QStringLiteral("note"), answer.note},
            {QStringLiteral("segments"), segments}};
}

QJsonObject Read(const QString& file) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(in.readAll()).object();
}

QString KeyOf(const QString& line) {
    return QString::fromLatin1(
        QCryptographicHash::hash(line.trimmed().toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString Escaped(const QString& text) {
    return text.toHtmlEscaped().replace(u'\n', QStringLiteral("<br>"));
}

} // namespace

QString Prompt(const Request& request) {
    QStringList parts;
    if (!request.title.isEmpty()) {
        parts.append(QStringLiteral("Title: ") + request.title);
    }
    if (!request.names.isEmpty()) {
        parts.append(QStringLiteral("This title's names in the line:\n- ") +
                     request.names.join(QStringLiteral("\n- ")));
    }
    if (!request.before.isEmpty()) {
        parts.append(QStringLiteral("The lines just before:\n- ") +
                     request.before.join(QStringLiteral("\n- ")));
    }
    parts.append(QStringLiteral("The line: ") + request.line);
    return parts.join(QStringLiteral("\n\n"));
}

QStringList Arguments(const Setup& setup, const Request& request) {
    // No tools, settings or MCP servers: one short request and its answer.
    return {QStringLiteral("-p"),
            QStringLiteral("--model"),
            setup.model,
            QStringLiteral("--effort"),
            setup.effort,
            QStringLiteral("--tools"),
            QString(),
            QStringLiteral("--no-session-persistence"),
            QStringLiteral("--strict-mcp-config"),
            QStringLiteral("--setting-sources"),
            QString(),
            QStringLiteral("--output-format"),
            QStringLiteral("json"),
            QStringLiteral("--json-schema"),
            QString::fromLatin1(Schema),
            QStringLiteral("--system-prompt"),
            QString::fromUtf8(System),
            Prompt(request)};
}

Answer Parse(const QByteArray& output, const QByteArray& errors) {
    Answer answer;
    QJsonParseError problem{};
    const QJsonObject result = QJsonDocument::fromJson(output, &problem).object();
    const QJsonObject made = result.value(QStringLiteral("structured_output")).toObject();
    if (problem.error != QJsonParseError::NoError || result.isEmpty()) {
        const QByteArray said = errors.trimmed().isEmpty() ? output.trimmed() : errors.trimmed();
        answer.error = said.isEmpty() ? QStringLiteral("No answer from Claude.")
                                      : QString::fromUtf8(said.left(300));
        return answer;
    }
    if (result.value(QStringLiteral("is_error")).toBool() || made.isEmpty()) {
        const QString said = result.value(QStringLiteral("result")).toString().trimmed();
        answer.error = said.isEmpty() ? QStringLiteral("No answer from Claude.") : said.left(300);
        return answer;
    }
    answer = FromObject(made);
    if (answer.IsEmpty()) {
        answer.error = QStringLiteral("No answer from Claude.");
    }
    return answer;
}

Answer Ask(const Setup& setup, const Request& request) {
    Answer answer;
    QString program = setup.command.trimmed();
    if (program.isEmpty()) {
        program = QStringLiteral("claude");
    }
    if (!QFileInfo(program).isAbsolute()) {
        QString found = QStandardPaths::findExecutable(program);
        if (found.isEmpty()) {
            // Where its installer puts it, which a program started from a shortcut may not
            // have on its path.
            found = QStandardPaths::findExecutable(
                program, {QDir::homePath() + QStringLiteral("/.local/bin"),
                          QDir::homePath() + QStringLiteral("/AppData/Roaming/npm")});
        }
        if (found.isEmpty()) {
            answer.error = QStringLiteral("Claude Code is not installed on this computer (%1).")
                               .arg(program);
            return answer;
        }
        program = found;
    }
    QProcess process;
    // An empty folder of its own: it reads no project's instructions.
    const QString folder = QDir::tempPath() + QStringLiteral("/furoshiki-explain");
    QDir().mkpath(folder);
    process.setWorkingDirectory(folder);
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(program, Arguments(setup, request));
    if (!process.waitForStarted(10000)) {
        answer.error = QStringLiteral("Claude Code could not be started (%1).").arg(program);
        return answer;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(2000);
        answer.error = QStringLiteral("Claude took more than two minutes.");
        return answer;
    }
    return Parse(process.readAllStandardOutput(), process.readAllStandardError());
}

Answer ParseService(const QByteArray& body, const QByteArray& errors) {
    QJsonParseError problem{};
    const QJsonObject object = QJsonDocument::fromJson(body, &problem).object();
    Answer answer = problem.error == QJsonParseError::NoError ? FromObject(object) : Answer{};
    if (answer.IsEmpty()) {
        // A refusal is said in words (the day's limit, no token set there).
        const QByteArray said = errors.trimmed().isEmpty() ? body.trimmed() : errors.trimmed();
        answer.error = said.isEmpty() || said.startsWith('{') ? QStringLiteral("No answer from the service.")
                                                              : QString::fromUtf8(said.left(300));
    }
    return answer;
}

QByteArray ServiceBody(const Request& request, const QString& game_id) {
    return QJsonDocument(QJsonObject{{QStringLiteral("text"), request.line},
                                     {QStringLiteral("before"), QJsonArray::fromStringList(request.before)},
                                     {QStringLiteral("game"), request.title},
                                     {QStringLiteral("game_id"), game_id}})
        .toJson(QJsonDocument::Compact);
}

Answer AskService(const QString& address, const Request& request, const QString& game_id) {
    QProcess curl;
    // The body by standard input; an answer that is a refusal is still read (no --fail).
    curl.start(QStringLiteral("curl"),
               {QStringLiteral("--silent"), QStringLiteral("--show-error"), QStringLiteral("--location"),
                QStringLiteral("--max-time"), QStringLiteral("130"), QStringLiteral("-H"),
                QStringLiteral("Content-Type: application/json"), QStringLiteral("--data-binary"), QStringLiteral("@-"),
                address + QStringLiteral("/api/explain")});
    if (!curl.waitForStarted(10000)) {
        Answer answer;
        answer.error = QStringLiteral("curl could not be started.");
        return answer;
    }
    curl.write(ServiceBody(request, game_id));
    curl.closeWriteChannel();
    if (!curl.waitForFinished(140000)) {
        curl.kill();
        curl.waitForFinished(2000);
        Answer answer;
        answer.error = QStringLiteral("The service took more than two minutes.");
        return answer;
    }
    return ParseService(curl.readAllStandardOutput(), curl.readAllStandardError());
}

Cache::Cache(QString file_) : file(std::move(file_)) {}

std::optional<Answer> Cache::Of(const QString& line) const {
    QMutexLocker lock(&cache_mutex);
    const QJsonValue kept = Read(file).value(KeyOf(line));
    if (!kept.isObject()) {
        return std::nullopt;
    }
    const Answer answer = FromObject(kept.toObject());
    return answer.IsEmpty() ? std::nullopt : std::optional<Answer>(answer);
}

QString Cache::TranslationOf(const QString& line) const {
    const auto answer = Of(line);
    return answer ? answer->translation : QString();
}

void Cache::Keep(const QString& line, const Answer& answer) {
    if (answer.IsEmpty()) {
        return;
    }
    QMutexLocker lock(&cache_mutex);
    QJsonObject all = Read(file);
    // Kept in the order made (a JSON object has none): the oldest go once there are many.
    QJsonArray order = all.value(QStringLiteral("order")).toArray();
    const QString key = KeyOf(line);
    if (!all.contains(key)) {
        order.append(key);
    }
    while (order.size() > MostKept) {
        all.remove(order.first().toString());
        order.removeFirst();
    }
    all.insert(key, ToObject(answer));
    all.insert(QStringLiteral("order"), order);
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        out.commit();
    }
}

QString ToHtml(const Answer& answer) {
    if (!answer.error.isEmpty()) {
        return QStringLiteral("<p>") + Escaped(answer.error) + QStringLiteral("</p>");
    }
    QString out = QStringLiteral("<p style=\"font-size:large\">") + Escaped(answer.translation) +
                  QStringLiteral("</p>");
    if (!answer.segments.isEmpty()) {
        out += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"5\">");
        for (const Segment& segment : answer.segments) {
            QString word = QStringLiteral("<b>") + Escaped(segment.text) + QStringLiteral("</b>");
            if (!segment.reading.isEmpty()) {
                word += QStringLiteral("<br><small>") + Escaped(segment.reading) +
                        QStringLiteral("</small>");
            }
            QString gloss = Escaped(segment.gloss);
            if (!segment.grammar.isEmpty()) {
                gloss += QStringLiteral("<br><small><i>") +
                         Escaped(segment.grammar.join(QStringLiteral(" · "))) +
                         QStringLiteral("</i></small>");
            }
            out += QStringLiteral("<tr><td valign=\"top\" style=\"white-space:nowrap\">") + word +
                   QStringLiteral("</td><td valign=\"top\">") + gloss + QStringLiteral("</td></tr>");
        }
        out += QStringLiteral("</table>");
    }
    if (!answer.note.isEmpty()) {
        out += QStringLiteral("<p><i>") + Escaped(answer.note) + QStringLiteral("</i></p>");
    }
    return out;
}

} // namespace ReaderApp::Explain
