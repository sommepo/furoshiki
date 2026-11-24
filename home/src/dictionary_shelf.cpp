// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The Dictionaries page's work, on the reader's database (dictionary_shelf.h). What the old
// Dictionaries window did (shared/reader/qt/reader_dialogs.cpp, Dictionaries), without a window:
// an import runs on a thread of its own and the page shows how far it is.

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include "dictionary_shelf.h"
#include "reader/desktop/dictionary.h"
#include "reader/qt/host.h"
#include "reader/qt/reader_session.h"

namespace Home {
namespace {

const QString JitendexAddress =
    QStringLiteral("https://github.com/stephenmk/stephenmk.github.io/releases/latest/download/jitendex-yomitan.zip");
constexpr int JitendexMegabytes = 39;

class ReaderDictionaryShelf final : public DictionaryShelf {
public:
    ReaderDictionaryShelf() {
        pool.setMaxThreadCount(1);
    }

    ~ReaderDictionaryShelf() override {
        // Quitting while an import runs: it is abandoned (the database is left as it was) and
        // waited for, so that nothing writes to the database once the program is going.
        changed = nullptr;
        say = nullptr;
        stopped = true;
        queue.clear();
        if (download_ticks != nullptr) {
            download_ticks->stop();
        }
        if (download != nullptr) {
            QObject::disconnect(download, nullptr, &context, nullptr);
            download->kill();
            download->waitForFinished(2000);
        }
        pool.waitForDone();
    }

    QList<DictionaryEntry> List() override {
        // An import holds the database for as long as it takes: the list is the one read last.
        if (stale && !work.busy) {
            Read();
        }
        return list;
    }

    DictionaryWork Work() const override {
        return work;
    }

    QStringList Problems() const override {
        return problems;
    }

    void ClearProblems() override {
        if (!problems.isEmpty() || !last_import.isEmpty()) {
            problems.clear();
            last_import.clear();
            Changed();
        }
    }

    void Refresh() override {
        stale = true;
        Changed();
    }

    bool Converting() const override {
        return Database().Converting();
    }

    int AbandonConversion() override {
        const int count = static_cast<int>(Database().AbandonConversion().size());
        stale = true;
        Changed();
        return count;
    }

    DictionaryConversion Conversion() const override {
        const auto now = Database().ConversionProgress();
        return {now.done, now.total, now.title, now.step, now.steps, now.finished, now.failed, now.seconds, now.resumed};
    }

    QString LastImport() const override {
        return last_import;
    }

    void SetEnabled(const QList<qint64>& ids, bool on) override {
        if (ids.isEmpty() || Refused()) {
            return;
        }
        try {
            Database().SetEnabled(ids, on);
        } catch (const std::exception& error) {
            Say(QStringLiteral("Couldn't change the dictionaries: %1").arg(QString::fromUtf8(error.what())));
        }
        stale = true;
        Changed();
    }

    void Reorder(const QList<qint64>& wanted) override {
        if (Refused()) {
            return;
        }
        // The whole order is written: the names dictionaries keep their places in it, the
        // others take the places the others had, in their new order.
        const QList<DictionaryEntry> current = List();
        QSet<qint64> existing_other;
        for (const DictionaryEntry& entry : current) {
            if (!entry.names) {
                existing_other.insert(entry.id);
            }
        }
        const QSet<qint64> wanted_set(wanted.begin(), wanted.end());
        if (wanted.size() != existing_other.size() || wanted_set != existing_other) {
            return; // not the same dictionaries: the list changed meanwhile
        }
        QList<qint64> order;
        int next = 0;
        for (const DictionaryEntry& entry : current) {
            if (entry.names) {
                order.append(entry.id);
            } else {
                order.append(wanted[next++]);
            }
        }
        try {
            Database().SetOrder(order);
        } catch (const std::exception& error) {
            Say(QStringLiteral("Couldn't change the order: %1").arg(QString::fromUtf8(error.what())));
        }
        stale = true;
        Changed();
    }

