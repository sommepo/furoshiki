// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hook_runner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include "hook_fetch.h"
#include "hook_scripts.h"

namespace Home::Hooks {

Runner::Runner(QObject* parent) : QObject(parent) {
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] { Read(false); });
    connect(&process, &QProcess::readyReadStandardError, this, [this] { Read(true); });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        Read(false);
        Read(true);
        if (const auto event = textractor.Rest()) {
            Take(*event);
        }
        if (storage_dirty) {
            Agent::SaveStorage(plan.shared, plan.title, plan.pid, storage);
            storage_dirty = false;
        }
        attached = false;
        if (stopping) {
            return;
        }
        const QString why = !fault.isEmpty() ? fault : code != 0 && !said.isEmpty() ? said.last() : QStringLiteral("the program went");
        fault = why;
        emit Changed();
        emit Ended(why);
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !stopping) {
            fault = QStringLiteral("%1 could not be started").arg(QFileInfo(process.program()).fileName());
            emit Changed();
            emit Ended(fault);
        }
    });
    // A sentence of Textractor's is whole once nothing more of it came for a moment.
    rest.setSingleShot(true);
    rest.setInterval(80);
    connect(&rest, &QTimer::timeout, this, [this] {
        if (const auto event = textractor.Rest()) {
            Take(*event);
        }
    });
    keep.setInterval(5000);
    connect(&keep, &QTimer::timeout, this, [this] {
        if (storage_dirty) {
            Agent::SaveStorage(plan.shared, plan.title, plan.pid, storage);
            storage_dirty = false;
        }
    });
}

Runner::~Runner() {
    Stop();
}

bool Runner::Start(const Plan& plan_, QString& error) {
    Stop();
    plan = plan_;
    threads.Clear();
    textractor.Reset();
    out_buffer.clear();
    error_buffer.clear();
    said.clear();
    fault.clear();
    attached = false;
    stopping = false;
    QStringList command;
    QByteArray input;
    if (plan.engine == Engine::Agent) {
        const QString inject = FridaInject(plan.shared);
        if (inject.isEmpty()) {
            error = QStringLiteral("Frida's frida-inject is not here: it comes in hooks beside the program");
            return false;
        }
        QFile host(QStringLiteral(":/hooks/agent_host.js"));
        if (!host.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("This build has no host for Agent's scripts");
            return false;
        }
        storage = Agent::LoadStorage(plan.shared, plan.title, plan.pid);
        const QString bundle = Agent::Bundle(ScriptsFolder(plan.shared), plan.script, QString::fromUtf8(host.readAll()), storage, error);
        if (bundle.isEmpty()) {
            return false;
        }
        const QString file = HooksFolder(plan.shared) + QStringLiteral("/run/%1.js").arg(plan.title.isEmpty() ? QStringLiteral("wizard") : plan.title);
        QDir().mkpath(QFileInfo(file).path());
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(bundle.toUtf8()) <= 0 || !out.commit()) {
            error = QStringLiteral("The script's bundle could not be written to %1").arg(file);
            return false;
        }
        // QuickJS, as Agent runs its scripts.
        command = {inject, QStringLiteral("-p"), QString::number(plan.pid), QStringLiteral("-s"), QDir::toNativeSeparators(file), QStringLiteral("-R"), QStringLiteral("qjs")};
        keep.start();
    } else if (plan.engine == Engine::Textractor) {
        command = TextractorCommand(plan.shared, plan.is32);
        if (command.isEmpty()) {
            error = QStringLiteral("Textractor is not fetched yet");
            return false;
        }
        input = TextractorCommands(plan.pid, plan.codes);
    } else {
        command = LunaCommand(plan.shared);
        if (command.isEmpty()) {
            error = QStringLiteral("LunaHook is not fetched yet, or Furoshiki's host for it is not beside the program");
            return false;
        }
        command << QString::number(plan.pid) << (plan.is32 ? QStringLiteral("32") : QStringLiteral("64"));
        for (const QString& code : plan.codes) {
            if (LooksLikeHookCode(code)) {
                command << code.trimmed();
            }
        }
    }
    process.setProgram(command.first());
    process.setArguments(command.mid(1));
    process.setWorkingDirectory(QFileInfo(command.first()).isAbsolute() ? QFileInfo(command.first()).path() : QString());
    process.start();
    if (!input.isEmpty()) {
        process.write(input);
    }
    emit Changed();
    return true;
}

