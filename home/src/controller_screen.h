// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include "controller_map.h"
#include "pad_names.h"

namespace Home {

/**
 * The controller map: over the whole window, in the shelf's look, every action with the button
 * it is on and a Learn for each; a tab for all consoles and one for each console, whose own
 * mapping wins while its games play; the console's controller drawn beside, the action's
 * button lit on it, a button pressed glowing a moment. The controller works it (the window
 * hands it the buttons, ControllerScreen::Pressed), as do the mouse and the keys.
 */
class ControllerScreen : public QWidget {
public:
    struct Look {
        QColor ground, raised, ink, muted, accent, rule;
        /// The shelf's faces at a size in the window's pixels: 0 display, 1 text, 2 mono.
        std::function<QFont(int face, double px, double spacing)> font;
    };
    struct Console {
        QString key, name; ///< "ps2", "PS2"; the first is all consoles ("", "ALL CONSOLES")
    };

    ControllerScreen(QWidget* over, Look look, ControllerMap::Store store, QList<Console> consoles,
                     std::function<void()> closed);

    /// Whether a press is to be learnt (the window then gives it here as it is, not as a key).
    [[nodiscard]] bool Learning() const { return learning >= 0; }
    /// A button of the controller went down or up (an input's id: "south", "grip1"): learnt
    /// while learning, else lit on the drawing and, from a controller, the screen's own keys.
    void Pressed(const QString& input, bool down, bool from_pad = true);
    /// The controller in the hand: SDL's name of it (empty: none), what it calls its buttons,
    /// and whether the Steam Controller bridge has a Steam Controller (its drawing and grips).
    void SetHand(const QString& name, const PadNames::Names& names, bool steam);
    /// A key (the keyboard's, or a controller's turned into one).
    void Key(int key);
    /// The row in hand on the first shortcut (the list's Shortcuts), as the setup opens it.
    void ShowShortcuts();
    void Close();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] double Scale() const;
    [[nodiscard]] QPointF ToSheet(const QPointF& at) const;
    [[nodiscard]] QString ConsoleKey() const;
    [[nodiscard]] const ControllerMap::Action& ActionAt(int row) const;
    [[nodiscard]] int Rows() const;
    void Learn(int row);
    void Stop();
    void Say(const QString& text);
    void ShowRow(int row);
    void PaintPad(QPainter& p, const QRectF& area);

    Look look;
    ControllerMap::Store store;
    QList<Console> consoles;
    std::function<void()> closed;
    QList<ControllerMap::Action> actions;
    /// The list on the right: the console's own buttons, then the shortcuts, a head over each.
    struct Line {
        enum Kind { Head, Game, Shortcut } kind;
        QString id;    ///< A game button's
        QString label;
        int action{-1}; ///< A shortcut's, in `actions`
    };
    QList<Line> lines;
    /// The input wizard: every button of the tab's console asked for in turn, the controller's
    /// press its answer. While it runs every button is its own: nothing moves about the screen.
    QStringList wizard_steps; ///< Empty: not running
    int wizard_at{0};
    QHash<QString, QString> wizard_got;
    QSet<QString> wizard_held;
    qint64 wizard_since{0};
    QString wizard_note;
    void StartWizard();
    void WizardStep(const QString& input);
    void WizardBack();
    void EndWizard(bool keep);
    [[nodiscard]] QString GameName(const QString& id) const;
    void Build();
    [[nodiscard]] QString HandName(const QString& input) const;
    /// The pad every tab draws: the controller in the hand.
    [[nodiscard]] const ControllerMap::Pad& ShownPad() const;
    /// The console's own pad (its buttons and their names), else the controller in the hand.
    [[nodiscard]] const ControllerMap::Pad& ConsolePad() const;
    /// A button's name on a drawn pad (the common gamepad's by the pad in the hand).
    [[nodiscard]] QString LabelIn(const ControllerMap::Pad& pad, const QString& id) const;
    QString hand_name;
    PadNames::Names hand_names = PadNames::OfFamily(PadNames::Xbox);
    bool hand_steam{};
    int tab{0};
    int row{0};
    int hover{-1};
    int learning{-1};
    double top{0};
    qint64 learn_since{0};
    QSet<QString> lit;
    QHash<QString, qint64> lit_at; ///< When a button last went down, for its glow
    QString notice;
    qint64 notice_at{0};
    QElapsedTimer clock;
    QTimer frames;
    bool leaving{};
    qint64 left_at{0};
    struct Hit {
        QRectF area;
        int kind; ///< 0 a tab, 1 a row, 2 a row's Learn, 3 Same as all consoles
        int index;
    };
    QList<Hit> hits;
};

} // namespace Home
