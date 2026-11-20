// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QRegularExpression>
#include <QSet>
#include "pad_bluetooth.h"

#if defined(_WIN32)
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#endif

namespace Home::PadBluetooth {

QString AddressIn(const QString& id) {
    // Each form Windows writes an address in, the 12 digits never followed by another hex digit.
    static const QRegularExpression forms[] = {
        QRegularExpression(QStringLiteral("DEV_([0-9A-F]{12})(?![0-9A-F])"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("&([0-9A-F]{12})_C0"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("_REV&[0-9A-F]{4}_([0-9A-F]{12})(?![0-9A-F])"), QRegularExpression::CaseInsensitiveOption),
    };
    for (const QRegularExpression& form : forms) {
        if (const QRegularExpressionMatch match = form.match(id); match.hasMatch()) {
            return match.captured(1).toUpper();
        }
    }
    return {};
}

bool IsBluetooth(const QString& id) {
    const QString upper = id.toUpper();
    return upper.contains(QStringLiteral("BTHENUM")) || upper.contains(QStringLiteral("BTHLE")) ||
           upper.contains(QStringLiteral("{00001124-0000-1000-8000-00805F9B34FB}")) ||
           upper.contains(QStringLiteral("{00001812-0000-1000-8000-00805F9B34FB}"));
}

bool VendorProductIn(const QString& id, uint16_t& vendor, uint16_t& product) {
    static const QRegularExpression vid(QStringLiteral("VID[&_]([0-9A-F]{4,8})"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pid(QStringLiteral("PID[&_]([0-9A-F]{4,8})"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch v = vid.match(id);
    const QRegularExpressionMatch p = pid.match(id);
    if (!v.hasMatch() || !p.hasMatch()) {
        return false;
    }
    vendor = static_cast<uint16_t>(v.captured(1).right(4).toUInt(nullptr, 16));
    product = static_cast<uint16_t>(p.captured(1).right(4).toUInt(nullptr, 16));
    return true;
}

QString InstanceIdOfPath(const QString& path) {
    QString text = path;
    if (!text.startsWith(QStringLiteral("\\\\?\\")) && !text.startsWith(QStringLiteral("\\\\.\\"))) {
        return text;
    }
    text = text.mid(4);
    // The last part is the interface's class ("{4d1e55b2-...}"): not the device's.
    QStringList parts = text.split(QLatin1Char('#'));
    if (parts.size() > 1 && parts.last().startsWith(QLatin1Char('{'))) {
        parts.removeLast();
    }
    return parts.join(QLatin1Char('\\'));
}

#if defined(_WIN32)

namespace {

QString IdOf(DEVINST node) {
    wchar_t id[MAX_DEVICE_ID_LEN + 1] = {};
    if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
        return {};
    }
    return QString::fromWCharArray(id);
}

/// The address in a device's id or in one of its parents' (a classic pad's HID device has it in
/// its BTHENUM parent's id; a Bluetooth LE pad's in its own).
QString AddressUp(DEVINST node) {
    for (int up = 0; up < 5; ++up) {
        const QString id = IdOf(node);
        if (const QString address = AddressIn(id); !address.isEmpty() && IsBluetooth(id)) {
            return address;
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) {
            break;
        }
        node = parent;
    }
    return {};
}

QString AddressOfInstance(const QString& instance) {
    DEVINST node = 0;
    std::wstring wide = instance.toStdWString();
    if (CM_Locate_DevNodeW(&node, wide.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return {};
    }
    return AddressUp(node);
}

QString Property(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD which) {
    wchar_t text[512] = {};
    DWORD type = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, which, &type, reinterpret_cast<BYTE*>(text), sizeof(text) - sizeof(wchar_t), nullptr)) {
        return {};
    }
    return QString::fromWCharArray(text);
}

/// Calls `each` with the instance id and the set's entry of every present device of an enumerator.
template <typename Each>
void Devices(const wchar_t* enumerator, Each each) {
    const HDEVINFO set = SetupDiGetClassDevsW(nullptr, enumerator, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return;
    }
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); ++i) {
        wchar_t id[MAX_DEVICE_ID_LEN + 1] = {};
        if (SetupDiGetDeviceInstanceIdW(set, &data, id, MAX_DEVICE_ID_LEN, nullptr)) {
            each(QString::fromWCharArray(id), set, data);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

/// Windows' name of the Bluetooth device with this address ("8BitDo Pro 2").
QString NameOf(const QString& address) {
    QString name;
    for (const wchar_t* enumerator : {L"BTHENUM", L"BTHLE"}) {
        Devices(enumerator, [&](const QString& id, HDEVINFO set, SP_DEVINFO_DATA& data) {
            if (!name.isEmpty() || !id.startsWith(QString::fromWCharArray(enumerator) + QStringLiteral("\\DEV_"), Qt::CaseInsensitive) ||
                AddressIn(id) != address) {
                return;
            }
            name = Property(set, data, SPDRP_FRIENDLYNAME);
            if (name.isEmpty()) {
                name = Property(set, data, SPDRP_DEVICEDESC);
            }
        });
    }
    return name.trimmed();
}

} // namespace

Found Find(const QString& sdl_path, uint16_t vendor, uint16_t product) {
    Found found;
    // SDL's path names the device itself (HIDAPI, raw input): the address in it or above it.
    if (!sdl_path.isEmpty()) {
        found.address = IsBluetooth(sdl_path) ? AddressIn(sdl_path) : QString();
        if (found.address.isEmpty() && (sdl_path.startsWith(QStringLiteral("\\\\?\\")) || sdl_path.contains(QLatin1Char('\\')))) {
            found.address = AddressOfInstance(InstanceIdOfPath(sdl_path));
        }
    }
    // "XInput#0" names no device: the one connected Bluetooth HID device with its vendor and product.
    if (found.address.isEmpty() && vendor != 0) {
        QSet<QString> addresses;
        Devices(L"HID", [&](const QString& id, HDEVINFO, SP_DEVINFO_DATA& data) {
            uint16_t v = 0, p = 0;
            if (!IsBluetooth(id) || !VendorProductIn(id, v, p) || v != vendor || p != product) {
                return;
            }
            if (const QString address = AddressUp(data.DevInst); !address.isEmpty()) {
                addresses.insert(address);
            }
        });
        if (addresses.size() == 1) {
            found.address = *addresses.begin();
        }
    }
    if (!found.address.isEmpty()) {
        found.name = NameOf(found.address);
    }
    return found;
}

#else

Found Find(const QString&, uint16_t, uint16_t) {
    return {};
}

#endif

} // namespace Home::PadBluetooth
