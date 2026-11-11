// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * A line put into English and taken apart into its grammar, as Furochibi's 解説 does it
 * (furochibi/server/console_ocr/explain.py: the same instructions and the same answer), by
 * Claude Code in print mode on this computer with the user's own login. Nothing is asked
 * unless the user asks; the line, the few lines before it and the title go to Anthropic and
 * nothing else does. Answers are kept in a file, so a line asked again costs nothing and its
 * English is there for a card's {translation}.
 */
namespace ReaderApp::Explain {

struct Segment {
    QString text;
    QString reading;
    QString gloss;
    QStringList grammar;
};

struct Answer {
    QString translation;
    QList<Segment> segments;
    QString note;
    /// What went wrong, when nothing could be had.
    QString error;

    [[nodiscard]] bool IsEmpty() const {
        return translation.isEmpty() && segments.isEmpty();
    }
};

struct Request {
    QString line;
    /// The lines just before, oldest first.
    QStringList before;
    QString title;
    /// The title's names that stand in the line, to be kept as names.
    QStringList names;
};

struct Setup {
    /// The program: "claude", or its whole path.
    QString command{QStringLiteral("claude")};
    QString model{QStringLiteral("claude-sonnet-5-5")};
    QString effort{QStringLiteral("low")};
};

/// What Claude is told the line is.
[[nodiscard]] QString Prompt(const Request& request);
/// The program's arguments for `request`.
[[nodiscard]] QStringList Arguments(const Setup& setup, const Request& request);
/// Claude Code's printed JSON as an answer; `error` set when it is none.
[[nodiscard]] Answer Parse(const QByteArray& output, const QByteArray& errors = {});

/// Asks, waiting for the answer (some seconds; two minutes at most): not for the main thread.
[[nodiscard]] Answer Ask(const Setup& setup, const Request& request);

/// What a service of the user's answered (Furochibi's POST /api/explain: the answer's own
/// fields, or words saying why not) as an answer.
[[nodiscard]] Answer ParseService(const QByteArray& body, const QByteArray& errors = {});
/// What that service is sent for `request`, of the game `game_id` (its names are the service's).
[[nodiscard]] QByteArray ServiceBody(const Request& request, const QString& game_id);
/**
 * Asks a service of the user's instead of Claude Code on this computer: `address` is its base
 * ("https://box.example.ts.net:8794"), asked with the system's curl. The same answer on every
 * device, and no Claude Code needed on this one. Waits: not for the main thread.
 */
[[nodiscard]] Answer AskService(const QString& address, const Request& request, const QString& game_id);

/// The answers kept in `file` (a JSON object by line), any thread.
class Cache {
public:
    explicit Cache(QString file_);
    [[nodiscard]] std::optional<Answer> Of(const QString& line) const;
    /// A line's English, or empty when it was never explained.
    [[nodiscard]] QString TranslationOf(const QString& line) const;
    void Keep(const QString& line, const Answer& answer);

private:
    QString file;
};

/// An answer as a small page for a text view: the English, each part with its reading,
/// meaning and grammar points, the note.
[[nodiscard]] QString ToHtml(const Answer& answer);

} // namespace ReaderApp::Explain
