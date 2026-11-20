// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QHash>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>

/**
 * What the controller's buttons do, as the controller map (controller_screen.h) shows and sets
 * it: the shared data (shared/controllers/controllers.json, the Android app's too), and the
 * settings it is kept in. All consoles' layer is the reader's own settings, as they always were
 * (reader_cursor_button, reader_menu_button, reader_stats_button, reader_back_button_1..5) with
 * reader_button_action_<number> for any other action on a button; a console's layer is the same
 * keys in the program's layers for that console (play_settings.h, "controller.<key>"), which win
 * while one of its games plays.
 */
namespace Home::ControllerMap {

struct Input {
    QString id;     ///< "south", "l3", "grip1"
    int number{-1}; ///< The reader's button number (Android's key code), or -1 for a back button
    int back{0};    ///< 1 to 5: the Steam Controller's grips and its "..." button
    QString label;  ///< For a back button
};

struct Action {
    QString id;      ///< As the settings name it: "reader_backlog", "pointer", "furoshiki_guide"
    QString label;
    QString setting; ///< A button setting of its own (the pointer's, the menu's, the clock's), or empty
    bool held{};     ///< Held down, so only a button the reader's filter can keep (not a back button)
    bool desktop{};
};

/// One thing a pad's drawing is made of, in the order they are painted (shared/controllers/draw.py).
struct Item {
    enum Kind { Body, Part, Line, Key } kind{Line};
    QString id, label;   ///< A key's
    QPainterPath path;   ///< On a 400 by 260 grid
    QPainterPath glyph;  ///< Drawn in the label's place (the PlayStation's shapes); empty: none
    bool strong{};       ///< A line in the ink's own
    bool sunk{};         ///< A line filled as the ground (a screen)
    bool quiet{};        ///< A key whose label is not written
    bool edge{true};     ///< A key with a line of its own
    QPointF label_at;
    double label_size{8};
};

struct Pad {
    QString id, name;
    QList<Item> draw;
    /// The pad's own name for a button by its id ("△", "ZL"), or empty.
    [[nodiscard]] QString LabelOf(const QString& id) const;
};

struct Data {
    QList<Input> inputs;
    QList<Action> actions; ///< Those of the desktop program
    QHash<QString, QString> pad_of; ///< Console key ("ps2", "all") to pad id
    QHash<QString, Pad> pads;
};

/// The shared data, read once from the program's resources.
[[nodiscard]] const Data& Load();
/// The pad a console's tab shows; the Steam Controller for all consoles and any console without one.
[[nodiscard]] const Pad& PadOf(const QString& console);
/// An input by its id or its number; null when there is none.
[[nodiscard]] const Input* InputById(const QString& id);
[[nodiscard]] const Input* InputByNumber(int number);
/// What a pad calls an input: its own label, else the input's ("Grip 1"), else its id in capitals.
[[nodiscard]] QString NameOf(const Pad& pad, const QString& input);
/// An SVG path of M, L, C, Q and Z (absolute) as a painter path.
[[nodiscard]] QPainterPath ParsePath(const QString& text);

/// Where the bindings are kept: a key's value in one layer (console empty: all consoles), empty
/// when that layer does not set it; set with an empty value takes it out of that layer.
struct Store {
    std::function<QString(const QString& key, const QString& console)> get;
    std::function<void(const QString& key, const QString& value, const QString& console)> set;
    std::function<void()> save;
};

/// Every key the map keeps.
[[nodiscard]] QStringList Keys();
/// A key's value for a console: its layer's, else all consoles', else what it is by itself.
[[nodiscard]] QString Resolved(const Store& store, const QString& key, const QString& console, bool* inherited = nullptr);

struct Binding {
    QString input;   ///< An input's id
    bool inherited{}; ///< From all consoles' layer, on a console's tab
};
/// The inputs an action is on, for a console (empty: all consoles).
[[nodiscard]] QList<Binding> BindingsOf(const Store& store, const Action& action, const QString& console);
/// The action an input does there, or empty.
[[nodiscard]] QString ActionOn(const Store& store, const QString& input, const QString& console);

/// The console's own buttons, each on a button of the controller in the hand: pad_button_<id>
/// is the controller's button (an input's id) that presses the console's button `id`; by itself
/// each is its own. The keys a pad has, in the order they are drawn, each once.
[[nodiscard]] QStringList GameButtons(const Pad& pad);
/// The controller's button that presses the console's `button` there ("none": nothing does).
[[nodiscard]] QString GameSource(const Store& store, const QString& button, const QString& console, bool* inherited = nullptr,
                                 bool* changed = nullptr);
/// Puts the console's `button` on the controller's `input`; what was on that input takes the
/// one `button` leaves, so that no two are on one. False for a back button (it cannot be held).
bool AssignGame(const Store& store, const QString& button, const QString& input, const QString& console);
/// The input wizard's answers at once (console button to the controller's input pressed for
/// it), as they were pressed: no swaps. A button passed over keeps what it had, unless that is
/// now another's: then nothing presses it.
void SetGame(const Store& store, const QHash<QString, QString>& given, const QString& console);
/// The console's button back on its own.
void ClearGame(const Store& store, const QString& button, const QString& console);

/// Two buttons held together that do an action: their reader numbers, their id ("select+l3"),
/// the action. A chord's id is two inputs' ids joined by "+", in Assign and Binding alike.
struct ChordBinding {
    int a{-1}, b{-1};
    QString id, action;
};
/// The chords that do an action in that layer (all consoles' under a console's own).
[[nodiscard]] QList<ChordBinding> Chords(const Store& store, const QString& console);

/// Puts `action` on `input` (or on a chord, "select+l3") in that layer, alone: the action leaves the inputs it was on and
/// what was on the input leaves it. False (and nothing changed) where it cannot be: a held
/// action on a back button.
bool Assign(const Store& store, const Action& action, const QString& input, const QString& console);
/// Takes the action off every input in that layer.
void Clear(const Store& store, const Action& action, const QString& console);
/// A console's layer emptied: all consoles' bindings again.
void Reset(const Store& store, const QString& console);
/// Whether a console's layer sets anything.
[[nodiscard]] bool HasOwn(const Store& store, const QString& console);

} // namespace Home::ControllerMap