    void Delete(const QList<qint64>& ids) override {
        if (ids.isEmpty() || Refused()) {
            return;
        }
        QString title;
        for (const DictionaryEntry& entry : list) {
            title = entry.id == ids.first() ? entry.title : title;
        }
        const QString what = ids.size() == 1 ? title : QStringLiteral("%1 dictionaries").arg(ids.size());
        work = {};
        work.busy = true;
        work.doing = QStringLiteral("Deleting %1").arg(what);
        Changed();
        // Taking a large dictionary out of the old file takes a while: not on the window's thread.
        pool.start([this, ids, what] {
            QString problem;
            try {
                Database().Delete(ids);
            } catch (const std::exception& error) {
                problem = QString::fromUtf8(error.what());
                LOG_ERROR(Frontend, "[Reader] Deleting dictionaries failed: {}", error.what());
            }
            QMetaObject::invokeMethod(&context, [this, what, problem] {
                work = {};
                stale = true;
                Say(problem.isEmpty() ? QStringLiteral("Deleted %1").arg(what)
                                      : QStringLiteral("Couldn't delete %1: %2").arg(what, problem));
                Changed();
            }, Qt::QueuedConnection);
        });
    }

    void Import(const QStringList& paths) override {
        if (Refused()) {
            return;
        }
        // Every zip in a folder and the ones in it (the maintainer keeps them nested, as
        // 日本語/国語/大辞泉/x.zip), in the order of their paths.
        QStringList files;
        for (const QString& path : paths) {
            if (QFileInfo(path).isDir()) {
                QStringList found;
                QDirIterator walk(path, {QStringLiteral("*.zip"), QStringLiteral("*.ZIP")}, QDir::Files,
                                  QDirIterator::Subdirectories);
                while (walk.hasNext()) {
                    found.append(walk.next());
                }
                found.sort();
                files += found;
            } else if (!path.isEmpty()) {
                files.append(path);
            }
        }
        if (files.isEmpty()) {
            Say(QStringLiteral("No Yomitan dictionary files (.zip) found"));
            return;
        }
        Begin(files, false);
    }

    void DownloadJitendex() override {
        if (Refused()) {
            return;
        }
        const QString file = ReaderUi::ReaderSession::DataDirectory() + QStringLiteral("/jitendex-yomitan.zip");
        QFile::remove(file);
        problems.clear();
        stopped = false;
        work = {};
        work.busy = true;
        work.doing = QStringLiteral("Downloading Jitendex");
        work.total = JitendexMegabytes;
        work.unit = QStringLiteral("MB");
        download = new QProcess(&context);
        // How far it is: the file's size as it grows (curl says nothing while it is quiet).
        download_ticks = new QTimer(download);
        QObject::connect(download_ticks, &QTimer::timeout, &context, [this, file] {
            work.done = static_cast<int>(QFileInfo(file).size() / 1048576);
            if (work.done > work.total) {
                work.total = work.done;
            }
            Changed();
        });
        download_ticks->start(250);
        QObject::connect(download, &QProcess::finished, &context, [this, file] { Downloaded(file); });
        QObject::connect(download, &QProcess::errorOccurred, &context, [this, file](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                Downloaded(file);
            }
        });
        download->start(QStringLiteral("curl"),
                        {QStringLiteral("-L"), QStringLiteral("--proto"), QStringLiteral("=https"),
                         QStringLiteral("--proto-redir"), QStringLiteral("=https"), QStringLiteral("--connect-timeout"),
                         QStringLiteral("30"), QStringLiteral("--fail"), QStringLiteral("--silent"),
                         QStringLiteral("--show-error"), QStringLiteral("--max-time"), QStringLiteral("600"),
                         QStringLiteral("-o"), file, JitendexAddress});
        Changed();
    }

    void Cancel() override {
        if (!work.busy) {
            return;
        }
        stopped = true;
        queue.clear();
        if (download != nullptr) {
            download->kill();
        }
    }

private:
    static ReaderApp::DictionaryDatabase& Database() {
        return ReaderUi::ReaderSession::Instance().Dictionaries();
    }

