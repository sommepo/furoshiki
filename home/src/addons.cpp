// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSet>
#include "addons.h"
#include "play_settings.h"

namespace Home {

namespace {

const QString FileName = QStringLiteral("addon.json");

QString SystemName() {
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QStringList Strings(const QJsonValue& value) {
    QStringList out;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            out.append(item.toString());
        }
    }
    return out;
}

QJsonObject ReadObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool WriteObject(const QString& path, const QJsonObject& object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

QByteArray ReadAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

} // namespace

bool Addon::For(const QString& console_key, const QString& game) const {
    return !game.isEmpty() && games.contains(game) && (console.isEmpty() || console == console_key);
}

QStringList AddonFolders(const QString& shared_folder, const QString& program_folder) {
    QStringList folders;
    if (!shared_folder.isEmpty()) {
        folders.append(QDir(shared_folder).absoluteFilePath(QStringLiteral("addons")));
    }
    if (!program_folder.isEmpty()) {
        folders.append(QDir(program_folder).absoluteFilePath(QStringLiteral("addons")));
        const QDir beside = QFileInfo(QDir(program_folder).absolutePath()).dir();
        // Not the root of a disc, nor the program's own folder's sisters on Linux's /usr.
        if (!beside.isRoot()) {
            folders.append(beside.absolutePath());
        }
    }
    return folders;
}

Addon ReadAddon(const QString& folder, QString* problem) {
    const auto fail = [problem](const QString& why) {
        if (problem != nullptr) {
            *problem = why;
        }
        return Addon{};
    };
    const QString path = QDir(folder).absoluteFilePath(FileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(path + QStringLiteral(": cannot be read"));
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (!document.isObject()) {
        return fail(path + QStringLiteral(": ") + error.errorString());
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toInt() > AddonFormat) {
        return fail(path + QStringLiteral(": a newer format than this Furoshiki reads"));
    }
    Addon addon;
    addon.folder = QDir(folder).absolutePath();
    addon.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (addon.name.isEmpty()) {
        addon.name = QFileInfo(addon.folder).fileName();
    }
    addon.about = root.value(QStringLiteral("about")).toString();
    addon.console = root.value(QStringLiteral("console")).toString();
    addon.games = Strings(root.value(QStringLiteral("games")));
    if (addon.games.isEmpty()) {
        return fail(path + QStringLiteral(": no games"));
    }
    const QString scripts = root.value(QStringLiteral("scripts")).toString();
    if (!scripts.isEmpty()) {
        addon.scripts = QDir(addon.folder).absoluteFilePath(scripts);
    }
    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        addon.settings.append({it.key(), it.value().isString() ? it.value().toString()
                                                               : it.value().toVariant().toString()});
    }
    const QJsonObject companion = root.value(QStringLiteral("companion")).toObject();
    addon.companion = Strings(companion.value(SystemName()));
    const QJsonObject environment = companion.value(QStringLiteral("env")).toObject();
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
        addon.environment.append({it.key(), it.value().toString()});
    }
    addon.url = companion.value(QStringLiteral("url")).toString();
    return addon;
}

QList<Addon> FindAddons(const QStringList& folders, QStringList* problems) {
    QList<Addon> found;
    QSet<QString> names;
    QSet<QString> seen;
    for (const QString& folder : folders) {
        const QDir dir(folder);
        if (!dir.exists()) {
            continue;
        }
        for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString path = QDir::cleanPath(dir.absoluteFilePath(entry));
            if (seen.contains(path) || !QFileInfo::exists(QDir(path).absoluteFilePath(FileName))) {
                continue;
            }
            seen.insert(path);
            QString problem;
            Addon addon = ReadAddon(path, &problem);
            if (addon.name.isEmpty()) {
                if (problems != nullptr) {
                    problems->append(problem);
                }
                continue;
            }
            if (names.contains(addon.name.toLower())) {
                continue;
            }
            names.insert(addon.name.toLower());
            found.append(std::move(addon));
        }
    }
    return found;
}

QStringList InstallScripts(const Addon& addon, const QString& profiles, const QString& state_file) {
    QStringList copied;
    if (addon.scripts.isEmpty()) {
        return copied;
    }
    QJsonObject state = ReadObject(state_file);
    QJsonObject ours = state.value(QStringLiteral("scripts")).toObject();
    const QDir from(addon.scripts);
    QDir().mkpath(profiles);
    for (const QString& name : from.entryList(QDir::Files, QDir::Name)) {
        const QByteArray content = ReadAll(from.absoluteFilePath(name));
        const QString target = QDir(profiles).absoluteFilePath(name);
        if (QFileInfo::exists(target)) {
            if (ReadAll(target) == content) {
                continue;
            }
            // The user's own file of that name (not one an add-on put there): kept once.
            const QString kept = target + QStringLiteral(".before-addon");
            if (!ours.contains(name) && !QFileInfo::exists(kept)) {
                QFile::copy(target, kept);
            }
        }
        QSaveFile file(target);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(content);
            if (file.commit()) {
                ours.insert(name, addon.name);
                copied.append(name);
            }
        }
    }
    if (!copied.isEmpty()) {
        state.insert(QStringLiteral("format"), 1);
        state.insert(QStringLiteral("scripts"), ours);
        WriteObject(state_file, state);
    }
    return copied;
}

