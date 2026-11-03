// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QPushButton;

namespace ReaderUi {

/**
 * What a card asks before it is made, in a game whose cards ask about their audio: whether it
 * gets what the game played during its line. Three buttons with signs and no words, in the
 * dictionary popup's look: ▶ listens to the clip (■ while it plays), a speaker with a tick
 * makes the card with it, a speaker with a cross without it; the words are their tooltips.
 * A window of its own, like the popup, that never takes the keyboard from the game.
 */
class AudioPrompt : public QWidget {
    Q_OBJECT

public:
    /// For this long after it came up, the question takes no answer: the press that asked for
    /// the card is not one.
    static constexpr int GuardMs = 300;

    explicit AudioPrompt(QWidget* parent);

    /// Makes the question ready to be shown, its signs at `scale` percent of the normal size
    /// (the dictionary popup's text size setting).
    void Prepare(int scale);
    /// While the clip plays, the first button stops it.
    void SetPlaying(bool playing);
    [[nodiscard]] bool IsPlaying() const {
        return playing;
    }

signals:
    void ListenClicked();
    void KeepClicked();
    void DropClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QPushButton* listen;
    QPushButton* keep;
    QPushButton* drop;
    QElapsedTimer shown;
    bool playing{};
    int sign_px{22};
};

} // namespace ReaderUi