void Runner::Stop() {
    keep.stop();
    rest.stop();
    if (process.state() == QProcess::NotRunning) {
        return;
    }
    stopping = true;
    if (plan.engine == Engine::Textractor) {
        const QString detach = QStringLiteral("detach -P%1\n").arg(plan.pid);
        process.write(QByteArray(reinterpret_cast<const char*>(detach.utf16()), detach.size() * 2));
        process.waitForBytesWritten(200);
    }
    process.closeWriteChannel();
    // frida-inject takes its script out of the game when it is asked to end; a console program
    // on Windows cannot be asked, and Frida lets go of the game by itself when its host is gone.
    process.terminate();
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(1500);
    }
    attached = false;
    emit Changed();
}

QString Runner::Status() const {
    if (!Running()) {
        return fault.isEmpty() ? QStringLiteral("not hooked") : QStringLiteral("ended  ·  ") + fault;
    }
    if (!attached) {
        return QStringLiteral("hooking");
    }
    if (plan.engine == Engine::Agent) {
        return QStringLiteral("hooked");
    }
    const int count = static_cast<int>(threads.All().size());
    return QStringLiteral("hooked  ·  %1 %2").arg(count).arg(count == 1 ? QStringLiteral("thread") : QStringLiteral("threads"));
}

void Runner::Read(bool errors) {
    const QByteArray bytes = errors ? process.readAllStandardError() : process.readAllStandardOutput();
    if (bytes.isEmpty()) {
        return;
    }
    if (plan.engine == Engine::Textractor && !errors) {
        for (const Event& event : textractor.Take(bytes)) {
            Take(event);
        }
        rest.start();
        return;
    }
    QByteArray& buffer = errors ? error_buffer : out_buffer;
    buffer += bytes;
    qsizetype end;
    while ((end = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(end);
        buffer.remove(0, end + 1);
        std::optional<Event> event = plan.engine == Engine::Luna && !errors ? ReadLunaLine(line) : ReadInjectLine(line);
        if (plan.engine != Engine::Agent && errors && event) {
            event->kind = Event::Said;
        }
        if (event) {
            Take(*event);
        }
    }
}

void Runner::Say(const QString& text) {
    said.append(text);
    while (said.size() > 12) {
        said.removeFirst();
    }
}

void Runner::Take(const Event& event) {
    switch (event.kind) {
    case Event::Text: {
        attached = true;
        const QString text = CleanText(event.text);
        const Thread thread = threads.Note(event);
        if (!text.isEmpty()) {
            emit Line(thread, text);
        }
        break;
    }
    case Event::Said:
        Say(event.text);
        // Agent's runtime says so once the script has run to its end.
        if (plan.engine == Engine::Agent && event.text.startsWith(QStringLiteral("[+] Agent loaded"))) {
            attached = true;
        }
        if (plan.engine == Engine::Agent && (event.text.startsWith(QStringLiteral("Failed to ")) || event.text.startsWith(QStringLiteral("Unable to ")))) {
            fault = event.text; // frida-inject's own refusal (no such process, not allowed to look into it)
        }
        break;
    case Event::Fault:
        fault = event.text;
        Say(event.text);
        break;
    case Event::Attached:
        attached = true;
        Say(plan.engine == Engine::Textractor ? event.text : QStringLiteral("attached"));
        break;
    case Event::Gone:
        Say(event.text);
        break;
    case Event::Storage:
        Agent::ApplyStorage(storage, event.which, event.op, event.args);
        storage_dirty = true;
        return;
    }
    emit Changed();
}

} // namespace Home::Hooks
