// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A title read by a hook Furoshiki starts itself (docs/hooks.md; docs/furoshiki-3.md, 9): a
// reading source as line_source.h is, whose lines come from an engine run against the game's
// process (hook_runner.h). The title remembers its engine, its script or hook codes, its
// program's name and the threads that are its dialogue (line_titles.h). While it is read the
// program is looked for; when it runs it is hooked, and hooked again when it was closed and
// started anew. The chosen threads' lines go through the lines' rules (line_feed.h), each
// thread by itself, and then the way a core's lines go.

#include <QHash>
#include <QTimer>
#include "hook_runner.h"
#include "line_feed.h"
#include "line_titles.h"
#include "reader/qt/reader_session.h"

namespace Home {

class HookSource : public ReaderUi::ReadingSource {
    Q_OBJECT

public:
    explicit HookSource(QObject* parent = nullptr);

    [[nodiscard]] About Describe() const override;

    bool Start(const Lines::Title& title, const QString& shared, QString& error);
    void Stop();
    [[nodiscard]] bool Running() const {
        return running;
    }
    [[nodiscard]] const QString& GameId() const {
        return title.id;
    }
    /// The title it reads, as it was started with.
    [[nodiscard]] const Lines::Title& Of() const {
        return title;
    }
    [[nodiscard]] const QString& Title() const {
        return title.title;
    }
    /// "HOOK  ·  AGENT", for the strip's head.
    [[nodiscard]] QString Label() const;
    /// For the user: "waiting for game.exe", "hooked  ·  game.exe", a fault.
    [[nodiscard]] QString Status() const;
    [[nodiscard]] const Hooks::Runner& Engine() const {
        return runner;
    }
    /// How many lines were handed to the reader.
    [[nodiscard]] qint64 Handed() const {
        return sequence;
    }

signals:
    void StatusChanged();

private:
    void Look();
    void Arrived(const Hooks::Thread& thread, const QString& text);
    void HandOver(const Lines::Feed::Out& out);
    void SettleAll();
    [[nodiscard]] bool Chosen(const Hooks::Thread& thread) const;

    Hooks::Runner runner;
    Lines::Title title;
    QString shared;
    QHash<QString, Lines::Feed> feeds;
    QTimer settle, look;
    QString waiting; ///< Why it is not hooked now
    bool running{};
    qint64 sequence{};
};

} // namespace Home