    void Say(const QString& text) {
        if (say) {
            say(text);
        }
    }

    void Changed() {
        if (changed) {
            changed();
        }
    }

    bool Refused() {
        if (Converting()) {
            Say(QStringLiteral("Wait until dictionaries finish updating"));
            return true;
        }
        if (!work.busy) {
            return false;
        }
        const bool cancellable = !work.doing.startsWith(QStringLiteral("Deleting")) &&
                                 !work.doing.startsWith(QStringLiteral("Indexing"));
        Say(cancellable ? QStringLiteral("Wait for the current operation to finish, or cancel it first")
                        : QStringLiteral("Wait for the current operation to finish"));
        return true;
    }

    void Read() {
        stale = false;
        list.clear();
        try {
            for (const ReaderApp::DictionaryInfo& info : Database().Dictionaries()) {
                list.append({info.id, info.title, info.revision, info.author, info.description, info.frequency_mode,
                             info.term_count, info.meta_count, info.media, info.enabled, !info.scope.isEmpty(), info.kanji_count});
            }
        } catch (const std::exception& error) {
            LOG_ERROR(Frontend, "[Reader] Reading the dictionaries failed: {}", error.what());
        }
    }

    void Downloaded(const QString& file) {
        if (download == nullptr) {
            return;
        }
        if (download_ticks != nullptr) {
            download_ticks->stop();
            download_ticks = nullptr;
        }
        const bool started = download->error() != QProcess::FailedToStart;
        const bool fine = started && download->exitStatus() == QProcess::NormalExit && download->exitCode() == 0;
        const QString said = started ? QString::fromUtf8(download->readAllStandardError()).trimmed() : QString();
        download->deleteLater();
        download = nullptr;
        if (stopped || !fine) {
            QFile::remove(file);
            work = {};
            if (!stopped) {
                Say(started ? QStringLiteral("Couldn't download Jitendex: %1")
                                  .arg(said.isEmpty() ? QStringLiteral("check your internet connection and try again") : said)
                            : QStringLiteral("Couldn't download Jitendex (curl wasn't found)"));
            }
            Changed();
            return;
        }
        Begin({file}, true);
    }

    /// The files' import, one at a time, each on the pool's thread.
    void Begin(const QStringList& files, bool downloaded_) {
        problems.clear();
        last_import.clear();
        stopped = false;
        downloaded = downloaded_;
        queue.assign(files.begin(), files.end());
        files_count = static_cast<int>(files.size());
        imported = 0;
        already = 0;
        last_title.clear();
        last_entries = 0;
        work = {};
        work.busy = true;
        work.files = files_count;
        Next();
    }

    void Next() {
        if (queue.empty()) {
            Finished();
            return;
        }
        const QString file = queue.front();
        queue.pop_front();
        work.file = files_count - static_cast<int>(queue.size());
        work.doing = QStringLiteral("Importing %1").arg(QFileInfo(file).completeBaseName());
        work.done = 0;
        work.total = 0;
        work.unit.clear();
        Changed();
        pool.start([this, file] {
            ReaderApp::DictionaryImporter importer(Database());
            const ReaderApp::DictionaryImporter::Result result = importer.Import(
                file,
                [this](int done, int total, const QString& title) {
                    QMetaObject::invokeMethod(&context, [this, done, total, title] {
                        if (!title.isEmpty()) {
                            work.doing = QStringLiteral("Importing %1").arg(title);
                        }
                        work.done = done;
                        work.total = total;
                        Changed();
                    }, Qt::QueuedConnection);
                },
                [this] { return !stopped.load(); });
            QMetaObject::invokeMethod(&context, [this, file, result] { Imported(file, result); }, Qt::QueuedConnection);
        });
    }

