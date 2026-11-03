// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "old_settings.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

#include "play_settings.h"

namespace Home::OldSettings {

namespace {

/// An INI file as the three emulators write it: "[Section]", then "Key = Value".
class Ini {
public:
    explicit Ini(const QString& text) {
        QString section;
        for (const QString& raw : text.split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2).trimmed();
                sections.insert(section, {});
                continue;
            }
            const qsizetype equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0 || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
                continue;
            }
            sections[section].insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }

    [[nodiscard]] bool HasSection(const QString& section) const {
        return sections.contains(section);
    }
    [[nodiscard]] bool Has(const QString& section, const QString& key) const {
        return sections.value(section).contains(key);
    }
    [[nodiscard]] QString Text(const QString& section, const QString& key) const {
        return sections.value(section).value(key);
    }
    [[nodiscard]] int Number(const QString& section, const QString& key, int otherwise) const {
        bool ok = false;
        const int value = Text(section, key).toInt(&ok);
        return ok ? value : otherwise;
    }
    [[nodiscard]] bool Switch(const QString& section, const QString& key, bool otherwise) const {
        if (!Has(section, key)) {
            return otherwise;
        }
        const QString value = Text(section, key).toLower();
        return value == QStringLiteral("true") || value == QStringLiteral("yes") || value == QStringLiteral("1");
    }

private:
    QHash<QString, QHash<QString, QString>> sections;
};

/// The program's choice for the emulator's `value`, from pairs of the emulator's name and the
/// program's; empty for a value not among them (its default, or one the program has not).
QString Choice(const QString& value, std::initializer_list<std::pair<const char*, const char*>> names) {
    for (const auto& [name, choice] : names) {
        if (value.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
            return QLatin1String(choice);
        }
    }
    return {};
}

void Add(QList<Value>& out, const char* console, const char* key, const QString& value) {
    if (!value.isEmpty()) {
        out.append({QLatin1String(console), QLatin1String(key), value});
    }
}

void AddSwitch(QList<Value>& out, const char* console, const char* key, bool on, bool emulator_default) {
    if (on != emulator_default) {
        Add(out, console, key, on ? QStringLiteral("1") : QStringLiteral("0"));
    }
}

QString TextOf(const QString& file) {
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
}

} // namespace

QList<Value> FromStation(const QString& text) {
    const Ini ini(text);
    QList<Value> out;
    // DuckStation of 2021's defaults on the desktop, with Station Reader's OpenGL.
    if (const int scale = ini.Number(QStringLiteral("GPU"), QStringLiteral("ResolutionScale"), 1); scale > 1) {
        Add(out, "ps1", "picture.resolution", QString::number(std::min(scale, 8)));
    }
    Add(out, "ps1", "picture.renderer",
        Choice(ini.Text(QStringLiteral("GPU"), QStringLiteral("Renderer")), {{"Software", "software"}}));
    if (ini.Switch(QStringLiteral("Display"), QStringLiteral("Stretch"), false)) {
        Add(out, "ps1", "picture.aspect", QStringLiteral("stretch"));
    } else {
        Add(out, "ps1", "picture.aspect",
            Choice(ini.Text(QStringLiteral("Display"), QStringLiteral("AspectRatio")), {{"4:3", "4:3"}, {"16:9", "16:9"}}));
    }
    AddSwitch(out, "ps1", "picture.vsync", ini.Switch(QStringLiteral("Display"), QStringLiteral("VSync"), false), false);
    int volume = std::clamp(ini.Number(QStringLiteral("Audio"), QStringLiteral("OutputVolume"), 100), 0, 100);
    if (ini.Switch(QStringLiteral("Audio"), QStringLiteral("OutputMuted"), false)) {
        volume = 0;
    }
    if (volume != 100) {
        Add(out, "ps1", "sound.volume", QString::number(volume));
    }
    AddSwitch(out, "ps1", "ps1.true_color", ini.Switch(QStringLiteral("GPU"), QStringLiteral("TrueColor"), false), false);
    Add(out, "ps1", "ps1.texture_filter",
        Choice(ini.Text(QStringLiteral("GPU"), QStringLiteral("TextureFilter")),
               {{"Bilinear", "bilinear"}, {"JINC2", "jinc2"}, {"xBR", "xbr"}}));
    AddSwitch(out, "ps1", "ps1.pgxp", ini.Switch(QStringLiteral("GPU"), QStringLiteral("PGXPEnable"), false), false);
    AddSwitch(out, "ps1", "ps1.widescreen", ini.Switch(QStringLiteral("GPU"), QStringLiteral("WidescreenHack"), false), false);
    AddSwitch(out, "ps1", "ps1.fast_boot", ini.Switch(QStringLiteral("BIOS"), QStringLiteral("PatchFastBoot"), false), false);
    Add(out, "ps1", "ps1.region",
        Choice(ini.Text(QStringLiteral("Console"), QStringLiteral("Region")),
               {{"NTSC-J", "ntsc-j"}, {"NTSC-U", "ntsc-u"}, {"PAL", "pal"}}));
    Add(out, "ps1", "ps1.cd_speed",
        Choice(ini.Text(QStringLiteral("CDROM"), QStringLiteral("ReadSpeedup")), {{"2", "2"}, {"4", "4"}, {"8", "8"}}));
    return out;
}

