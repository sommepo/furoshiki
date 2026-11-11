// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hook_fetch.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#ifdef HOME_QUAZIP
#include <quazip.h>
#include <quazipfile.h>
#endif

namespace Home::Hooks {

namespace {
#ifdef _WIN32
constexpr bool OnWindows = true;
const QString Exe = QStringLiteral(".exe");
#else
constexpr bool OnWindows = false;
const QString Exe;
#endif

QString Address(const QString& key) {
    if (key == QStringLiteral("scripts")) {
        return qEnvironmentVariable("READER_HOME_SCRIPTS_URL", QStringLiteral("https://codeload.github.com/0xDC00/scripts/zip/refs/heads/main"));
    }
    if (key == QStringLiteral("frida")) {
        return qEnvironmentVariable("READER_HOME_FRIDA_URL",
                                    QStringLiteral("https://github.com/frida/frida/releases/download/%1/frida-inject-%1-%2").arg(FridaVersion, OnWindows ? QStringLiteral("windows-x86_64.exe.xz") : QStringLiteral("linux-x86_64.xz")));
    }
    if (key == QStringLiteral("textractor")) {
        return QStringLiteral("https://github.com/Artikash/Textractor/releases/download/v%1/Textractor-%1-Zip-Version-English-Only.zip").arg(TextractorVersion);
    }
    if (key == QStringLiteral("luna")) {
        return QStringLiteral("https://github.com/HIllya51/LunaTranslator/releases/download/v%1/LunaTranslator_x64.zip").arg(LunaVersion);
    }
    return {};
}

QStringList Words(const char* variable) {
    return qEnvironmentVariable(variable).split(QLatin1Char('|'), Qt::SkipEmptyParts);
}

/// The members of Textractor's zip that its CLIs need.
QStringList TextractorMembers() {
    QStringList members;
    for (const QString& size : {QStringLiteral("x86"), QStringLiteral("x64")}) {
        for (const QString& file : {QStringLiteral("TextractorCLI.exe"), QStringLiteral("texthook.dll"), QStringLiteral("msvcp140.dll"), QStringLiteral("vcruntime140.dll")}) {
            members.append(QStringLiteral("Textractor/%1/%2").arg(size, file));
        }
    }
    members.append(QStringLiteral("Textractor/x64/vcruntime140_1.dll"));
    return members;
}

/// Unpacks a zip into a folder, its first folder left out (GitHub's zip has the whole tree under
/// "scripts-main/"), with its names as the zip says them in UTF-8. Done here, not by tar: Windows'
/// tar turns names into the console's code page, could not with four of the scripts' (＆, ē, ’, β:
/// "Error exit delayed from previous errors", 2026-11-10 on the Z13) and gave up on all of them.
bool Unzip(const QString& file, const QString& into, QString& why) {
#ifndef HOME_QUAZIP
    (void)file;
    (void)into;
    why = QStringLiteral("this build has no zip reader (it is built without cores)");
    return false;
#else
    QuaZip zip(file);
    if (!zip.open(QuaZip::mdUnzip)) {
        why = QStringLiteral("what was fetched is not a zip");
        return false;
    }
    const QString root = QDir(into).absolutePath() + QLatin1Char('/');
    int count = 0;
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile()) {
        const QString name = zip.getCurrentFileName().section(QLatin1Char('/'), 1);
        if (name.isEmpty()) {
            continue;
        }
        const QString path = QDir::cleanPath(root + name);
        if (!path.startsWith(root)) {
            continue; // nothing outside the folder
        }
        if (name.endsWith(QLatin1Char('/'))) {
            QDir().mkpath(path);
            continue;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QuaZipFile in(&zip);
        QFile out(path);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            why = QStringLiteral("%1 could not be written").arg(name);
            return false;
        }
        out.write(in.readAll());
        ++count;
    }
    if (count == 0) {
        why = QStringLiteral("the zip was empty");
        return false;
    }
    return true;
#endif
}

QStringList LunaMembers() {
    return {QStringLiteral("LunaTranslator_x64/files/LunaHook/LunaHost64.dll"), QStringLiteral("LunaTranslator_x64/files/LunaHook/LunaHook32.dll"),
            QStringLiteral("LunaTranslator_x64/files/LunaHook/LunaHook64.dll"), QStringLiteral("LunaTranslator_x64/files/LunaSubprocess32.exe"),
            QStringLiteral("LunaTranslator_x64/files/LunaSubprocess64.exe")};
}
} // namespace

QString HooksFolder(const QString& shared) {
    return shared + QStringLiteral("/program/hooks");
}

QString ScriptsFolder(const QString& shared) {
    return HooksFolder(shared) + QStringLiteral("/scripts");
}

