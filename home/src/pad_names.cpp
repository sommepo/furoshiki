// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <mutex>
#include "pad_names.h"

namespace Home::PadNames {

namespace {

constexpr uint16_t Microsoft = 0x045E;
/// An Xbox One S controller over Bluetooth: what 8BitDo pads in X-input mode say they are.
constexpr uint16_t XboxOneSBluetooth = 0x02E0;

std::mutex current_mutex;
Names current = OfFamily(Xbox);

} // namespace

Names OfFamily(int family) {
    Names names;
    names.family = family;
    names.l3 = QStringLiteral("L3");
    names.r3 = QStringLiteral("R3");
    names.select = QStringLiteral("Select");
    names.start = QStringLiteral("Start");
    if (family == PlayStation) {
        names.south = QStringLiteral("×");
        names.east = QStringLiteral("○");
        names.west = QStringLiteral("□");
        names.north = QStringLiteral("△");
        names.l1 = QStringLiteral("L1");
        names.r1 = QStringLiteral("R1");
        names.l2 = QStringLiteral("L2");
        names.r2 = QStringLiteral("R2");
    } else if (family == Nintendo) {
        // By position: the Nintendo layout has B at the bottom and A on the right.
        names.south = QStringLiteral("B");
        names.east = QStringLiteral("A");
        names.west = QStringLiteral("Y");
        names.north = QStringLiteral("X");
        names.l1 = QStringLiteral("L");
        names.r1 = QStringLiteral("R");
        names.l2 = QStringLiteral("ZL");
        names.r2 = QStringLiteral("ZR");
        names.select = QStringLiteral("−");
        names.start = QStringLiteral("+");
    } else {
        names.family = Xbox;
        names.south = QStringLiteral("A");
        names.east = QStringLiteral("B");
        names.west = QStringLiteral("X");
        names.north = QStringLiteral("Y");
        names.l1 = QStringLiteral("LB");
        names.r1 = QStringLiteral("RB");
        names.l2 = QStringLiteral("LT");
        names.r2 = QStringLiteral("RT");
    }
    return names;
}

Names Of(const Pad& pad) {
    // 8BitDo pads say they are other makers' pads (an Xbox One S over Bluetooth in X-input mode).
    // Theirs are marked Select and Start, with the face letters where an Xbox pad has them,
    // unless the pad is in its Switch mode.
    if (pad.name.contains(QStringLiteral("8BitDo"), Qt::CaseInsensitive)) {
        return OfFamily(pad.kind == Kind::SwitchPro ? Nintendo : Xbox);
    }
    switch (pad.kind) {
    case Kind::PS3:
        return OfFamily(PlayStation); // a DualShock 3 is marked Select and Start
    case Kind::PS4: {
        Names names = OfFamily(PlayStation);
        names.select = QStringLiteral("Share");
        names.start = QStringLiteral("Options");
        return names;
    }
    case Kind::PS5: {
        Names names = OfFamily(PlayStation);
        names.select = QStringLiteral("Create");
        names.start = QStringLiteral("Options");
        return names;
    }
    case Kind::SwitchPro:
        return OfFamily(Nintendo);
    case Kind::Xbox360:
        return OfFamily(Xbox);
    default:
        break;
    }
    Names names = OfFamily(Xbox);
    // View and Menu only on what is surely an Xbox One or Series pad: not 045E:02E0, which other
    // makers' pads claim to be.
    const bool claimed = pad.vendor == Microsoft && pad.product == XboxOneSBluetooth;
    const bool xbox_one = pad.kind == Kind::XboxOne ||
                          (pad.kind == Kind::Unknown && pad.name.contains(QStringLiteral("Xbox"), Qt::CaseInsensitive) &&
                           !pad.name.contains(QStringLiteral("360")));
    if (xbox_one && !claimed) {
        names.select = QStringLiteral("View");
        names.start = QStringLiteral("Menu");
    }
    return names;
}

Names WithLayout(const Names& names, int family) {
    if (family != Xbox && family != PlayStation && family != Nintendo) {
        return names;
    }
    const Names face = OfFamily(family);
    Names out = names;
    out.family = family;
    out.south = face.south;
    out.east = face.east;
    out.west = face.west;
    out.north = face.north;
    return out;
}

QString LayoutKey(int family) {
    switch (family) {
    case Xbox:
        return QStringLiteral("xbox");
    case PlayStation:
        return QStringLiteral("playstation");
    case Nintendo:
        return QStringLiteral("nintendo");
    default:
        return {};
    }
}

int LayoutOf(const QString& key) {
    if (key == QStringLiteral("xbox")) {
        return Xbox;
    }
    if (key == QStringLiteral("playstation")) {
        return PlayStation;
    }
    if (key == QStringLiteral("nintendo")) {
        return Nintendo;
    }
    return -1;
}

int LayoutFromPress(const QString& input) {
    return input == QStringLiteral("south") ? Xbox : input == QStringLiteral("east") ? Nintendo : -1;
}

QString Label(const Names& names, const QString& id) {
    if (id == QStringLiteral("south")) {
        return names.south;
    }
    if (id == QStringLiteral("east")) {
        return names.east;
    }
    if (id == QStringLiteral("west")) {
        return names.west;
    }
    if (id == QStringLiteral("north")) {
        return names.north;
    }
    if (id == QStringLiteral("select")) {
        return names.select;
    }
    if (id == QStringLiteral("start")) {
        return names.start;
    }
    if (id == QStringLiteral("l1")) {
        return names.l1;
    }
    if (id == QStringLiteral("r1")) {
        return names.r1;
    }
    if (id == QStringLiteral("l2")) {
        return names.l2;
    }
    if (id == QStringLiteral("r2")) {
        return names.r2;
    }
    if (id == QStringLiteral("l3")) {
        return names.l3;
    }
    if (id == QStringLiteral("r3")) {
        return names.r3;
    }
    return {};
}

QString LayoutAnswer(int family) {
    switch (family) {
    case Nintendo:
        return QStringLiteral("A is on the right: Nintendo letters");
    case PlayStation:
        return QStringLiteral("× ○ △ □: PlayStation letters");
    default:
        return QStringLiteral("A is at the bottom: Xbox letters");
    }
}

QString ConfirmName(const Names& names) {
    return names.south;
}

QString BackName(const Names& names) {
    return names.east;
}

QList<ConsoleFaces> FacesOnConsoles() {
    // As main_window.cpp's Roles hand them over: the bottom button is the core's "Cross" or "A",
    // the right one "Circle" or "B", the left "Square" or "X", the top "Triangle" or "Y". The
    // Nintendo cores (Eden, Azahar, melonDS) name their buttons A, B, X, Y, so the bottom button
    // is their A, by name, though a Nintendo pad has A on the right. Flycast's A is the bottom one.
    const QString none = QStringLiteral("–");
    return {
        {QStringLiteral("Furoshiki"), QStringLiteral("Confirm"), QStringLiteral("Back"), none, none},
        {QStringLiteral("PS1, PS2, PSP, PS Vita"), QStringLiteral("×"), QStringLiteral("○"), QStringLiteral("□"), QStringLiteral("△")},
        {QStringLiteral("Switch"), QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("X"), QStringLiteral("Y")},
        {QStringLiteral("3DS and DS"), QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("X"), QStringLiteral("Y")},
        {QStringLiteral("Dreamcast"), QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("X"), QStringLiteral("Y")},
    };
}

QString ConsoleFaceName(const QString& console, const QString& place) {
    static const QStringList playstation{QStringLiteral("ps1"), QStringLiteral("ps2"), QStringLiteral("psp"), QStringLiteral("psvita")};
    static const QStringList lettered{QStringLiteral("switch"), QStringLiteral("3ds"), QStringLiteral("nds"), QStringLiteral("dc")};
    const QList<ConsoleFaces> rows = FacesOnConsoles();
    const ConsoleFaces* row = playstation.contains(console) ? &rows[1] : lettered.contains(console) ? &rows[2] : nullptr;
    if (row == nullptr) {
        return {};
    }
    return place == QStringLiteral("south")   ? row->south
           : place == QStringLiteral("east")  ? row->east
           : place == QStringLiteral("west")  ? row->west
           : place == QStringLiteral("north") ? row->north
                                              : QString();
}

void SetCurrent(const Names& names) {
    std::scoped_lock lock{current_mutex};
    current = names;
}

Names Current() {
    std::scoped_lock lock{current_mutex};
    return current;
}

} // namespace Home::PadNames