    void Imported(const QString& file, const ReaderApp::DictionaryImporter::Result& result) {
        // Each import's speed, in Furoshiki's log, for whoever looks into a slow one.
        LOG_INFO(Frontend, "[Reader] Dictionary {} ({})", result.Summary().toStdString(),
                 (downloaded ? QStringLiteral("downloaded") : QFileInfo(file).fileName()).toStdString());
        if (downloaded) {
            QFile::remove(file);
        }
        imported += result.ok ? 1 : 0;
        already += result.already_there ? 1 : 0;
        if (result.ok) {
            // Said by name when it was the only one (the maintainer, 2026-11-24: an import on the
            // Fold gave no clear sign that it had happened at all).
            last_title = result.title;
            last_entries = result.terms + result.metas + result.kanji;
        }
        if (!result.ok && !stopped && !result.not_dictionary && !result.already_there) {
            problems.append(QStringLiteral("%1: %2").arg(QFileInfo(file).fileName(), result.error));
        }
        // The list read again between two imports, while the pool's thread is idle, so that each
        // dictionary shows on the page as it comes in (the maintainer's recording, 2026-11-23: 18
        // imported while the page said "No dictionaries" all the time).
        Read();
        if (stopped) {
            queue.clear();
        }
        Next();
    }

    void Finished() {
        const bool cancelled = stopped;
        if (imported > 0) {
            // Rebuild the library index off the UI thread while keeping the busy banner up, so
            // Read() and user clicks don't stall on Library::mutex.
            work = {};
            work.busy = true;
            work.doing = QStringLiteral("Indexing dictionaries");
            Changed();
            pool.start([this, cancelled] {
                try {
                    Database().Compact();
                } catch (const std::exception& error) {
                    LOG_WARNING(Frontend, "[Reader] Indexing dictionaries failed: {}", error.what());
                }
                QMetaObject::invokeMethod(&context, [this, cancelled] { Complete(cancelled); }, Qt::QueuedConnection);
            });
            return;
        }
        Complete(cancelled);
    }

    void Complete(bool cancelled) {
        work = {};
        Read();
        // One line for all of it, kept on the page until the next import: what came in, what
        // was there already, what could not be taken (its details behind it).
        QStringList parts;
        if (downloaded && imported > 0) {
            parts.append(QStringLiteral("Jitendex installed"));
        } else if (imported == 1 && files_count == 1 && !last_title.isEmpty()) {
            parts.append(last_entries == 1 ? QStringLiteral("Imported %1 (1 entry)").arg(last_title)
                         : last_entries > 1
                             ? QStringLiteral("Imported %1 (%2 entries)").arg(last_title, QLocale(QLocale::English).toString(last_entries))
                             : QStringLiteral("Imported %1").arg(last_title));
        } else if (imported > 0 || (already == 0 && problems.isEmpty())) {
            parts.append(imported == 1 ? QStringLiteral("Imported 1 dictionary") : QStringLiteral("Imported %1 dictionaries").arg(imported));
        }
        if (already > 0) {
            parts.append(QStringLiteral("%1 already installed").arg(already));
        }
        if (!problems.isEmpty()) {
            parts.append(problems.size() == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(problems.size()));
        }
        if (cancelled) {
            parts.append(QStringLiteral("cancelled"));
        }
        if (imported == 0 && already == 0 && problems.isEmpty() && !cancelled && !downloaded) {
            parts = {QStringLiteral("No Yomitan dictionaries found")};
        }
        last_import = parts.join(QStringLiteral(" · "));
        Say(last_import);
        Changed();
    }

    QObject context; ///< What the pool's thread hands its news to, on the window's thread
    QThreadPool pool;
    std::atomic<bool> stopped{false};
    std::deque<QString> queue;
    int files_count{};
    int imported{};
    int already{}; ///< Of the import, installed already
    bool downloaded{};
    QPointer<QProcess> download;
    QPointer<QTimer> download_ticks;
    DictionaryWork work;
    QStringList problems;
    QString last_import;
    QString last_title;   ///< The last dictionary imported, by its title
    qint64 last_entries{}; ///< And how many entries it has
    QList<DictionaryEntry> list;
    bool stale{true};
};

} // namespace

std::unique_ptr<DictionaryShelf> MakeReaderDictionaryShelf() {
    return std::make_unique<ReaderDictionaryShelf>();
}

} // namespace Home