QString FridaInject(const QString& shared) {
    const QString name = QStringLiteral("frida-inject") + Exe;
    for (const QString& file : {qEnvironmentVariable("READER_HOME_FRIDA_INJECT"), QCoreApplication::applicationDirPath() + QStringLiteral("/hooks/") + name,
                                HooksFolder(shared) + QStringLiteral("/frida-%1/").arg(FridaVersion) + name}) {
        if (!file.isEmpty() && QFileInfo(file).isFile()) {
            return file;
        }
    }
    return {};
}

QStringList TextractorCommand(const QString& shared, bool is32) {
    if (const QStringList standin = Words("READER_HOME_TEXTRACTOR_CLI"); !standin.isEmpty()) {
        return standin;
    }
    const QString file = HooksFolder(shared) + QStringLiteral("/textractor-%1/Textractor/%2/TextractorCLI.exe").arg(TextractorVersion, is32 ? QStringLiteral("x86") : QStringLiteral("x64"));
    return QFileInfo(file).isFile() ? QStringList{file} : QStringList();
}

QString LunaFolder(const QString& shared) {
    return HooksFolder(shared) + QStringLiteral("/luna-%1/LunaTranslator_x64/files").arg(LunaVersion);
}

QStringList LunaCommand(const QString& shared) {
    if (const QStringList standin = Words("READER_HOME_LUNA_HOST"); !standin.isEmpty()) {
        return standin;
    }
    const QString host = QCoreApplication::applicationDirPath() + QStringLiteral("/hooks/furoshiki-luna-host.exe");
    const QString folder = LunaFolder(shared);
    if (!QFileInfo(host).isFile() || !QFileInfo(folder + QStringLiteral("/LunaHook/LunaHost64.dll")).isFile()) {
        return {};
    }
    return {host, QDir::toNativeSeparators(folder)};
}

qint64 ScriptsFetched(const QString& shared) {
    QFile in(HooksFolder(shared) + QStringLiteral("/scripts.json"));
    return in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object().value(QStringLiteral("fetched")).toInteger() : 0;
}

Kit KitOf(const QString& key) {
    if (key == QStringLiteral("scripts")) {
        return {key, QStringLiteral("0xDC00's scripts"), Address(key), QStringLiteral("1.5 MB"), QStringLiteral("0xDC00 and contributors, MIT")};
    }
    if (key == QStringLiteral("frida")) {
        return {key, QStringLiteral("Frida %1 (frida-inject)").arg(FridaVersion), Address(key), OnWindows ? QStringLiteral("20 MB") : QStringLiteral("18.5 MB"),
                QStringLiteral("Frida's authors, wxWindows Library Licence 3.1")};
    }
    if (key == QStringLiteral("textractor")) {
        return {key, QStringLiteral("Textractor %1").arg(TextractorVersion), Address(key), QStringLiteral("35 MB"), QStringLiteral("Artikash, GPL-3.0")};
    }
    if (key == QStringLiteral("luna")) {
        return {key, QStringLiteral("LunaHook (LunaTranslator %1)").arg(LunaVersion), Address(key), QStringLiteral("67 MB"), QStringLiteral("HIllya51, GPL-3.0")};
    }
    return {};
}

bool CanFetch(const QString& key) {
    if (key == QStringLiteral("scripts")) {
        return true;
    }
    if (key == QStringLiteral("frida")) {
        return !OnWindows; // Windows has no xz: frida-inject.exe comes with the package
    }
    return OnWindows && (key == QStringLiteral("textractor") || key == QStringLiteral("luna"));
}

Fetch::Fetch(QObject* parent) : QObject(parent) {
    process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (steps.isEmpty()) {
            return;
        }
        const Step done = steps.takeFirst();
        if (status != QProcess::NormalExit || code != 0) {
            const QString tail = QString::fromLocal8Bit(process.readAll()).trimmed().section(QLatin1Char('\n'), -1);
            End(false, QStringLiteral("%1 failed%2").arg(done.what, tail.isEmpty() ? QString() : QStringLiteral(": ") + tail));
            return;
        }
        Next();
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !steps.isEmpty()) {
            const Step failed = steps.takeFirst();
            End(false, QStringLiteral("%1 is not on this computer").arg(failed.program));
        }
    });
}

Fetch::~Fetch() {
    steps.clear();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
}

