// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <QDir>
#include <QLibrary>
#include "core_library.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Home {

namespace {

/// Whether a struct the other side filled reaches `field` (the struct's size, its first field).
template <typename Struct>
bool Reaches(const Struct& value, std::size_t end_of_field) {
    return value.struct_size >= end_of_field;
}
#define READER_END_OF(type, field) (offsetof(type, field) + sizeof(type::field))

bool Empty(const char* text) {
    return text == nullptr || *text == '\0';
}

} // namespace

QStringList FindCores(const QString& folder) {
#ifdef _WIN32
    const QString pattern = QStringLiteral("reader_core_*.dll");
#else
    const QString pattern = QStringLiteral("reader_core_*.so");
#endif
    QStringList out;
    const QDir dir(folder);
    for (const QString& name : dir.entryList({pattern}, QDir::Files, QDir::Name)) {
        out.append(dir.filePath(name));
    }
    return out;
}

QStringList CheckCore(const reader_core_api& api, const reader_core_info& info) {
    QStringList problems;
    if (api.api_version != READER_CORE_API_VERSION || info.api_version != READER_CORE_API_VERSION) {
        problems.append(QStringLiteral("built for version %1 of the interface, this program has %2")
                            .arg(info.api_version)
                            .arg(READER_CORE_API_VERSION));
        return problems;
    }
    if (!Reaches(api, READER_END_OF(reader_core_api, press_button))) {
        problems.append(QStringLiteral("its table of functions is shorter than this interface's"));
        return problems;
    }
    // What the program cannot do without.
    const std::pair<const char*, bool> needed[] = {
        {"info", api.info != nullptr},
        {"init", api.init != nullptr},
        {"load_game", api.load_game != nullptr},
        {"stop", api.stop != nullptr},
        {"read_memory", api.read_memory != nullptr},
        {"memory_regions", api.memory_regions != nullptr},
    };
    for (const auto& [name, there] : needed) {
        if (!there) {
            problems.append(QStringLiteral("it has no %1").arg(QLatin1String(name)));
        }
    }
    if (!Reaches(info, READER_END_OF(reader_core_info, setting_count))) {
        problems.append(QStringLiteral("its description is shorter than this interface's"));
        return problems;
    }
    if (Empty(info.key) || Empty(info.system) || Empty(info.app_key)) {
        problems.append(QStringLiteral("it does not say its console's key, name and app"));
    }
    if (Empty(info.game_id_pattern)) {
        problems.append(QStringLiteral("it does not say what a game's ID is like"));
    }
    if (info.screen_width <= 0 || info.screen_height <= 0) {
        problems.append(QStringLiteral("its screen has no size"));
    }
    const reader_core_cpu& cpu = info.cpu;
    if (cpu.name_count > 0 && cpu.names == nullptr) {
        problems.append(QStringLiteral("its processor's registers are not listed"));
    }
    for (uint32_t i = 0; cpu.names != nullptr && i < cpu.name_count; ++i) {
        const reader_core_register_name& name = cpu.names[i];
        if (Empty(name.name) || name.index >= cpu.value_count || name.kind > READER_CORE_REGISTER_FLOAT64) {
            problems.append(QStringLiteral("register %1 is not in its snapshot").arg(i));
        }
    }
    if (cpu.hook_alignment == 0 || (cpu.hook_alignment & (cpu.hook_alignment - 1)) != 0) {
        problems.append(QStringLiteral("its processor's instructions have no alignment (a power of two)"));
    }
    for (uint32_t i = 0; i < info.setting_count; ++i) {
        if (info.settings == nullptr || Empty(info.settings[i].key)) {
            problems.append(QStringLiteral("setting %1 has no key").arg(i));
            break;
        }
    }
    if (info.file_type_count > 0 && info.file_types == nullptr) {
        problems.append(QStringLiteral("its file types are not listed"));
    }
    if (info.button_count > 0 && info.buttons == nullptr) {
        problems.append(QStringLiteral("its buttons are not listed"));
    }
    // What it says it can do beyond the first version: every function that needs is there.
    const uint32_t features = Features(info);
    const std::pair<uint32_t, std::pair<const char*, bool>> promised[] = {
        {READER_CORE_HAS_SAVE_SYNC, {"save_locations", READER_CORE_HAS_FIELD(api, save_locations)}},
        {READER_CORE_HAS_DISCS, {"disc_count, disc_current and disc_insert",
                                 READER_CORE_HAS_FIELD(api, disc_count) && READER_CORE_HAS_FIELD(api, disc_current) &&
                                     READER_CORE_HAS_FIELD(api, disc_insert)}},
        {READER_CORE_HAS_POINTER, {"pointer, and a screen that takes it",
                                   READER_CORE_HAS_FIELD(api, pointer) && info.pointer_screens != 0}},
        {READER_CORE_HAS_STATES, {"state_info, and slots", READER_CORE_HAS_FIELD(api, state_info) && info.state_slots > 0}},
        {READER_CORE_HAS_PATCHES, {"patches", READER_CORE_HAS_FIELD(api, patches)}},
    };
    for (const auto& [feature, check] : promised) {
        if ((features & feature) != 0 && !check.second) {
            problems.append(QStringLiteral("it says it can do more than it has (no %1)").arg(QLatin1String(check.first)));
        }
    }
    if (Reaches(info, READER_END_OF(reader_core_info, device_count)) && info.device_count > 0 &&
        info.devices == nullptr) {
        problems.append(QStringLiteral("its kinds of controller are not listed"));
    }
    return problems;
}

