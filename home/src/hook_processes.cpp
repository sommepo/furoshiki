// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hook_processes.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace Home::Hooks {

namespace {
#ifdef _WIN32
struct Listing {
    QList<Program> programs;
    QHash<qint64, int> seen;
};

BOOL CALLBACK EachWindow(HWND window, LPARAM into) {
    auto& listing = *reinterpret_cast<Listing*>(into);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    wchar_t title[512] = {};
    if (GetWindowTextW(window, title, 511) <= 0) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0 || pid == GetCurrentProcessId() || listing.seen.contains(pid)) {
        return TRUE;
    }
    Program program;
    program.pid = pid;
    program.window = QString::fromWCharArray(title);
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[1024] = {};
        DWORD size = 1023;
        if (QueryFullProcessImageNameW(process, 0, path, &size)) {
            program.path = QDir::fromNativeSeparators(QString::fromWCharArray(path, static_cast<int>(size)));
            program.name = QFileInfo(program.path).fileName();
        }
        BOOL wow = FALSE;
        program.is32 = IsWow64Process(process, &wow) && wow;
        CloseHandle(process);
    }
    if (program.name.isEmpty()) {
        return TRUE; // a program of the system's that may not be looked at
    }
    listing.seen.insert(pid, 1);
    listing.programs.append(program);
    return TRUE;
}

QList<Program> Real() {
    Listing listing;
    EnumWindows(EachWindow, reinterpret_cast<LPARAM>(&listing));
    return listing.programs;
}
#else
QList<Program> Real() {
    QList<Program> programs;
    const uint me = getuid();
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool number = false;
        const qint64 pid = entry.toLongLong(&number);
        if (!number || pid == QCoreApplication::applicationPid()) {
            continue;
        }
        const QFileInfo info(proc.filePath(entry));
        if (info.ownerId() != me) {
            continue;
        }
        const QString path = QFileInfo(info.filePath() + QStringLiteral("/exe")).symLinkTarget();
        if (path.isEmpty()) {
            continue;
        }
        Program program;
        program.pid = pid;
        program.path = path;
        program.name = QFileInfo(path).fileName().remove(QStringLiteral(" (deleted)"));
        programs.append(program);
    }
    // The newest first: a game just started is what is wanted.
    std::sort(programs.begin(), programs.end(), [](const Program& a, const Program& b) { return a.pid > b.pid; });
    return programs;
}
#endif

bool SameName(const QString& a, const QString& b) {
    return a.compare(b, Qt::CaseInsensitive) == 0;
}
} // namespace

QList<Program> Programs() {
    const QString fake = qEnvironmentVariable("READER_HOME_FAKE_PROGRAMS");
    if (fake.isEmpty()) {
        return Real();
    }
    QList<Program> programs, real;
    bool asked = false;
    for (const QString& entry : fake.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList parts = entry.split(QLatin1Char('|'));
        if (parts.size() < 2) {
            continue;
        }
        Program program;
        program.name = parts[1];
        program.window = parts.value(2);
        program.is32 = parts.value(3) == QStringLiteral("32");
        if (parts[0] == QStringLiteral("?")) {
            if (!asked) {
                real = Real();
                asked = true;
            }
            const auto found = std::find_if(real.begin(), real.end(), [&](const Program& one) { return SameName(one.name, program.name); });
            if (found == real.end()) {
                continue;
            }
            program.pid = found->pid;
            program.path = found->path;
        } else {
            program.pid = parts[0].toLongLong();
        }
        programs.append(program);
    }
    return programs;
}

std::optional<Program> ProgramNamed(const QString& name) {
    if (name.isEmpty()) {
        return std::nullopt;
    }
    for (const Program& program : Programs()) {
        if (SameName(program.name, name)) {
            return program;
        }
    }
    return std::nullopt;
}

bool Runs(qint64 pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool runs = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return runs;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

Program ProgramOfWindow(const QList<Program>& programs, quintptr handle, const QString& title) {
#ifdef _WIN32
    if (handle != 0) {
        DWORD pid = 0;
        GetWindowThreadProcessId(reinterpret_cast<HWND>(handle), &pid);
        for (const Program& program : programs) {
            if (pid != 0 && program.pid == static_cast<qint64>(pid)) {
                return program;
            }
        }
    }
#else
    (void)handle;
#endif
    for (const Program& program : programs) {
        if (!title.isEmpty() && program.window == title) {
            return program;
        }
    }
    return {};
}

} // namespace Home::Hooks
