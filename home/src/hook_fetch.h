// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Where the hook engines' files are, and fetching them (docs/hooks.md). None of them is in this
// repository. In <shared folder>/program/hooks:
//   scripts/            0xDC00's scripts, their repository's newest (MIT), fetched when asked
//   frida-<version>/    frida-inject, where it did not come with the program (hooks/ beside it)
//   textractor-<v>/     x86/ and x64/ of Textractor's release (GPL-3.0), fetched at first use
//   luna-<version>/     LunaTranslator's LunaHook files (GPL-3.0), fetched at first use
// Fetched with the system's curl and tar (Windows 10 and later have both), so that no TLS of
// the program's own is needed.

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace Home::Hooks {

inline const QString FridaVersion = QStringLiteral("16.7.19");
inline const QString TextractorVersion = QStringLiteral("5.2.0");
inline const QString LunaVersion = QStringLiteral("12.1.3.9");

[[nodiscard]] QString HooksFolder(const QString& shared);
[[nodiscard]] QString ScriptsFolder(const QString& shared);
/// frida-inject: READER_HOME_FRIDA_INJECT, else hooks/ beside the program, else the fetched one.
/// Empty when there is none.
[[nodiscard]] QString FridaInject(const QString& shared);
/// TextractorCLI of the game's size (READER_HOME_TEXTRACTOR_CLI: a stand-in's command, its
/// words separated by "|"), as a command; empty when it was not fetched.
[[nodiscard]] QStringList TextractorCommand(const QString& shared, bool is32);
/// Furoshiki's LunaHook host with the folder of LunaTranslator's files (READER_HOME_LUNA_HOST:
/// a stand-in's command); empty when either is missing.
[[nodiscard]] QStringList LunaCommand(const QString& shared);
[[nodiscard]] QString LunaFolder(const QString& shared);
/// When the scripts were fetched (ms since 1970), 0 for never.
[[nodiscard]] qint64 ScriptsFetched(const QString& shared);

/// What a kit is fetched from and how large the download is, for the page to say.
struct Kit {
    QString key;   ///< "scripts", "frida", "textractor", "luna"
    QString name;
    QString from;  ///< Its address
    QString size;  ///< "1.5 MB"
    QString whose; ///< Licence and author
};
[[nodiscard]] Kit KitOf(const QString& key);
/// Whether this system can fetch it by itself (frida-inject on Windows comes with the package).
[[nodiscard]] bool CanFetch(const QString& key);

class Fetch : public QObject {
    Q_OBJECT

public:
    explicit Fetch(QObject* parent = nullptr);
    ~Fetch() override;
    /// Begins fetching a kit. False when one is being fetched or this system cannot.
    bool Start(const QString& key, const QString& shared);
    [[nodiscard]] bool Busy() const {
        return !steps.isEmpty() || process.state() != QProcess::NotRunning;
    }
    [[nodiscard]] const QString& Key() const {
        return key;
    }
    /// For the user: what it is doing, or how it ended.
    [[nodiscard]] const QString& Said() const {
        return said;
    }

signals:
    void Done(const QString& key, bool ok, const QString& said);

private:
    struct Step {
        QString what;
        QString program;
        QStringList arguments;
    };
    void Next();
    void End(bool ok, const QString& why);
    bool Finish(QString& why);

    QProcess process;
    QList<Step> steps;
    QString key, shared, said, work;
};

} // namespace Home::Hooks
