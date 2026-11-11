// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ocr_helper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

namespace Home {

namespace {
/// How long a helper may take to answer after its start (Python and its packages read from a
/// cold disk, a virus scanner looking at each).
constexpr int StartMs = 90000;
constexpr int PollMs = 300;
} // namespace

LocalHelper ReadLocalHelper(const QString& folder, QString* problem) {
    const auto fail = [&](const QString& why) {
        if (problem != nullptr) {
            *problem = why;
        }
        return LocalHelper{};
    };
    if (folder.isEmpty()) {
        return fail(QStringLiteral("no folder"));
    }
    const QString path = QDir(folder).absoluteFilePath(QStringLiteral("helper.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("%1 is not there").arg(QDir::toNativeSeparators(path)));
    }
    QByteArray text = file.readAll();
    if (text.startsWith("\xef\xbb\xbf")) {
        text.remove(0, 3); // as Windows' own tools write UTF-8
    }
    QJsonParseError error{};
    const QJsonObject root = QJsonDocument::fromJson(text, &error).object();
    if (error.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), error.errorString()));
    }
    if (root.value(QStringLiteral("format")).toInt(1) > LocalHelperFormat) {
        return fail(QStringLiteral("%1 is of a newer Furoshiki's").arg(QDir::toNativeSeparators(path)));
    }
    LocalHelper local;
    local.name = root.value(QStringLiteral("name")).toString(QStringLiteral("OCR helper"));
    local.address = Lines::CleanHelperAddress(root.value(QStringLiteral("address")).toString());
    local.source = root.value(QStringLiteral("source")).toString().trimmed();
    local.token = root.value(QStringLiteral("token")).toString();
    for (const QJsonValue& word : root.value(QStringLiteral("program")).toArray()) {
        local.program.append(word.toString());
    }
    const QJsonObject env = root.value(QStringLiteral("env")).toObject();
    for (auto it = env.begin(); it != env.end(); ++it) {
        local.environment.append({it.key(), it.value().toString()});
    }
    if (local.address.isEmpty() || local.source.isEmpty() || local.token.isEmpty() || local.program.isEmpty() ||
        local.program.first().isEmpty()) {
        return fail(QStringLiteral("%1 lacks an address, a source, a token or a program").arg(QDir::toNativeSeparators(path)));
    }
    local.folder = QDir(folder).absolutePath();
    return local;
}

QStringList LocalHelperFolders(const QString& program_folder) {
    QStringList folders;
    if (const QString named = qEnvironmentVariable("READER_HOME_OCR_HELPER"); !named.isEmpty()) {
        folders.append(QDir(named).absolutePath());
    }
    if (!program_folder.isEmpty()) {
        // Furoshiki's own helper (ocr-helper/, the published one) where its installer puts it, in
        // the program's folder or beside it; then Furochibi's, which answers the same.
        for (const char* name : {"FuroshikiOcrHelper", "FurochibiHelper"}) {
            folders.append(QDir(program_folder).absoluteFilePath(QString::fromLatin1(name)));
            folders.append(QDir::cleanPath(QDir(program_folder).absoluteFilePath(QStringLiteral("../") + QString::fromLatin1(name))));
        }
    }
    return folders;
}

LocalHelper FindLocalHelper(const QString& program_folder) {
    for (const QString& folder : LocalHelperFolders(program_folder)) {
        if (LocalHelper local = ReadLocalHelper(folder); local.Valid()) {
            return local;
        }
    }
    return {};
}

Lines::Helper SettingOf(const LocalHelper& local) {
    Lines::Helper helper;
    helper.address = local.address;
    helper.source = local.source;
    helper.token = local.token;
    helper.program = local.folder;
    return helper;
}

Lines::Helper Refreshed(const Lines::Helper& helper) {
    if (helper.program.isEmpty()) {
        return helper;
    }
    const LocalHelper local = ReadLocalHelper(helper.program);
    if (!local.Valid()) {
        return helper;
    }
    Lines::Helper now = SettingOf(local);
    now.engine = helper.engine; // the device's own choice, which the helper's folder does not say
    return now;
}

OcrHelper& OcrHelper::Instance() {
    static OcrHelper* instance = new OcrHelper(QCoreApplication::instance());
    return *instance;
}

OcrHelper::OcrHelper(QObject* parent) : QObject(parent), net(new QNetworkAccessManager(this)), poll(new QTimer(this)) {
    // The helper is this computer's: no proxy of the system's is asked for it.
    net->setProxy(QNetworkProxy::NoProxy);
    poll->setInterval(PollMs);
    connect(poll, &QTimer::timeout, this, &OcrHelper::Poll);
    if (QCoreApplication::instance() != nullptr) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &OcrHelper::Stop);
    }
}

