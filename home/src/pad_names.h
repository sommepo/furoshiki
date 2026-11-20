// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <QList>
#include <QString>

/**
 * What the buttons of the controller in the hand are called, by its kind: the one place the
 * program names them (the controller screen, the feet's caps, the popup's key marks, the reading
 * hints, "Start reading with", the Keys page). SDL numbers buttons by position (south, east, west,
 * north), so the letter written on each comes from the pad's kind. No SDL here: the caller turns
 * SDL's controller type into a Kind (main_window.cpp), so this can be tested on its own.
 */
namespace Home::PadNames {

/// SDL's controller types, as far as the names go.
enum class Kind {
    Unknown,
    Xbox360,
    XboxOne, ///< SDL's "Xbox One", which also covers the Series pads
    PS3,
    PS4,
    PS5,
    SwitchPro, ///< and the Joy-Cons, and other pads that are Switch-style
    Virtual,
    Other, ///< Amazon Luna, Google Stadia, NVIDIA Shield and the like: lettered as an Xbox pad
};

/// The face letters' family; also the value the pages call a pad style (0, 1, 2).
enum Family { Xbox = 0, PlayStation = 1, Nintendo = 2 };

struct Pad {
    Kind kind{Kind::Unknown};
    uint16_t vendor{};
    uint16_t product{};
    QString name; ///< SDL's name of it
};

struct Names {
    int family{Xbox};
    QString south, east, west, north;
    QString select, start;
    QString l1, r1, l2, r2, l3, r3;
    bool operator==(const Names&) const = default;
};

/// The names of a pad's buttons.
[[nodiscard]] Names Of(const Pad& pad);
/// The names of a family's pads with Select and Start (a page script's "pad xbox", the pages'
/// feet, which only show the face and shoulder buttons).
[[nodiscard]] Names OfFamily(int family);
/// The face buttons' letters as a layout has them printed (Xbox, PlayStation, Nintendo: a
/// family), the rest of the names kept: an 8BitDo pad in X-input mode says it is an Xbox pad but
/// may have the Nintendo letters on it. The buttons still work by place (south confirms).
[[nodiscard]] Names WithLayout(const Names& names, int family);
/// A layout as it is kept ("xbox", "nintendo", "playstation") and back; -1 for none.
[[nodiscard]] QString LayoutKey(int family);
[[nodiscard]] int LayoutOf(const QString& key);
/// The layout the setup takes from the button pressed for "A": south means the Xbox layout, east
/// the Nintendo one; -1 for any other.
[[nodiscard]] int LayoutFromPress(const QString& input);
/// A button's name by the controller map's id ("south", "select", "l1", "up"), or empty.
[[nodiscard]] QString Label(const Names& names, const QString& id);

/// What the setup says of a layout answered ("A is on the right: Nintendo letters").
[[nodiscard]] QString LayoutAnswer(int family);
/// The buttons that confirm and go back in Furoshiki, by this controller's letters: always the
/// bottom and the right one (south, east), whatever is printed on them.
[[nodiscard]] QString ConfirmName(const Names& names);
[[nodiscard]] QString BackName(const Names& names);

/// What each face button, by its place, is on a console (or in Furoshiki's own menus), as the
/// program hands it to the cores: by the core's names for its buttons, so the bottom button is
/// × on the PlayStations and A on the Switch, the 3DS, the DS and the Dreamcast.
struct ConsoleFaces {
    QString console;
    QString south, east, west, north;
};
[[nodiscard]] QList<ConsoleFaces> FacesOnConsoles();
/// A console's name (its key: "ps2", "switch", "3ds") for the face button at a place ("south"),
/// as the program hands it over; empty for another console, place or key.
[[nodiscard]] QString ConsoleFaceName(const QString& console, const QString& place);

/// The names of the controller in use now, for code that has no window to ask (reader_rows.cpp).
/// The program sets it when the controller in use changes.
void SetCurrent(const Names& names);
[[nodiscard]] Names Current();

} // namespace Home::PadNames
