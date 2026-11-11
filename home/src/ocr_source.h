// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A window of this computer read by OCR, as a reading source (docs/furoshiki-3.md, "A window
// read by OCR"): the chosen window's picture goes to the OCR helper about four times a second
// (window_list.h, ocr_client.h), and the lines the helper reads in the title's area come back
// and go the way a core's do (ReaderUi::ReadingSource): counted, kept for the Log, looked up,
// made into cards. The helper has already made them lines (its stabiliser waits for the text to
// stand still, its cleaner drops furigana, repeats and what is not Japanese, and a line read
// while the game was still writing it is replaced by the whole one: that one is taken back here).

#include <QHash>
#include <QTimer>
#include "line_titles.h"
#include "ocr_client.h"
#include "reader/qt/reader_session.h"
#include "window_list.h"

namespace Home {

class OcrSource : public ReaderUi::ReadingSource {
    Q_OBJECT

public:
    explicit OcrSource(QObject* parent = nullptr);

    [[nodiscard]] About Describe() const override;

    /// Begins reading `title` (by "ocr": its window and area) with the helper `helper`. False,
    /// with `error`, when no helper's address is set or the title names no window.
    bool Start(const Lines::Title& title, const Lines::Helper& helper, QString& error);
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
    /// For the user: whether the window is there and what the helper says.
    [[nodiscard]] QString Status() const;
    /// The characters' places of the line handed over last, 0-1 of the window's picture.
    [[nodiscard]] const QList<QRectF>& Boxes() const {
        return boxes;
    }
    /// Whether that line is on the screen now, as far as the helper knows.
    [[nodiscard]] bool OnScreen() const {
        return on_screen;
    }
    /// Where the window's picture is on the screen now; empty where that is not known.
    [[nodiscard]] QRect Place() const {
        return place;
    }
    /// Reads the title's boxes that are read only on request, now (the key for it).
    void ReadAsked(std::function<void(bool ok, const QString& what)> then = nullptr) {
        if (running) {
            client.ReadAsked(std::move(then));
        }
    }
    [[nodiscard]] OcrClient& Client() {
        return client;
    }

signals:
    void StatusChanged();
    /// The line handed over last came onto the screen or left it, or the window moved.
    void ScreenChanged();

private:
    void Tick();

    OcrClient client;
    QTimer tick;
    Lines::Title title;
    Windows::Entry window;
    bool running{};
    bool window_there{};
    qint64 sequence{};
    QHash<qint64, QString> handed; ///< The helper's line IDs handed over, and their text
    qint64 last_id{-1};
    QList<QRectF> boxes;
    bool on_screen{};
    QRect place;
};

} // namespace Home