QList<Value> FromFlycast(const QString& text) {
    const Ini ini(text);
    const QString config = QStringLiteral("config");
    QList<Value> out;
    // Flycast's defaults: 480 lines, the USA and English, no widescreen, its own filtering.
    if (const int lines = ini.Number(config, QStringLiteral("rend.Resolution"), 480); lines >= 960) {
        Add(out, "dc", "picture.resolution", QString::number(std::min(lines / 480, 6)));
    }
    if (const int volume = std::clamp(ini.Number(config, QStringLiteral("aica.Volume"), 100), 0, 100); volume != 100) {
        Add(out, "dc", "sound.volume", QString::number(volume));
    }
    Add(out, "dc", "dc.region",
        Choice(ini.Text(config, QStringLiteral("Dreamcast.Region")), {{"0", "japan"}, {"2", "europe"}, {"3", "default"}}));
    Add(out, "dc", "dc.language",
        Choice(ini.Text(config, QStringLiteral("Dreamcast.Language")),
               {{"0", "japanese"}, {"2", "german"}, {"3", "french"}, {"4", "spanish"}, {"5", "italian"}}));
    AddSwitch(out, "dc", "dc.widescreen", ini.Switch(config, QStringLiteral("rend.WideScreen"), false), false);
    Add(out, "dc", "dc.texture_filter",
        Choice(ini.Text(config, QStringLiteral("rend.TextureFiltering")), {{"1", "nearest"}, {"2", "linear"}}));
    AddSwitch(out, "dc", "dc.hle_bios", ini.Switch(config, QStringLiteral("UseReios"), false), false);
    return out;
}

QList<Value> FromPpsspp(const QString& text) {
    const Ini ini(text);
    QList<Value> out;
    // PPSSPP's defaults on the desktop: its resolution "auto", its texture filtering "auto",
    // the console's language as the system's, cross for yes, the JIT, full volume.
    if (const int times = ini.Number(QStringLiteral("Graphics"), QStringLiteral("InternalResolution"), 0); times > 0) {
        Add(out, "psp", "picture.resolution", QString::number(std::min(times, 4)));
    }
    int volume = 100;
    if (ini.Has(QStringLiteral("Sound"), QStringLiteral("GameVolume"))) {
        volume = ini.Number(QStringLiteral("Sound"), QStringLiteral("GameVolume"), 100);
    } else if (ini.Has(QStringLiteral("Sound"), QStringLiteral("GlobalVolume"))) {
        volume = ini.Number(QStringLiteral("Sound"), QStringLiteral("GlobalVolume"), 10) * 10;
    }
    if (volume = std::clamp(volume, 0, 100); volume != 100) {
        Add(out, "psp", "sound.volume", QString::number(volume));
    }
    Add(out, "psp", "psp.language",
        Choice(ini.Text(QStringLiteral("SystemParam"), QStringLiteral("GameLanguage")), {{"0", "japanese"}, {"1", "english"}}));
    Add(out, "psp", "psp.confirm",
        Choice(ini.Text(QStringLiteral("SystemParam"), QStringLiteral("ButtonPreference")), {{"0", "circle"}}));
    Add(out, "psp", "psp.texture_filter",
        Choice(ini.Text(QStringLiteral("Graphics"), QStringLiteral("TextureFiltering")), {{"2", "nearest"}, {"3", "linear"}}));
    Add(out, "psp", "psp.cpu",
        Choice(ini.Text(QStringLiteral("CPU"), QStringLiteral("CPUCore")), {{"0", "interpreter"}, {"2", "ir"}}));
    return out;
}