bool Fetch::Start(const QString& key_, const QString& shared_) {
    if (Busy() || !CanFetch(key_)) {
        return false;
    }
    key = key_;
    shared = shared_;
    const Kit kit = KitOf(key);
    work = HooksFolder(shared) + QStringLiteral("/fetch-") + key;
    QDir(work).removeRecursively();
    QDir().mkpath(work);
    const QString got = work + QStringLiteral("/download");
    steps.append({QStringLiteral("Fetching %1").arg(kit.name), QStringLiteral("curl"),
                  {QStringLiteral("-fsSL"), QStringLiteral("--retry"), QStringLiteral("2"), QStringLiteral("--connect-timeout"), QStringLiteral("20"), QStringLiteral("-o"), got, kit.from}});
    if (key == QStringLiteral("scripts")) {
        QDir().mkpath(work + QStringLiteral("/scripts"));
        steps.append({QStringLiteral("Unpacking the scripts"), QString(), {got, work + QStringLiteral("/scripts")}}); // here, by Unzip
    } else if (key == QStringLiteral("frida")) {
        QFile::remove(got + QStringLiteral(".xz"));
        steps[0].arguments[6] = got + QStringLiteral(".xz");
        steps.append({QStringLiteral("Unpacking frida-inject"), QStringLiteral("xz"), {QStringLiteral("-d"), QStringLiteral("-f"), got + QStringLiteral(".xz")}});
    } else {
        QDir().mkpath(work + QStringLiteral("/files"));
        QStringList arguments{QStringLiteral("-xf"), got, QStringLiteral("-C"), work + QStringLiteral("/files")};
        arguments += key == QStringLiteral("textractor") ? TextractorMembers() : LunaMembers();
        steps.append({QStringLiteral("Unpacking %1").arg(kit.name), QStringLiteral("tar"), arguments});
    }
    Next();
    return true;
}

void Fetch::Next() {
    if (steps.isEmpty()) {
        QString why;
        const bool ok = Finish(why);
        End(ok, why);
        return;
    }
    said = steps.first().what;
    if (steps.first().program.isEmpty()) {
        // A step done in this program: the zip unpacked.
        const Step step = steps.takeFirst();
        QString why;
        if (!Unzip(step.arguments.value(0), step.arguments.value(1), why)) {
            End(false, QStringLiteral("%1 failed: %2").arg(step.what, why));
            return;
        }
        Next();
        return;
    }
    process.start(steps.first().program, steps.first().arguments);
}

bool Fetch::Finish(QString& why) {
    const QString hooks = HooksFolder(shared);
    const auto swap = [&](const QString& from, const QString& to) {
        QDir(to + QStringLiteral(".old")).removeRecursively();
        if (QFileInfo::exists(to) && !QDir().rename(to, to + QStringLiteral(".old"))) {
            return false;
        }
        const bool moved = QDir().rename(from, to);
        QDir(to + QStringLiteral(".old")).removeRecursively();
        return moved;
    };
    if (key == QStringLiteral("scripts")) {
        if (!QFileInfo(work + QStringLiteral("/scripts/libLoader.js")).isFile()) {
            why = QStringLiteral("What was fetched is not the scripts: it has no libLoader.js");
            return false;
        }
        if (!swap(work + QStringLiteral("/scripts"), ScriptsFolder(shared))) {
            why = QStringLiteral("The scripts could not be put in %1").arg(ScriptsFolder(shared));
            return false;
        }
        QFile out(hooks + QStringLiteral("/scripts.json"));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(QJsonObject{{QStringLiteral("fetched"), QDateTime::currentMSecsSinceEpoch()}, {QStringLiteral("from"), KitOf(key).from}}).toJson());
        }
        const int count = static_cast<int>(QDir(ScriptsFolder(shared)).entryList({QStringLiteral("*.js")}, QDir::Files).size());
        why = QStringLiteral("%1 script files fetched").arg(count);
        return true;
    }
    if (key == QStringLiteral("frida")) {
        const QString folder = hooks + QStringLiteral("/frida-%1").arg(FridaVersion), file = folder + QStringLiteral("/frida-inject") + Exe;
        QDir().mkpath(folder);
        QFile::remove(file);
        if (!QFile::rename(work + QStringLiteral("/download"), file)) {
            why = QStringLiteral("frida-inject could not be put in %1").arg(folder);
            return false;
        }
        QFile::setPermissions(file, QFile::permissions(file) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        why = QStringLiteral("Frida %1 fetched").arg(FridaVersion);
        return true;
    }
    const QString folder = hooks + (key == QStringLiteral("textractor") ? QStringLiteral("/textractor-") + TextractorVersion : QStringLiteral("/luna-") + LunaVersion);
    if (!swap(work + QStringLiteral("/files"), folder)) {
        why = QStringLiteral("%1 could not be put in %2").arg(KitOf(key).name, folder);
        return false;
    }
    why = QStringLiteral("%1 fetched").arg(KitOf(key).name);
    return true;
}

void Fetch::End(bool ok, const QString& why) {
    steps.clear();
    QDir(work).removeRecursively();
    said = why;
    emit Done(key, ok, why);
}

} // namespace Home::Hooks
