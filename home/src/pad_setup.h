// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QRectF>
#include <QTimer>
#include <QWidget>
#include "pad_names.h"

namespace Home {

/**
 * A controller's first use, in three steps along a row: Letters, Check, Shortcuts. A pad says
 * what kind it is, not what is printed on it (an 8BitDo pad in X-input mode says Xbox and may have
 * the Nintendo letters), so nothing here relies on a face button's meaning before it is known: the
 * d-pad moves (right goes on, left goes back, up and down choose). Letters: the button marked A
 * pressed, at the bottom the Xbox letters, on the right the Nintendo ones, or the row for × ○ △ □.
 * Check: what each face button is in Furoshiki and on each console. Shortcuts: whether to map them
 * now. From the Check on, the bottom button confirms and the right one goes back too, named by this
 * controller's letters. The keys (Enter, Esc, the arrows) and the mouse work it as well.
 */
class PadSetup : public QWidget {
public:
    struct Look {
        QColor ground, raised, ink, muted, accent, rule;
        std::function<QFont(int face, double px, double spacing)> font;
    };

    /// The third step's shortcuts, as the program keeps them (pad_shortcuts.h).
    struct Shortcuts {
        QStringList actions;                                         ///< The rows, in order
        std::function<QString(const QString& action)> label;         ///< "Backlog"
        /// "L3", "Select + R3", or "None", by the letters answered
        std::function<QString(const QString& action, const PadNames::Names& letters)> current;
        /// An input or a chord ("select+l3") by the letters answered
        std::function<QString(const QString& input, const PadNames::Names& letters)> name;
        std::function<QString(const QString& input)> conflict;       ///< What reading already does with it, or empty
        std::function<bool(const QString& action, const QString& input)> assign;
        QString free;                                                ///< The free buttons, said first in a hint
    };

    /// `names`: what the pad's buttons are called by its kind (Select, LB...), the face letters
    /// then set by the answer. `reported`: the layout it says it has, the answer until one is given.
    /// `chosen` is told the layout once the Check is confirmed; `finished` whether to map the
    /// shortcuts now (false also when closed with Esc).
    PadSetup(QWidget* over, Look look, QString pad_name, PadNames::Names names, int reported, Shortcuts shortcuts,
             std::function<void(int family)> chosen, std::function<void(bool set_up)> finished);

    /// A controller's button went down, by its place: "south", "east", "west", "north", "up",
    /// "down", "left", "right".
    void Pressed(const QString& input);
    /// A controller's button went up (any input's id: "l3", "select", "grip1"), for learning
    /// a shortcut from one button or two held together.
    void Released(const QString& input);
    /// Whether a shortcut is being learnt (every button is then the setup's).
    [[nodiscard]] bool Learning() const { return learning; }
    /// 0 Letters, 1 Check, 2 Shortcuts.
    [[nodiscard]] int Step() const { return step; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override; ///< Follows the window's size

private:
    [[nodiscard]] double Scale() const;
    [[nodiscard]] QPointF ToSheet(const QPointF& at) const;
    [[nodiscard]] PadNames::Names Shown() const; ///< The names with the answer's letters
    void Next();
    void Back();
    void Finish(bool set_up);
    void PaintSteps(QPainter& p);
    void PaintFoot(QPainter& p, const QList<QPair<QString, QString>>& keys);
    void Learnt(const QString& input);

    Look look;
    QString pad_name;
    PadNames::Names names;
    int reported{0};
    std::function<void(int)> chosen;
    std::function<void(bool)> finished;
    int step{0};
    int answer{0};       ///< The layout answered (a family); the reported one until a press
    bool pressed{};      ///< Whether the answer came from a press
    bool playstation_row{}; ///< Step 0: the × ○ △ □ row chosen with the d-pad
    Shortcuts shortcuts;
    int choice{0};       ///< Step 2: the shortcut row in hand (actions.size(): Done)
    bool learning{};
    QSet<QString> learn_down;  ///< Held while learning
    QStringList learn_got;     ///< Pressed while learning, in order
    QString pending;           ///< A button warned about, taken if pressed again
    QString note;
    bool done{};
    QElapsedTimer clock;
    qint64 step_at{0};
    QTimer frames;
    QList<QPair<QRectF, int>> hits; ///< On the sheet: 0..2 a layout row, 10 Next, 11 Back, 20/21 a step 2 choice
};

} // namespace Home
