// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Hooks started by Furoshiki (docs/hooks.md): what the three engines say, read into the same
// things. An engine is a program run against a game's process: frida-inject with a 0xDC00 Agent
// script, Textractor's CLI, or LunaHook through Furoshiki's small host. Each writes lines; here
// they become events: a line of text on a *thread* (one stream of text the engine found in the
// game; an Agent script is one thread), something the engine says, a fault.
//
// Qt Core only: tested by reader-home-tests with output made up from the engines' documented
// forms. Neither Textractor nor LunaHook was ever run by these tests.

#include <optional>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace Home::Hooks {

enum class Engine { Agent, Luna, Textractor };
/// "agent", "luna", "textractor", as a title's file names it.
[[nodiscard]] QString KeyOf(Engine engine);
[[nodiscard]] std::optional<Engine> EngineOf(const QString& key);
/// For the page: "AGENT", "LUNAHOOK", "TEXTRACTOR".
[[nodiscard]] QString LabelOf(Engine engine);

/// The one thread of an Agent script.
inline const QString AgentThread = QStringLiteral("script");

struct Thread {
    QString key;   ///< What it is remembered by (ThreadKey)
    QString name;  ///< The hook's name ("KiriKiri", "GetGlyphOutlineA"); an Agent script's own
    QString code;  ///< Its hook code
    quint32 pid{};
    quint64 addr{}, ctx{}, ctx2{};
    QString latest; ///< The last text it said
    int count{};    ///< How many it said
};

/// A thread's key: its hook's name and code and the low halves of its two contexts. A game's
/// addresses move from one start to the next, by whole 64 kB: the low 16 bits of a return address
/// stay, and so does a hook code that names a module. (As Textractor's own saved threads, this is
/// a best guess: a thread not found again is chosen again.)
[[nodiscard]] QString ThreadKey(const QString& name, const QString& code, quint64 ctx, quint64 ctx2);

struct Event {
    enum Kind {
        Text,     ///< `text` on `thread`
        Said,     ///< The engine or the script says `text` (a log line, Textractor's Console)
        Fault,    ///< A script's error, an engine's refusal
        Storage,  ///< An Agent script stored something: `which`, `op`, `args`
        Attached, ///< The engine reached the process
        Gone,     ///< The engine let go of the process
    };
    Kind kind{Said};
    Thread thread;
    QString text;
    QString which, op;
    QStringList args;
};

/// One line of frida-inject's output with Agent's runtime in the script: a message as JSON
/// ({"type":"send","payload":{"cmd":"copy","text":…}}, {"type":"error",…}), or a plain line the
/// script logged. Nothing for a message that is no one's business here.
[[nodiscard]] std::optional<Event> ReadInjectLine(const QByteArray& line);

/// One line of Furoshiki's LunaHook host (hook-helper/luna_host.cpp), UTF-8 JSON:
///   {"text":"…","name":"…","code":"…","pid":1,"addr":"1F","ctx":"2A","ctx2":"0"}
///   {"thread":"new", …the same without text}   {"said":"…"}   {"fault":"…"}
///   {"attached":1234}   {"gone":1234}
[[nodiscard]] std::optional<Event> ReadLunaLine(const QByteArray& line);

/// Textractor's CLI writes UTF-16LE: "[handle:pid:addr:ctx:ctx2:name:hookcode] text", a sentence
/// with line breaks going on over the next lines. Bytes in (in any pieces), events out. A
/// sentence is known whole only when the next begins: Rest hands over the one that waits (call
/// it when nothing came for a moment).
class TextractorReader {
public:
    QList<Event> Take(const QByteArray& bytes);
    std::optional<Event> Rest();
    void Reset();

private:
    void Line(const QString& line, QList<Event>& out);
    QByteArray odd;  ///< A byte or a line's beginning left over
    QString partial;
    std::optional<Event> waits;
};

/// What Textractor's CLI is told, as the bytes it reads (UTF-16LE): attach, then each hook code.
[[nodiscard]] QByteArray TextractorCommands(qint64 pid, const QStringList& codes);
/// "/HS-4@12345:game.exe" and the like: a hook code the CLIs take as one word (no spaces).
[[nodiscard]] bool LooksLikeHookCode(const QString& code);

/// The threads an engine has shown, in the order they came.
class Threads {
public:
    /// Notes a text event's thread and its line; the thread as kept.
    const Thread& Note(const Event& event);
    [[nodiscard]] const QList<Thread>& All() const {
        return threads;
    }
    [[nodiscard]] const Thread* Find(const QString& key) const;
    void Clear() {
        threads.clear();
    }

private:
    QList<Thread> threads;
};

/// Text an engine handed over, made one sentence a line the way line_feed.h's Normalise does not:
/// an engine's sentence may hold several lines of a text box, which are one line here.
[[nodiscard]] QString CleanText(const QString& text);

} // namespace Home::Hooks