void CoreLog(uint32_t level, const std::string& text) {
    if (level >= READER_CORE_LOG_ERROR) {
        qWarning("%s", text.c_str());
    } else {
        qInfo("%s", text.c_str());
    }
}

uint32_t SettingFlags(const reader_core_info& info, uint32_t index) {
    return Reaches(info, READER_END_OF(reader_core_info, setting_flags)) && info.setting_flags != nullptr &&
                   index < info.setting_count
               ? info.setting_flags[index]
               : 0;
}

uint32_t PortCount(const reader_core_info& info) {
    return Reaches(info, READER_END_OF(reader_core_info, port_count)) && info.port_count > 0 ? info.port_count : 1;
}

uint32_t PointerScreens(const reader_core_info& info) {
    return Reaches(info, READER_END_OF(reader_core_info, pointer_screens)) ? info.pointer_screens : 0;
}

int32_t StateSlots(const reader_core_info& info) {
    return Reaches(info, READER_END_OF(reader_core_info, state_slots)) && info.state_slots > 0 ? info.state_slots : 10;
}

QList<reader_core_device> Devices(const reader_core_info& info) {
    QList<reader_core_device> out;
    if (!Reaches(info, READER_END_OF(reader_core_info, device_count)) || info.devices == nullptr) {
        return out;
    }
    for (uint32_t i = 0; i < info.device_count; ++i) {
        if (info.devices[i].name != nullptr) {
            out.append(info.devices[i]);
        }
    }
    return out;
}

bool LoadCore(const QString& path, Core& core, QString& error) {
    // Never unloaded (QLibrary leaves it loaded when it goes): an emulator keeps threads and
    // state of its own until the program ends.
#ifdef _WIN32
    // A core's own libraries are beside it (PCSX2's: SDL 3, zstd, ...), where Windows looks for
    // them only when told to: loaded so first, the core is the one QLibrary then finds (its
    // plain LoadLibrary looks beside the program and along PATH, and stops there at a folder
    // it does not trust, as on the maintainer's PC: "untrusted mount point").
    LoadLibraryExW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), nullptr,
                   LOAD_WITH_ALTERED_SEARCH_PATH);
#endif
    QLibrary library(path);
    if (!library.load()) {
        error = library.errorString();
        return false;
    }
    const auto get = reinterpret_cast<reader_core_get_fn>(library.resolve(READER_CORE_ENTRY));
    if (get == nullptr) {
        error = QStringLiteral("%1 is not a reader core (it has no %2)").arg(path, QLatin1String(READER_CORE_ENTRY));
        return false;
    }
    const reader_core_api* api = get(READER_CORE_API_VERSION);
    if (api == nullptr) {
        error = QStringLiteral("%1 does not serve version %2 of the interface").arg(path).arg(READER_CORE_API_VERSION);
        return false;
    }
    // The table's size and version are read before anything else of it.
    if (api->struct_size < READER_END_OF(reader_core_api, info) || api->info == nullptr) {
        error = QStringLiteral("%1: its table of functions is too short").arg(path);
        return false;
    }
    const reader_core_info* info = api->info();
    if (info == nullptr) {
        error = QStringLiteral("%1 says nothing of itself").arg(path);
        return false;
    }
    const QStringList problems = CheckCore(*api, *info);
    if (!problems.isEmpty()) {
        error = QStringLiteral("%1: %2").arg(path, problems.join(QStringLiteral("; ")));
        return false;
    }
    core = Core{path.toStdString(), api, info};
    return true;
}

} // namespace Home
