// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <QString>

/**
 * A controller's Bluetooth device, on Windows: its address and the name Windows has for it. Pads
 * that claim another maker's id (an 8BitDo in X-input mode says it is an Xbox One S, 045E:02E0)
 * all look alike to SDL, with one GUID and one name; their Bluetooth address tells them apart and
 * their Bluetooth name says what they are ("8BitDo Pro 2"). The parsing is plain text and tested
 * everywhere; the lookup is Windows' (SetupAPI and the configuration manager), and finds nothing
 * elsewhere.
 */
namespace Home::PadBluetooth {

/// The 12 hex digits of a Bluetooth address in a device instance id or a device path, in
/// capitals, or empty: "BTHENUM\DEV_E417D85A6799\...", "...&0&E417D85A6799_C00000000",
/// "BTHLEDEVICE\{...}_DEV_VID&02045E_PID&0B13_REV&0509_0C3526235A50", and the HID paths built on
/// them ("\\?\HID#{00001124-...}...#9&243dbe1c&0&e417d85a6799_c00000000#{...}").
[[nodiscard]] QString AddressIn(const QString& id);
/// Whether an instance id or a path is of a device that came over Bluetooth (classic HID or LE HID).
[[nodiscard]] bool IsBluetooth(const QString& id);
/// The vendor and product in an id or a path ("VID&0002045E_PID&02E0", "VID_045E&PID_02E0"):
/// the last four hex digits of each; false where there are none.
bool VendorProductIn(const QString& id, uint16_t& vendor, uint16_t& product);
/// A device path ("\\?\HID#X#Y#{interface}") as its instance id ("HID\X\Y"), or the text itself.
[[nodiscard]] QString InstanceIdOfPath(const QString& path);

struct Found {
    QString address; ///< 12 hex digits in capitals; empty: not found or not Bluetooth
    QString name;    ///< Windows' name of the Bluetooth device ("8BitDo Pro 2"), or empty
};
/// The Bluetooth device of a controller SDL opened: from SDL's path of it where that names one,
/// else the one connected Bluetooth HID device with its vendor and product (none when there are
/// several). Windows only; slow-ish (a walk over the devices), so once per controller added.
[[nodiscard]] Found Find(const QString& sdl_path, uint16_t vendor, uint16_t product);

} // namespace Home::PadBluetooth