bool ApplySettings(const Addon& addon, PlaySettings& settings, const QString& console_key, const QString& game,
                   const QString& state_file) {
    if (addon.settings.isEmpty() || !addon.For(console_key, game)) {
        return false;
    }
    QJsonObject state = ReadObject(state_file);
    QJsonObject applied = state.value(QStringLiteral("settings")).toObject();
    QJsonObject mine = applied.value(addon.name).toObject();
    QStringList done = Strings(mine.value(game));
    bool changed = false;
    for (const auto& [key, value] : addon.settings) {
        if (done.contains(key)) {
            continue;
        }
        if (!settings.IsSet(key, console_key, game)) {
            settings.Set(key, value, console_key, game);
            changed = true;
        }
        done.append(key);
    }
    QJsonArray list;
    for (const QString& key : std::as_const(done)) {
        list.append(key);
    }
    if (mine.value(game).toArray() != list) {
        mine.insert(game, list);
        applied.insert(addon.name, mine);
        state.insert(QStringLiteral("settings"), applied);
        state.insert(QStringLiteral("format"), 1);
        WriteObject(state_file, state);
    }
    return changed;
}

CompanionChannel MakeCompanionChannel(const QString& program_folder) {
#ifdef Q_OS_WIN
    Q_UNUSED(program_folder);
    const QString name = QStringLiteral("furoshiki-companion");
    return {name, QStringLiteral("\\\\.\\pipe\\") + name};
#else
    // A Unix socket's path is at most 107 bytes: the program's folder, else the user's runtime
    // folder, else the temporary folder (with the user's number, as others share it).
    const QString name = QStringLiteral("companion.sock");
    for (const QString& folder : {program_folder, qEnvironmentVariable("XDG_RUNTIME_DIR")}) {
        if (folder.isEmpty()) {
            continue;
        }
        const QString path = QDir(folder).absoluteFilePath(name);
        if (path.toUtf8().size() < 100) {
            return {path, path};
        }
    }
    const QString path = QDir::temp().absoluteFilePath(
        QStringLiteral("furoshiki-companion-%1.sock").arg(QString::number(QCoreApplication::applicationPid())));
    return {path, path};
#endif
}

Companions::Companions(QString control_address_, QString log_folder_, QObject* parent)
    : QObject(parent), control_address{std::move(control_address_)}, log_folder{std::move(log_folder_)} {}

Companions::~Companions() {
    for (QProcess* process : std::as_const(running)) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(2000);
        }
    }
}

bool Companions::Running(const QString& name) const {
    const QProcess* process = running.value(name);
    return process != nullptr && process->state() != QProcess::NotRunning;
}

bool Companions::Start(const Addon& addon, QString* problem) {
    if (addon.companion.isEmpty() || Running(addon.name)) {
        return true;
    }
    QString program = addon.companion.first();
    const QString own = QDir(addon.folder).absoluteFilePath(program);
    if (QFileInfo(own).isFile()) {
        program = own;
    }
    QProcess* process = running.value(addon.name);
    if (process == nullptr) {
        process = new QProcess(this);
        running.insert(addon.name, process);
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto& [name, value] : addon.environment) {
        QString text = value;
        text.replace(QStringLiteral("{control}"), control_address);
        text.replace(QStringLiteral("{folder}"), addon.folder);
        environment.insert(name, text);
    }
    environment.insert(QStringLiteral("FUROSHIKI_CONTROL"), control_address);
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(addon.folder);
    if (!log_folder.isEmpty()) {
        QDir().mkpath(log_folder);
        const QString log = QDir(log_folder).absoluteFilePath(addon.name + QStringLiteral(".log"));
        process->setProcessChannelMode(QProcess::MergedChannels);
        process->setStandardOutputFile(log, QIODevice::Truncate);
    }
    process->start(program, addon.companion.mid(1));
    if (!process->waitForStarted(5000)) {
        if (problem != nullptr) {
            *problem = program + QStringLiteral(": ") + process->errorString();
        }
        return false;
    }
    return true;
}

} // namespace Home
