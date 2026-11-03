// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Furoshiki: one front for the reader apps, and (built with HOME_CORES) the one program that
// plays their games itself, each emulator a core it loads (docs/one-program.md).
//   reader-home [--script FILE] [--dry-run] [--window] [--play FILE] [--pair LINK | LINK]
// --script runs a test's commands (home_window.h); --dry-run starts no app and says what it
// would have started; --window keeps it out of full screen whatever the settings say; --play
// plays a game here at once, with the core whose console's files it is (HOME_CORES); --pair (or
// the link alone, as a URL handler for readersync: hands it) sets save sync up from a pairing
// link (docs/pairing.md) and shows the settings window's Save sync tab.

#include <cstdio>
#include <mutex>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSettings>
#include "home_window.h"
#include "library.h"
#ifndef READER_NO_SAVE_SYNC
#include "pairing.h"
#endif
#ifdef HOME_CORES
#include "main_window.h"
#if !defined(_WIN32)
#include <unistd.h>
#endif
#endif
#if defined(_WIN32)
#include <csignal>
#include <cstring>
#include <io.h>
#include <windows.h>
#endif

namespace {

/// The program's log, also kept in a file (program/reader-home.log in Furoshiki's folder, the
/// run before as reader-home.old.log): on Windows a program with a window has no output to
/// read, and what a core says of a game (that it found its files) is only there.
QFile* g_log = nullptr;
std::mutex g_log_mutex;
QtMessageHandler g_qt_handler = nullptr;

void LogMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    {
        std::scoped_lock lock{g_log_mutex};
        if (g_log != nullptr) {
            const char* kind = type == QtWarningMsg ? "warning" : type == QtCriticalMsg || type == QtFatalMsg ? "error" : "info";
            g_log->write(QStringLiteral("%1 %2 %3\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                  QLatin1String(kind), message)
                             .toUtf8());
            g_log->flush();
        }
    }
    if (g_qt_handler != nullptr) {
        g_qt_handler(type, context, message);
    }
}

#if defined(_WIN32)
/// What ended the program, written straight into the log (the program may be in no state for
/// more): the reason, then each frame of the stack as module+offset, to be read against the
/// builds' symbols (a core's own errors abort without Windows' report).
void WriteEnd(const char* reason, void* const* frames, int count) {
    const int file = g_log != nullptr ? g_log->handle() : -1;
    if (file < 0) {
        return;
    }
    char line[512];
    int length = std::snprintf(line, sizeof(line), "error The program ended: %s\n", reason);
    _write(file, line, static_cast<unsigned>(length));
    for (int i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module) && module != nullptr) {
            GetModuleFileNameA(module, name, MAX_PATH);
        }
        const char* base = std::strrchr(name, '\\');
        length = std::snprintf(line, sizeof(line), "error   %s+%llx\n", base != nullptr ? base + 1 : name,
                               static_cast<unsigned long long>(static_cast<const char*>(frames[i]) -
                                                               reinterpret_cast<const char*>(module)));
        _write(file, line, static_cast<unsigned>(length));
    }
    _commit(file);
}

void OnAbort(int) {
    void* frames[48];
    const int count = CaptureStackBackTrace(1, 48, frames, nullptr);
    WriteEnd("aborted (an error no part of it handled)", frames, count);
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    char reason[96];
    std::snprintf(reason, sizeof(reason), "crashed (exception %08lx)", info->ExceptionRecord->ExceptionCode);
    void* at = info->ExceptionRecord->ExceptionAddress;
    WriteEnd(reason, &at, 1);
    return EXCEPTION_CONTINUE_SEARCH; // Windows' own report as well
}
#endif

void OpenLog() {
    const QString folder = Home::SharedFolder() + QStringLiteral("/program");
    QDir().mkpath(folder);
    const QString file = folder + QStringLiteral("/reader-home.log");
    QFile::remove(folder + QStringLiteral("/reader-home.old.log"));
    QFile::rename(file, folder + QStringLiteral("/reader-home.old.log"));
    auto* log = new QFile(file); // kept open to the end, whatever runs then
    if (log->open(QIODevice::WriteOnly | QIODevice::Text)) {
        g_log = log;
        g_qt_handler = qInstallMessageHandler(&LogMessage);
#if defined(_WIN32)
        std::signal(SIGABRT, &OnAbort);
        SetUnhandledExceptionFilter(&OnCrash);
#endif
    } else {
        delete log;
    }
}

/// The program, from its window's first showing to Qt's last object: the games stopped, what
/// was read written, the reader taken down (it belongs to the application object).
int Run(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("reader-home"));
    QCoreApplication::setApplicationName(QStringLiteral("reader-home"));
    OpenLog();
    // A machine whose own fonts have no Japanese (a test without a display) names a font file.
    const QString font_file = qEnvironmentVariable("READER_HOME_FONT");
    if (!font_file.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(font_file);
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty()) {
            QApplication::setFont(QFont(families.first()));
        }
    }
    const QStringList arguments = app.arguments();
#ifdef HOME_CORES
    // One window: Furoshiki's page, and the game a core plays in it.
    Home::MainWindow main_window;
    Home::HomeWindow& window = main_window.Home();
#else
    Home::HomeWindow window;
#endif
    window.SetDryRun(arguments.contains(QStringLiteral("--dry-run")));
    window.SetWindowed(arguments.contains(QStringLiteral("--window")));
    window.ShowAsSet();
#ifdef HOME_CORES
    if (const int play = arguments.indexOf(QStringLiteral("--play")); play >= 0 && play + 1 < arguments.size()) {
        QString error;
        if (!main_window.Play(arguments[play + 1], QString(), error)) {
            qWarning("--play: %s", qPrintable(error));
        }
    }
#ifndef READER_NO_SAVE_SYNC
    {
        QString link;
        if (const int pair = arguments.indexOf(QStringLiteral("--pair")); pair >= 0 && pair + 1 < arguments.size()) {
            link = arguments[pair + 1];
        }
        for (const QString& argument : arguments.mid(1)) {
            if (link.isEmpty() && Home::LooksLikePairing(argument)) {
                link = argument;
            }
        }
        if (!link.isEmpty()) {
            QString error;
            if (!main_window.UsePairing(link, error)) {
                qWarning("--pair: %s", qPrintable(error));
            }
        }
    }
#endif
#endif
    const int at = arguments.indexOf(QStringLiteral("--script"));
    if (at >= 0 && at + 1 < arguments.size()) {
        QFile file(arguments[at + 1]);
        if (file.open(QIODevice::ReadOnly)) {
            window.RunScript(QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts));
        }
    }
    return app.exec();
}

} // namespace

int main(int argc, char** argv) {
    const int code = Run(argc, argv);
#ifdef HOME_CORES
    // Everything of the program's own has ended. The cores stay loaded until the process ends
    // and keep threads and static objects of their own that do not take being torn down then
    // (on Windows a DLL's exit code runs after the process's other threads are gone: a core
    // waited for one of them for ever, or threw while joining it, and the closed program
    // stayed or crashed). So the process ends here, without them.
    std::fflush(nullptr);
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
#else
    _exit(code);
#endif
#endif
    return code;
}
