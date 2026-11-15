// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A game a core plays that has no script, read by OCR (docs/furoshiki-3.md, "A core's game read
// by OCR"). The picture is the core's own (reader_core.h, READER_CORE_GRAB_PICTURE: the frame
// cards are made from), not a window of the desktop; it goes to the OCR helper or service
// (ocr_client.h) with the game's boxes, as a window's does, but only while what stands in the
// boxes changes (ocr_gate.h): the core copies a frame four times a second on its own thread and
// is never waited for, and nothing is sent while the text stands. The lines the helper reads
// are the running session's, as a script's would be (ReaderSession::PostLine): counted under
// the game's own ID, kept in the backlog, made into cards with the game's picture and sound.
// Where the helper says where each character is, that goes in as the line's place on the
// screen (ReaderSession::PostLayout), so the game's own text is what is pointed at.

#include <functional>
#include <QHash>
#include <QObject>
#include <QTimer>
#include "line_titles.h"
#include "ocr_client.h"
#include "ocr_gate.h"

namespace Home {

class CoreOcr : public QObject {
    Q_OBJECT

public:
    explicit CoreOcr(QObject* parent = nullptr);

    /// Begins reading the running game as `game` says (its area and boxes) with `helper`.
    /// False, with `error`, when no helper's address is set or the game has no area.
    bool Start(const Lines::CoreGame& game, const Lines::Helper& helper, QString& error);
    void Stop();
    [[nodiscard]] bool Running() const {
        return running;
    }
    [[nodiscard]] const QString& GameId() const {
        return game.id;
    }
    /// For the user: what the helper says.
    [[nodiscard]] QString Status() const {
        return running ? client.Status() : QString();
    }
    /// Reads the game's boxes that are read only on request, now, from the picture as it is.
    void ReadAsked(std::function<void(bool ok, const QString& what)> then = nullptr);
    [[nodiscard]] OcrClient& Client() {
        return client;
    }
    [[nodiscard]] const OcrGate::Gate& Gate() const {
        return gate;
    }
    /// Lines handed to the session, and whether the last one has its place on the screen.
    [[nodiscard]] int Handed() const {
        return count;
    }
    [[nodiscard]] bool Placed() const {
        return placed;
    }

signals:
    void StatusChanged();

private:
    void Tick();
    void Took(const QImage& picture, bool always);
    void Place();

    OcrClient client;
    OcrGate::Gate gate;
    QTimer tick;
    Lines::CoreGame game;
    QList<QRectF> watched; ///< The boxes read by themselves: the ones a change is looked for in
    bool running{};
    bool grabbing{};
    int generation{};
    qint64 sequence{};
    qint64 began_seen{-1};
    qint64 audio_began{-1}; ///< Where the game's sound was when the boxes began to change
    QHash<qint64, QString> handed;
    qint64 last_id{-1};
    QList<QRectF> boxes;
    bool on_screen{}, placed{};
    int count{};
};

} // namespace Home
