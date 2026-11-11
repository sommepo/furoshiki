// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hook_source.h"

#include <QDateTime>
#include "reader/core/audio_tap.h"
#include "hook_processes.h"

namespace Home {

namespace {
ReaderApp::TextLine LineOf(const QString& text, qint64 sequence) {
    ReaderApp::TextLine line;
    line.sequence = sequence;
    // Where it is in the computer's sound, as cards cut it (loopback_audio.h), from when it came.
    line.audio_position = Reader::AudioTap::Instance().IsEnabled() ? std::max<qint64>(0, static_cast<qint64>(Reader::AudioTap::Instance().Position()) - static_cast<qint64>(Lines::Feed::SettleMs) * Reader::AudioTap::SampleRate / 1000) : -1;
    line.channel = ReaderApp::ChannelDialogue();
    line.text = text;
    line.source_id = Lines::SourceId(text);
    return line;
}
} // namespace

HookSource::HookSource(QObject* parent) : ReaderUi::ReadingSource(parent) {
    settle.setSingleShot(true);
    settle.setInterval(static_cast<int>(Lines::Feed::SettleMs));
    connect(&settle, &QTimer::timeout, this, &HookSource::SettleAll);
    look.setInterval(1500);
    connect(&look, &QTimer::timeout, this, &HookSource::Look);
    connect(&runner, &Hooks::Runner::Line, this, &HookSource::Arrived);
    connect(&runner, &Hooks::Runner::Changed, this, &HookSource::StatusChanged);
    connect(&runner, &Hooks::Runner::Ended, this, [this](const QString& why) {
        if (running) {
            // The game was closed, or the engine could not hold it: looked for again.
            waiting = why;
            look.start();
            emit StatusChanged();
        }
    });
}

ReaderUi::ReadingSource::About HookSource::Describe() const {
    return {QStringLiteral("hook"), Label(), false};
}

QString HookSource::Label() const {
    const auto engine = Hooks::EngineOf(title.engine);
    return QStringLiteral("HOOK  ·  ") + (engine ? Hooks::LabelOf(*engine) : title.engine.toUpper());
}

bool HookSource::Start(const Lines::Title& title_, const QString& shared_, QString& error) {
    Stop();
    if (!Hooks::EngineOf(title_.engine)) {
        error = QStringLiteral("\"%1\" is no hook engine").arg(title_.engine);
        return false;
    }
    if (title_.process.isEmpty()) {
        error = QStringLiteral("%1 does not say which program it is hooked in").arg(title_.title);
        return false;
    }
    title = title_;
    shared = shared_;
    feeds.clear();
    waiting.clear();
    running = true;
    emit Began(title.id, title.title);
    Look();
    look.start();
    emit StatusChanged();
    return true;
}

void HookSource::Stop() {
    if (!running) {
        return;
    }
    look.stop();
    settle.stop();
    SettleAll();
    runner.Stop();
    running = false;
    emit Ended();
    emit StatusChanged();
}

void HookSource::Look() {
    if (!running || runner.Running()) {
        return;
    }
    const auto program = Hooks::ProgramNamed(title.process);
    if (!program) {
        if (waiting.isEmpty()) {
            waiting = QStringLiteral("not running");
            emit StatusChanged();
        }
        return;
    }
    Hooks::Plan plan;
    plan.engine = *Hooks::EngineOf(title.engine);
    plan.pid = program->pid;
    plan.is32 = program->is32;
    plan.script = title.script;
    plan.codes = title.codes;
    plan.title = title.id;
    plan.shared = shared;
    QString error;
    if (!runner.Start(plan, error)) {
        waiting = error;
        look.setInterval(6000); // what is missing does not come by itself in a second
    } else {
        waiting.clear();
        look.setInterval(1500);
        feeds.clear();
    }
    emit StatusChanged();
}

QString HookSource::Status() const {
    if (!running) {
        return {};
    }
    if (runner.Running()) {
        return QStringLiteral("%1  ·  %2").arg(runner.Status(), title.process);
    }
    return QStringLiteral("waiting for %1%2").arg(title.process, waiting.isEmpty() || waiting == QStringLiteral("not running") ? QString() : QStringLiteral("  ·  ") + waiting);
}

bool HookSource::Chosen(const Hooks::Thread& thread) const {
    if (title.engine == QStringLiteral("agent")) {
        return true; // a script is one stream, and it is the choice
    }
    if (title.threads.contains(thread.key)) {
        return true;
    }
    // The thread's contexts are another's this time (a build of the game that moved its code
    // by less than 64 kB): the same hook is the same thread, where only one chosen thread has it.
    const QString hook = thread.key.section(QLatin1Char('|'), 0, 1);
    int same = 0;
    for (const QString& key : title.threads) {
        same += key.section(QLatin1Char('|'), 0, 1) == hook ? 1 : 0;
    }
    if (same != 1) {
        return false;
    }
    for (const QString& key : title.threads) {
        if (runner.Threads().end() != std::find_if(runner.Threads().begin(), runner.Threads().end(), [&](const Hooks::Thread& seen) { return seen.key == key; })) {
            return false; // the chosen one itself is there
        }
    }
    return true;
}

void HookSource::Arrived(const Hooks::Thread& thread, const QString& text) {
    if (!running || !Chosen(thread)) {
        return;
    }
    Lines::Feed& feed = feeds[thread.key];
    for (const Lines::Feed::Out& out : feed.Take(text, QDateTime::currentMSecsSinceEpoch())) {
        HandOver(out);
    }
    if (feed.HasPending()) {
        settle.start();
    }
}

void HookSource::SettleAll() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Lines::Feed& feed : feeds) {
        if (const auto out = feed.Settle(now)) {
            HandOver(*out);
        }
    }
}

void HookSource::HandOver(const Lines::Feed::Out& out) {
    if (out.takes_back) {
        ReaderUi::ReaderSession::Instance().TakeBack(LineOf(*out.takes_back, 0));
    }
    emit Line(LineOf(out.text, ++sequence));
}

} // namespace Home
