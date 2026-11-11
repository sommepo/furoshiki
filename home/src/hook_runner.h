// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// One hook engine against one process (docs/hooks.md): the engine as a child process of
// Furoshiki (frida-inject with an Agent script's bundle, Textractor's CLI, Furoshiki's LunaHook
// host), its output read into threads and their lines (hook_engine.h). The wizard runs one to
// show the threads; a title read by a hook runs one for as long as its game does.

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include "hook_engine.h"

namespace Home::Hooks {

struct Plan {
    Engine engine{Engine::Agent};
    qint64 pid{};
    bool is32{};
    QString script;    ///< Agent: the script's file among the fetched scripts
    QStringList codes; ///< LunaHook, Textractor: hook codes put in besides the engine's own
    QString title;     ///< Whose storage an Agent script's is ("wizard" before it has a name)
    QString shared;    ///< The shared folder
};

class Runner : public QObject {
    Q_OBJECT

public:
    explicit Runner(QObject* parent = nullptr);
    ~Runner() override;

    /// Starts the engine. False, with `error`, when it cannot be (no engine here, no script).
    bool Start(const Plan& plan, QString& error);
    void Stop();
    [[nodiscard]] bool Running() const {
        return process.state() != QProcess::NotRunning;
    }
    /// The engine reached the process (Agent: the script loaded).
    [[nodiscard]] bool Attached() const {
        return attached;
    }
    [[nodiscard]] const Plan& Current() const {
        return plan;
    }
    [[nodiscard]] const QList<Thread>& Threads() const {
        return threads.All();
    }
    /// What the engine and the script said last, the newest last (a few lines).
    [[nodiscard]] const QStringList& Said() const {
        return said;
    }
    /// The last fault, if any: a script's error, an engine's refusal, why it ended.
    [[nodiscard]] const QString& Fault() const {
        return fault;
    }
    /// For the user: "hooked · 3 threads", "hooking", "ended: …".
    [[nodiscard]] QString Status() const;

signals:
    void Line(const Home::Hooks::Thread& thread, const QString& text);
    void Changed();
    /// The engine's process ended (the game closed, the engine failed): `why` for the user.
    void Ended(const QString& why);

private:
    void Read(bool errors);
    void Take(const Event& event);
    void Say(const QString& text);

    QProcess process;
    QTimer rest, keep;
    Plan plan;
    Hooks::Threads threads;
    TextractorReader textractor;
    QByteArray out_buffer, error_buffer;
    QStringList said;
    QString fault;
    bool attached{}, stopping{};
    QJsonObject storage;
    bool storage_dirty{};
};

} // namespace Home::Hooks
