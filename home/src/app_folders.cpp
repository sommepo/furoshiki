// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "app_folders.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

namespace Home::AppFolders {

namespace {

bool IsFolder(const QString& path) {
    return !path.isEmpty() && QFileInfo(path).isDir();
}

#if !defined(_WIN32)
/// An XDG base folder: the variable's, else its default in the home folder.
QString XdgFolder(const char* variable, const char* fallback) {
    const QString folder = qEnvironmentVariable(variable);
    return folder.isEmpty() ? QDir::home().filePath(QLatin1String(fallback)) : folder;
}
#endif

/// An app's own folder `name` where the app keeps its data: in %APPDATA% (Windows), in the
/// XDG base folder `variable` (default `fallback`) on Linux, under the name `linux_name`.
QString RoamingFolder(const char* name, const char* variable, const char* fallback, const char* linux_name) {
#if defined(_WIN32)
    Q_UNUSED(variable);
    Q_UNUSED(fallback);
    Q_UNUSED(linux_name);
    const QString app_data = qEnvironmentVariable("APPDATA");
    return app_data.isEmpty() ? QString() : QDir(app_data).filePath(QLatin1String(name));
#else
    Q_UNUSED(name);
    return QDir(XdgFolder(variable, fallback)).filePath(QLatin1String(linux_name));
#endif
}

/// PCSX2 Reader's data folder: its own folder with a portable.ini or portable.txt beside the
/// program, else PCSX2Reader in the user's documents (Windows) or ~/.config (Linux).
QString Pcsx2Folder(const QString& beside) {
    if (!beside.isEmpty() && (QFileInfo::exists(QDir(beside).filePath(QStringLiteral("portable.ini"))) ||
                              QFileInfo::exists(QDir(beside).filePath(QStringLiteral("portable.txt"))))) {
        return beside;
    }
#if defined(_WIN32)
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QString() : QDir(documents).filePath(QStringLiteral("PCSX2Reader"));
#else
    return QDir(XdgFolder("XDG_CONFIG_HOME", ".config")).filePath(QStringLiteral("PCSX2Reader"));
#endif
}

/// A value of PCSX2's ini (`Name = value` under `[Section]`), as written: its Windows paths'
/// backslashes are no escapes (which QSettings would make of them).
QString IniValue(const QString& path, const QString& section, const QString& name, const QString& fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fallback;
    }
    QTextStream in(&file);
    QString current;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            current = line.mid(1, line.size() - 2).trimmed();
        } else if (current == section) {
            const qsizetype equals = line.indexOf(QLatin1Char('='));
            if (equals > 0 && line.left(equals).trimmed() == name) {
                return line.mid(equals + 1).trimmed();
            }
        }
    }
    return fallback;
}

#if !defined(_WIN32)
/// Whether the PPSSPP memory stick `stick` is PPSSPP Reader's: its PSP/SYSTEM/ppsspp.ini has the
/// app's own theme, which a plain PPSSPP does not write.
bool IsReaderPpsspp(const QString& stick) {
    QFile file(QDir(stick).filePath(QStringLiteral("PSP/SYSTEM/ppsspp.ini")));
    return file.open(QIODevice::ReadOnly | QIODevice::Text) &&
           QString::fromUtf8(file.readAll()).contains(QStringLiteral("ThemeName = PSP"));
}
#endif

} // namespace

Folders Of(const QString& key, const QString& program) {
    const QString beside = program.isEmpty() ? QString() : QFileInfo(program).absolutePath();
    const bool portable = !beside.isEmpty() && QFileInfo::exists(QDir(beside).filePath(QStringLiteral("portable.txt")));
    Folders found;
    if (key == QStringLiteral("ps1")) {
        // Station Reader keeps a game's own card in saves/<serial>/ of its folder, as the core
        // does in the saves folder it is given; its BIOS in bios.
        if (portable && IsFolder(QDir(beside).filePath(QStringLiteral("saves")))) {
            found.saves = QDir(beside).filePath(QStringLiteral("saves"));
            found.sync = QDir(beside).filePath(QStringLiteral("reader-savesync"));
        }
        if (portable && IsFolder(QDir(beside).filePath(QStringLiteral("bios")))) {
            found.system = QDir(beside).filePath(QStringLiteral("bios"));
        }
    } else if (key == QStringLiteral("psp")) {
        // PPSSPP Reader's memory stick: the core's saves folder is one (PSP/SAVEDATA in it). On
        // Linux it is a plain PPSSPP's ~/.config/ppsspp, taken only when it is the reader app's
        // (its ppsspp.ini with the app's PSP theme, as src/old_settings.cpp decides).
        const QString stick = portable ? QDir(beside).filePath(QStringLiteral("memstick"))
                                       : RoamingFolder("ppsspp-reader", "XDG_CONFIG_HOME", ".config", "ppsspp");
#if !defined(_WIN32)
        if (!portable && !IsReaderPpsspp(stick)) {
            return found;
        }
#endif
        if (IsFolder(QDir(stick).filePath(QStringLiteral("PSP/SAVEDATA")))) {
            found.saves = stick;
            found.sync = QDir(stick).filePath(QStringLiteral("PSP/SYSTEM/reader-savesync"));
        }
    } else if (key == QStringLiteral("ps2")) {
        // PCSX2 Reader keeps a game's own card in reader-saves/<serial>/card.ps2 of its memory
        // cards folder, as the core does in the saves folder it is given; its BIOS where its
        // settings say (inis/PCSX2.ini, [Folders], relative to its data folder).
        const QString root = Pcsx2Folder(beside);
        const QString ini = QDir(root).filePath(QStringLiteral("inis/PCSX2.ini"));
        if (!root.isEmpty() && QFileInfo::exists(ini)) {
            const QString folders = QStringLiteral("Folders");
            const QString cards = QDir(root).filePath(IniValue(ini, folders, QStringLiteral("MemoryCards"), QStringLiteral("memcards")));
            const QString bios = QDir(root).filePath(IniValue(ini, folders, QStringLiteral("Bios"), QStringLiteral("bios")));
            if (IsFolder(QDir(cards).filePath(QStringLiteral("reader-saves")))) {
                found.saves = QDir(cards).filePath(QStringLiteral("reader-saves"));
                found.sync = QDir(root).filePath(QStringLiteral("reader-savesync"));
            }
            if (IsFolder(bios)) {
                found.system = bios;
            }
        }
    } else if (key == QStringLiteral("switch")) {
        // Eden Reader's folder: its keys (keys/), its NAND (nand/), the saves in the NAND.
        const QString user = IsFolder(QDir(beside).filePath(QStringLiteral("user"))) ? QDir(beside).filePath(QStringLiteral("user"))
                                                                                     : RoamingFolder("eden-reader", "XDG_DATA_HOME", ".local/share", "eden-reader");
        if (IsFolder(QDir(user).filePath(QStringLiteral("nand")))) {
            found.system = user;
            found.saves = QDir(user).filePath(QStringLiteral("nand"));
        }
    }
    return found;
}

bool HasSystemFiles(const QString& key, const QString& folder) {
    const QDir dir(folder);
    if (key == QStringLiteral("switch")) {
        return QFileInfo::exists(dir.filePath(QStringLiteral("prod.keys"))) ||
               QFileInfo::exists(dir.filePath(QStringLiteral("keys/prod.keys")));
    }
    return !dir.isEmpty(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
}

} // namespace Home::AppFolders