OcrHelper::~OcrHelper() {
    Stop();
}

bool OcrHelper::Runs() const {
    return process != nullptr && process->state() != QProcess::NotRunning;
}

void OcrHelper::AskHealth(const QString& at, int timeout_ms, std::function<void(bool)> then) {
    QNetworkRequest request(QUrl(at + QStringLiteral("/api/health")));
    request.setTransferTimeout(timeout_ms);
    QNetworkReply* reply = net->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, then = std::move(then)] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                        QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("status")).toString() == QStringLiteral("ok");
        then(ok);
    });
}

void OcrHelper::Ensure(const Lines::Helper& said, Then then) {
    const Lines::Helper helper = Refreshed(said);
    if (starting && helper.address == address) {
        waiting.append(std::move(then));
        return;
    }
    AskHealth(helper.address, 1500, [this, helper, then = std::move(then)](bool ok) mutable {
        if (ok) {
            then(true, QString());
            return;
        }
        if (helper.program.isEmpty()) {
            then(false, QStringLiteral("nothing answers at %1").arg(helper.address));
            return;
        }
        if (starting && helper.address == address) {
            waiting.append(std::move(then)); // another asked meanwhile, and it is being started
            return;
        }
        QString problem;
        if (!Start(helper, problem)) {
            then(false, problem);
            return;
        }
        waiting.append(std::move(then));
    });
}

bool OcrHelper::Start(const Lines::Helper& helper, QString& problem) {
    const LocalHelper local = ReadLocalHelper(helper.program, &problem);
    if (!local.Valid()) {
        return false;
    }
    Stop(); // one at another address, or one that stopped answering
    QString program = local.program.first();
    if (const QString own = QDir(local.folder).absoluteFilePath(program); QFileInfo(own).isFile()) {
        program = own;
    }
    process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Nothing of a Python the user has of their own reaches the helper's.
    environment.remove(QStringLiteral("PYTHONHOME"));
    environment.remove(QStringLiteral("PYTHONPATH"));
    for (const auto& [name, value] : local.environment) {
        QString text = value;
        text.replace(QStringLiteral("{folder}"), local.folder);
        environment.insert(name, text);
    }
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(local.folder);
    const QString logs = QDir(local.folder).absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(logs);
    log_file = QDir(logs).absoluteFilePath(QStringLiteral("furoshiki.log"));
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setStandardOutputFile(log_file, QIODevice::Truncate);
    // Its standard input stays a pipe held here (never written): when this program ends,
    // however it ends, the helper's input ends and so does the helper (--stop-with-stdin).
#ifdef Q_OS_WIN
    // Never a console window of its own (CREATE_NO_WINDOW), whatever started Furoshiki.
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= 0x08000000; });
#endif
    process->start(program, local.program.mid(1));
    if (!process->waitForStarted(5000)) {
        problem = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(program), process->errorString());
        process->deleteLater();
        process = nullptr;
        return false;
    }
    address = helper.address;
    starting = true;
    asking = false;
    started_at = QDateTime::currentMSecsSinceEpoch();
    poll->start();
    emit Changed();
    return true;
}

void OcrHelper::Poll() {
    if (!starting) {
        poll->stop();
        return;
    }
    if (!Runs()) {
        Finish(false, QStringLiteral("the OCR helper stopped as it started: see %1").arg(QDir::toNativeSeparators(log_file)));
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() - started_at > StartMs) {
        Stop();
        Finish(false, QStringLiteral("the OCR helper did not answer at %1 within %2 s: see %3")
                          .arg(address).arg(StartMs / 1000).arg(QDir::toNativeSeparators(log_file)));
        return;
    }
    if (asking) {
        return;
    }
    asking = true;
    const QString at = address;
    AskHealth(at, 1500, [this, at](bool ok) {
        asking = false;
        if (ok && starting && at == address) {
            Finish(true, QString());
        }
    });
}

void OcrHelper::Finish(bool ok, const QString& what) {
    starting = false;
    poll->stop();
    const QList<Then> thens = std::move(waiting);
    waiting.clear();
    emit Changed();
    for (const Then& then : thens) {
        then(ok, what);
    }
}

void OcrHelper::Stop() {
    if (process == nullptr) {
        return;
    }
    QProcess* gone = process;
    process = nullptr;
    if (gone->state() != QProcess::NotRunning) {
        gone->closeWriteChannel();
        if (!gone->waitForFinished(4000)) {
            gone->kill();
            gone->waitForFinished(2000);
        }
    }
    gone->deleteLater();
    if (starting) {
        Finish(false, QStringLiteral("the OCR helper was stopped"));
    }
    emit Changed();
}

} // namespace Home
