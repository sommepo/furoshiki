// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The OCR helper beside the program (docs/furoshiki-3.md, "The helper beside the program"):
// Furochibi's service in its helper role, installed in a folder of its own on this computer
// (../furochibi/windows/helper/install.ps1 makes C:\Readers\FurochibiHelper). That folder's
// helper.json says where it answers, the picture source this device feeds there with its
// token, and how it is started:
//
//   { "format": 1, "name": "Furochibi OCR helper",
//     "address": "http://127.0.0.1:8794", "source": "furoshiki", "token": "...",
//     "program": [ "python/python.exe", "-m", "console_ocr", "helper", "--config", "config.json",
//                  "--stop-with-stdin" ],
//     "env": { "PYTHONPATH": "{folder}/app", "HF_HOME": "{folder}/hf" } }
//
// The device's setting names the folder (line_titles.h, Lines::Helper::program). When nothing
// answers at the address, the program is started hidden, in its folder, as an add-on's
// companion is (addons.h), and waited for; it is stopped when Furoshiki quits, and ends by
// itself if Furoshiki ends any other way (its standard input is a pipe Furoshiki holds). One
// that answers already (started by hand) is used and left running.

#include <functional>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include "line_titles.h"

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace Home {

struct LocalHelper {
    QString folder; ///< Absolute; empty: none (or a helper.json that cannot be used)
    QString name;
    QString address, source, token;
    QStringList program; ///< The program and its arguments
    QList<QPair<QString, QString>> environment;

    [[nodiscard]] bool Valid() const {
        return !folder.isEmpty();
    }
};

/// The newest format of helper.json read here.
inline constexpr int LocalHelperFormat = 1;

/// FOLDER/helper.json. Not Valid(), with the reason in `problem`, when there is none or it
/// lacks an address, a source, a token or a program.
[[nodiscard]] LocalHelper ReadLocalHelper(const QString& folder, QString* problem = nullptr);
/// Where an installed helper is looked for, in order: the folder READER_HOME_OCR_HELPER names,
/// "FurochibiHelper" beside the program's folder (C:\Readers\FurochibiHelper beside
/// C:\Readers\Furoshiki), and the program's own "FurochibiHelper".
[[nodiscard]] QStringList LocalHelperFolders(const QString& program_folder);
[[nodiscard]] LocalHelper FindLocalHelper(const QString& program_folder);
/// The device's setting for an installed helper.
[[nodiscard]] Lines::Helper SettingOf(const LocalHelper& local);
/// `helper` with the address, source and token its own folder's file says now (a helper
/// installed again may answer elsewhere); as it is when it names no folder or the file is gone.
[[nodiscard]] Lines::Helper Refreshed(const Lines::Helper& helper);

/// The helper this program started, if any. One for the program.
class OcrHelper : public QObject {
    Q_OBJECT

public:
    using Then = std::function<void(bool ok, const QString& what)>;

    static OcrHelper& Instance();
    ~OcrHelper() override;

    /// Calls `then` once the helper answers at its address (ok), or cannot be made to (with the
    /// reason): asked first; when nothing answers and the setting names a folder, started and
    /// waited for. Never before this returns.
    void Ensure(const Lines::Helper& helper, Then then);
    [[nodiscard]] bool Starting() const {
        return starting;
    }
    /// Whether a helper this program started runs.
    [[nodiscard]] bool Runs() const;
    [[nodiscard]] const QString& LogFile() const {
        return log_file;
    }
    /// Stops the helper this program started: its standard input ends, and it is given a
    /// moment to end by itself.
    void Stop();

signals:
    void Changed();

private:
    explicit OcrHelper(QObject* parent);
    void AskHealth(const QString& address, int timeout_ms, std::function<void(bool)> then);
    bool Start(const Lines::Helper& helper, QString& problem);
    void Poll();
    void Finish(bool ok, const QString& what);

    QNetworkAccessManager* net{};
    QProcess* process{};
    QTimer* poll{};
    QList<Then> waiting;
    QString address;  ///< Of the helper being started or started
    QString log_file;
    bool starting{};
    bool asking{};
    qint64 started_at{};
};

} // namespace Home