QList<Source> Sources(const QString& program_folder) {
    QList<Source> out;
    const auto add = [&out](const char* console, const char* app, const QString& file,
                            const QString& reader_mark = {}) {
        if (!QFileInfo(file).isFile()) {
            return;
        }
        // A file in a place a plain DuckStation or PPSSPP uses too counts only when it is the
        // reader app's.
        if (!reader_mark.isEmpty() && !TextOf(file).contains(reader_mark)) {
            return;
        }
        for (const Source& known : out) {
            if (known.console == QLatin1String(console)) {
                return; // the first place an app's file is found is the one it uses
            }
        }
        out.append({QLatin1String(console), QLatin1String(app), QDir::cleanPath(file)});
    };
#if defined(_WIN32)
    // The apps as windows-pc/push.sh installs them: folders beside the program's, each with
    // its portable.txt.
    const QString readers = QFileInfo(program_folder).dir().absolutePath();
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString app_data = qEnvironmentVariable("APPDATA");
    add("ps1", "station-reader", readers + QStringLiteral("/StationReader/settings.ini"));
    add("ps1", "station-reader", documents + QStringLiteral("/DuckStation/settings.ini"), QStringLiteral("[Reader]"));
    add("dc", "flycast-reader", readers + QStringLiteral("/FlycastReader/emu.cfg"));
    add("psp", "ppsspp-reader", readers + QStringLiteral("/PPSSPPReader/memstick/PSP/SYSTEM/ppsspp.ini"));
    add("psp", "ppsspp-reader", app_data + QStringLiteral("/ppsspp-reader/PSP/SYSTEM/ppsspp.ini"));
#else
    Q_UNUSED(program_folder);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    add("ps1", "station-reader", data + QStringLiteral("/duckstation/settings.ini"), QStringLiteral("[Reader]"));
    add("dc", "flycast-reader", config + QStringLiteral("/flycast-reader/emu.cfg"));
    add("psp", "ppsspp-reader", config + QStringLiteral("/ppsspp/PSP/SYSTEM/ppsspp.ini"), QStringLiteral("ThemeName = PSP"));
#endif
    return out;
}

QList<Value> Read(const Source& source) {
    const QString text = TextOf(source.file);
    if (source.console == QStringLiteral("ps1")) {
        return FromStation(text);
    }
    if (source.console == QStringLiteral("dc")) {
        return FromFlycast(text);
    }
    if (source.console == QStringLiteral("psp")) {
        return FromPpsspp(text);
    }
    return {};
}

QList<Value> ReadOnce(PlaySettings& settings, const QString& program_folder) {
    if (settings.OldAppsRead()) {
        return {};
    }
    QList<Value> taken;
    for (const Source& source : Sources(program_folder)) {
        for (const Value& value : Read(source)) {
            if (!settings.IsSet(value.key, value.console)) {
                settings.Set(value.key, value.value, value.console);
                taken.append(value);
            }
        }
    }
    settings.SetOldAppsRead();
    settings.Save();
    return taken;
}

} // namespace Home::OldSettings
