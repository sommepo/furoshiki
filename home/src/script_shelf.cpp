// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The Game scripts page's work (script_shelf.h): every console's published list of scripts
// (sommepo/furoshiki-scripts), the user's scripts folder and the program's own, the games' names
// dictionaries; what the old one-console window did (shared/reader/qt/reader_games.cpp), for every
// console at once and many games at a time, its fetching done in the background.

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThreadPool>
#include <QUrl>
#include <optional>
#include "reader/core/console.h"
#include "reader/desktop/catalog.h"
#include "reader/desktop/dictionary.h"
#include "reader/desktop/names.h"
#include "reader/desktop/script_index.h"
#include "reader/qt/host.h"
#include "reader/qt/reader_games.h"
#include "reader/qt/reader_session.h"
#include "script_shelf.h"

namespace Home {
namespace {

namespace Scripts = ReaderApp::Scripts;

const QString Bundled = QStringLiteral(":/reader/profiles");

/// The consoles of the scripts repository: key, name on the page, its folder there.
struct ListConsole {
    const char* key;
    const char* name;
    const char* folder;
};
constexpr ListConsole ListConsoles[] = {
    {"ps1", "PS1", "ps1"},       {"ps2", "PS2", "ps2"}, {"psp", "PSP", "psp"},          {"psvita", "PS Vita", "vita"},
    {"dc", "Dreamcast", "dreamcast"}, {"nds", "DS", "nds"},  {"3ds", "3DS", "3ds"},
#ifndef HOME_PUBLIC
    {"switch", "Switch", "switch"},
#endif
};

/// Where a console's list is: READER_SCRIPTS_ROOT (a folder or an address with a folder of each
/// console, as the repository's `scripts`, for a list being tried), else the published one.
QUrl ListUrl(const ListConsole& console) {
    QString root = qEnvironmentVariable("READER_SCRIPTS_ROOT").trimmed();
    if (root.isEmpty()) {
        root = QStringLiteral("https://raw.githubusercontent.com/sommepo/furoshiki-scripts/main/scripts");
    }
    const QString address = root + QLatin1Char('/') + QString::fromLatin1(console.folder) + QStringLiteral("/index.json");
    return QDir::isAbsolutePath(root) ? QUrl::fromLocalFile(address) : QUrl(address);
}

/// The console a game's ID is of, where neither a list nor the library says (tools/titles.py's
/// rules, as the shelf's ArtPlatform).
QString ConsoleOfId(const QString& id) {
    const QString i = id.toUpper();
    static const QRegularExpression psp(QStringLiteral("^(?:ULJ|NPJ|UCJ|ULA)"));
    static const QRegularExpression vita(QStringLiteral("^PCS"));
    static const QRegularExpression n3ds(QStringLiteral("^0004[0-9A-F]{12}$"));
    static const QRegularExpression swi(QStringLiteral("^[0-9A-F]{16}$"));
    static const QRegularExpression ps2(QStringLiteral("^(?:SLPS-?2|SLPM-?[5-7]|SCPS-?[15-7]|SLPS-?7)"));
    static const QRegularExpression ps1(QStringLiteral("^(?:SLPS-?[0189]|SLPM-?8|SCPS-?[14]0)"));
    static const QRegularExpression dc(QStringLiteral("^(?:T-?\\d|HDR-|MK-|\\d{7}$)"));
    static const QRegularExpression nds(QStringLiteral("^[A-Z0-9]{4}$"));
    if (psp.match(i).hasMatch()) return QStringLiteral("psp");
    if (vita.match(i).hasMatch()) return QStringLiteral("psvita");
    if (n3ds.match(i).hasMatch()) return QStringLiteral("3ds");
#ifndef HOME_PUBLIC
    if (swi.match(i).hasMatch()) return QStringLiteral("switch");
#endif
    if (ps2.match(i).hasMatch()) return QStringLiteral("ps2");
    if (ps1.match(i).hasMatch()) return QStringLiteral("ps1");
    if (dc.match(i).hasMatch()) return QStringLiteral("dc");
    if (nds.match(i).hasMatch()) return QStringLiteral("nds");
    return {};
}

std::optional<QByteArray> ReadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return file.readAll();
}

/// Any console's game IDs while the page reads lists, scripts and folders: the program's pattern
/// is the console it last set up (ApplyConsole), which would pass every other console's games
/// over (the Z13, 2026-11-23: whole lists empty, built-in scripts missing). Put back after.
class AnyGameId {
public:
    AnyGameId() : was(Reader::ConsoleTraits::GameIdPattern) {
        Reader::ConsoleTraits::GameIdPattern = "[A-Za-z0-9-]{4,32}";
    }
    ~AnyGameId() {
        Reader::ConsoleTraits::GameIdPattern = was;
    }
    AnyGameId(const AnyGameId&) = delete;
    AnyGameId& operator=(const AnyGameId&) = delete;

private:
    const char* was;
};

class ReaderScriptShelf final : public ScriptShelf {
public:
    ReaderScriptShelf() : net(new QNetworkAccessManager(&context)) {
        pool.setMaxThreadCount(1);
    }

    ~ReaderScriptShelf() override {
        changed = nullptr;
        say = nullptr;
        scripts_changed = nullptr;
        dictionaries_changed = nullptr;
        finally = nullptr;
        cancelled = true;
        queue.clear();
        if (reply) {
            QObject::disconnect(reply, nullptr, &context, nullptr);
            reply->abort();
            reply->deleteLater();
            reply = nullptr;
        }
        pool.waitForDone();
    }

    QList<ScriptGame> Games() override {
        if (stale) {
            Build();
        }
        return games;
    }

    QList<std::pair<QString, QString>> Consoles() const override {
        QList<std::pair<QString, QString>> out;
        for (const ListConsole& console : ListConsoles) {
            out.append({QString::fromLatin1(console.key), QString::fromLatin1(console.name)});
        }
        return out;
    }

    void FetchLists() override {
        if (work.busy) {
            return;
        }
        problems.clear();
        last.clear();
        Begin(QStringLiteral("Fetching script lists"), static_cast<int>(std::size(ListConsoles)));
        for (const ListConsole& console : ListConsoles) {
            const QString key = QString::fromLatin1(console.key);
            const QUrl url = ListUrl(console);
            queue.push_back({url, [this, key, url](const std::optional<QByteArray>& bytes, const QString& error) {
                                 if (!bytes) {
                                     problems.append(QStringLiteral("%1 script list: %2").arg(NameOf(key), error));
                                 } else {
                                     QString why;
                                     if (auto index = Scripts::ParseIndex(*bytes, &why)) {
                                         lists[key] = {*index, url};
                                     } else {
                                         problems.append(QStringLiteral("%1 script list: %2").arg(NameOf(key), why));
                                     }
                                 }
                                 Step();
                             }});
        }
        Finally([this] {
            if (!cancelled) {
                fetched = true;
            }
            const int failed = static_cast<int>(problems.size());
            if (cancelled) {
                last = QStringLiteral("Cancelled");
            } else if (failed == 0) {
                last.clear();
            } else {
                last = failed == 1 ? QStringLiteral("Couldn't fetch 1 script list")
                                   : QStringLiteral("Couldn't fetch %1 script lists").arg(failed);
            }
        });
        Next();
    }

    bool ListsFetched() const override {
        return fetched;
    }

    ScriptWork Work() const override {
        return work;
    }

    QString LastAction() const override {
        return last;
    }

    QStringList Problems() const override {
        return problems;
    }

    void ClearProblems() override {
        problems.clear();
        last.clear();
        Changed();
    }

    void Install(const QStringList& ids) override {
        if (Refused()) {
            return;
        }
        const AnyGameId any;
        problems.clear();
        last.clear();
        QList<std::pair<Scripts::Entry, QUrl>> wanted;
        for (const QString& id : ids) {
            if (const auto found = EntryOf(id)) {
                wanted.append(*found);
            }
        }
        if (wanted.isEmpty()) {
            Say(QStringLiteral("No downloadable script is available for the selected games"));
            return;
        }
        Begin(wanted.size() == 1 ? QStringLiteral("Installing %1").arg(wanted.first().first.title)
                                 : QStringLiteral("Installing %1 scripts").arg(wanted.size()),
              static_cast<int>(wanted.size()));
        auto installed = std::make_shared<int>(0);
        for (const auto& [entry, url] : wanted) {
            // Each game's files, one after the other, then the game's install.
            auto files = std::make_shared<QHash<QString, QByteArray>>();
            auto failed = std::make_shared<QString>();
            QList<Scripts::FileEntry> wants{entry.script};
            wants += entry.data;
            if (entry.names) {
                wants.append(*entry.names);
            }
            for (const Scripts::FileEntry& file : wants) {
                const bool optional = entry.names && file.file == entry.names->file;
                queue.push_back({url.resolved(QUrl(file.file)), [files, failed, file, optional](const std::optional<QByteArray>& bytes, const QString& error) {
                                     if (bytes) {
                                         files->insert(file.file, *bytes);
                                     } else if (!optional && failed->isEmpty()) {
                                         *failed = error;
                                     }
                                 }});
            }
            queue.push_back({{}, [this, entry, files, failed, installed](const std::optional<QByteArray>&, const QString&) {
                                 QString error = *failed;
                                 if (error.isEmpty()) {
                                     Scripts::Folder folder(ReaderUi::ReaderBridge::UserProfileDirectory());
                                     if (folder.Install(entry, *files, &error)) {
                                         ++*installed;
                                         error.clear();
                                     }
                                 }
                                 if (!error.isEmpty()) {
                                     problems.append(QStringLiteral("%1 (%2): %3").arg(entry.title, entry.id, error));
                                 }
                                 Step();
                             }});
        }
        Finally([this, installed] {
            ReaderUi::Games::Forget();
            QStringList parts{*installed == 1 ? QStringLiteral("Installed 1 script") : QStringLiteral("Installed %1 scripts").arg(*installed)};
            if (!problems.isEmpty()) {
                parts.append(problems.size() == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(problems.size()));
            }
            if (cancelled) {
                parts.append(QStringLiteral("cancelled"));
            }
            last = parts.join(QStringLiteral(" · "));
            if (*installed > 0 && scripts_changed) {
                scripts_changed();
            }
        });
        Next();
    }

    void InstallNames(const QStringList& ids) override {
        if (Refused()) {
            return;
        }
        const AnyGameId any;
        problems.clear();
        last.clear();
        Scripts::Folder folder(ReaderUi::ReaderBridge::UserProfileDirectory());
        const QList<ReaderApp::Names::Pack> existing_packs = Packs();
        QStringList wanted;
        QSet<QString> queued_files;
        for (const QString& id : ids) {
            wanted.append(id);
            // Fetched first where only the list has them.
            if (const auto found = EntryOf(id); found && found->first.names && folder.NeedsNames(found->first) &&
                !ReaderApp::Names::For(id.toUpper(), existing_packs) && !queued_files.contains(found->first.names->file)) {
                queued_files.insert(found->first.names->file);
                const Scripts::Entry entry = found->first;
                queue.push_back({found->second.resolved(QUrl(entry.names->file)), [this, entry](const std::optional<QByteArray>& bytes, const QString& error) {
                                     QString why = error;
                                     Scripts::Folder here(ReaderUi::ReaderBridge::UserProfileDirectory());
                                     if (!bytes || !here.InstallNames(entry, *bytes, &why)) {
                                         problems.append(QStringLiteral("Names for %1: %2").arg(entry.title, why));
                                     }
                                 }});
            }
        }
        Begin(QStringLiteral("Installing names"), static_cast<int>(wanted.size()));
        const QString user_dir = ReaderUi::ReaderBridge::UserProfileDirectory();
        const QString bundled_dir = QString::fromLatin1(ReaderApp::Names::BundledFolder);
        // After any downloads finish, import the names packs off the UI thread so large sets don't freeze the window.
        names_pending = [this, wanted, user_dir, bundled_dir] {
            pool.start([this, wanted, user_dir, bundled_dir] {
                const QList<ReaderApp::Names::Pack> packs = ReaderApp::Names::Find({user_dir, bundled_dir});
                QSet<QString> done;
                QStringList errors;
                int put = 0;
                auto& database = ReaderUi::ReaderSession::Instance().Dictionaries();
                for (const QString& id : wanted) {
                    if (cancelled.load()) {
                        break;
                    }
                    const auto pack = ReaderApp::Names::For(id.toUpper(), packs);
                    if (!pack || done.contains(pack->path)) {
                        continue;
                    }
                    done.insert(pack->path);
                    QString error;
                    try {
                        put += ReaderApp::Names::Ensure(database, *pack, &error) ? 1 : 0;
                    } catch (const std::exception& failure) {
                        error = QString::fromUtf8(failure.what());
                    }
                    if (!error.isEmpty()) {
                        errors.append(QStringLiteral("%1: %2").arg(pack->dictionary, error));
                    }
                    const int count = static_cast<int>(done.size());
                    QMetaObject::invokeMethod(&context, [this, count] {
                        work.done = count;
                        Changed();
                    }, Qt::QueuedConnection);
                }
                if (put > 0) {
                    try {
                        database.Compact();
                    } catch (const std::exception& failure) {
                        LOG_WARNING(Frontend, "[Reader] Dictionary compact after names import failed: {}", failure.what());
                    }
                }
                QMetaObject::invokeMethod(&context, [this, put, errors] {
                    problems += errors;
                    QStringList parts{put == 1 ? QStringLiteral("1 names dictionary installed")
                                               : QStringLiteral("%1 names dictionaries installed").arg(put)};
                    if (!problems.isEmpty()) {
                        parts.append(problems.size() == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(problems.size()));
                    }
                    if (cancelled.load()) {
                        parts.append(QStringLiteral("cancelled"));
                    }
                    last = parts.join(QStringLiteral(" · "));
                    names_installed.reset();
                    if (put > 0 && dictionaries_changed) {
                        dictionaries_changed();
                    }
                    Finish();
                }, Qt::QueuedConnection);
            });
        };
        Next();
    }

    void Remove(const QStringList& ids) override {
        if (Refused()) {
            return;
        }
        const AnyGameId any;
        Scripts::Folder folder(ReaderUi::ReaderBridge::UserProfileDirectory());
        int removed = 0;
        for (const QString& id : ids) {
            removed += folder.Remove(id) ? 1 : 0;
        }
        ReaderUi::Games::Forget();
        last = removed == 1 ? QStringLiteral("Removed 1 script") : QStringLiteral("Removed %1 scripts").arg(removed);
        problems.clear();
        stale = true;
        if (removed > 0 && scripts_changed) {
            scripts_changed();
        }
        Say(last);
        Changed();
    }

    void InstallFiles(const QStringList& paths) override {
        if (paths.isEmpty() || Refused()) {
            return;
        }
        const AnyGameId any;
        Scripts::Folder folder(ReaderUi::ReaderBridge::UserProfileDirectory());
        QString error;
        const QStringList ids = folder.InstallFiles(paths, &error);
        ReaderUi::Games::Forget();
        problems = error.isEmpty() ? QStringList() : QStringList{error};
        last = ids.size() == 1 ? QStringLiteral("Installed 1 custom script") : QStringLiteral("Installed %1 custom scripts").arg(ids.size());
        if (!problems.isEmpty()) {
            last += QStringLiteral(" · some files couldn't be installed");
        }
        stale = true;
        if (!ids.isEmpty() && scripts_changed) {
            scripts_changed();
        }
        Say(last);
        Changed();
    }

    void Cancel() override {
        cancelled = true;
        names_pending = nullptr;
        queue.clear();
        if (reply) {
            reply->abort();
        }
    }

private:
    using Got = std::function<void(const std::optional<QByteArray>&, const QString&)>;
    struct Job {
        QUrl url; ///< Empty: no file, the step alone
        Got then;
    };
    struct List {
        Scripts::Index index;
        QUrl url;
    };

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
        if (!work.busy) {
            return false;
        }
        Say(QStringLiteral("Wait for the current operation to finish, or cancel it first"));
        return true;
    }

    QString NameOf(const QString& key) const {
        for (const ListConsole& console : ListConsoles) {
            if (key == QLatin1String(console.key)) {
                return QString::fromLatin1(console.name);
            }
        }
        return key;
    }

    void Begin(const QString& doing, int total) {
        cancelled = false;
        names_pending = nullptr;
        work = {true, doing, 0, total};
        finally = nullptr;
        Changed();
    }

    void Step() {
        ++work.done;
        Changed();
    }

    void Finally(std::function<void()> then) {
        finally = std::move(then);
    }

    void Finish() {
        work = {};
        stale = true;
        if (const auto then = std::exchange(finally, nullptr)) {
            then();
        }
        // What went wrong, in the log too, whole: the page shows it, but not once it is put away.
        for (const QString& problem : problems) {
            LOG_WARNING(Frontend, "[Reader] Game scripts: {}", problem.toStdString());
        }
        if (!last.isEmpty()) {
            Say(last);
        }
        Changed();
    }

    /// The next job: its file fetched (in the background), then what it does with it.
    void Next() {
        const AnyGameId any;
        while (!queue.empty()) {
            Job job = std::move(queue.front());
            queue.pop_front();
            if (job.url.isEmpty()) {
                job.then(std::nullopt, {});
                continue;
            }
            if (job.url.isLocalFile()) {
                const auto bytes = ReadFile(job.url.toLocalFile());
                job.then(bytes, bytes ? QString() : QStringLiteral("Couldn't read %1").arg(job.url.toLocalFile()));
                continue;
            }
            QNetworkRequest request(job.url);
            request.setTransferTimeout(30000);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            reply = net->get(request);
            QObject::connect(reply, &QNetworkReply::finished, &context, [this, then = std::move(job.then)] {
                QNetworkReply* done = reply;
                reply = nullptr;
                if (done == nullptr) {
                    return;
                }
                const AnyGameId any;
                if (done->error() == QNetworkReply::NoError) {
                    then(done->readAll(), {});
                } else if (done->error() != QNetworkReply::OperationCanceledError) {
                    then(std::nullopt, QStringLiteral("Couldn't fetch %1 (%2)").arg(done->url().fileName(), done->errorString()));
                }
                done->deleteLater();
                Next();
            });
            return;
        }
        if (const auto run = std::exchange(names_pending, nullptr)) {
            if (!cancelled.load()) {
                run();
                return;
            }
        }
        Finish();
    }

    std::optional<std::pair<Scripts::Entry, QUrl>> EntryOf(const QString& id) const {
        for (auto list = lists.cbegin(); list != lists.cend(); ++list) {
            for (const Scripts::Entry& entry : list->index.entries) {
                if (entry.id.compare(id, Qt::CaseInsensitive) == 0) {
                    return std::make_pair(entry, list->url);
                }
            }
        }
        return std::nullopt;
    }

    static QList<ReaderApp::Names::Pack> Packs() {
        return ReaderApp::Names::Find({ReaderUi::ReaderBridge::UserProfileDirectory(), QString::fromLatin1(ReaderApp::Names::BundledFolder)});
    }

    /// The names dictionaries in the dictionaries now, by the games they are for.
    QSet<QString> NamesInstalled() {
        if (!names_installed) {
            QSet<QString> ids;
            try {
                for (const ReaderApp::DictionaryInfo& info : ReaderUi::ReaderSession::Instance().Dictionaries().Dictionaries()) {
                    for (const QString& id : info.scope) {
                        ids.insert(id.toUpper());
                    }
                }
            } catch (const std::exception&) {
            }
            names_installed = ids;
        }
        return *names_installed;
    }

    void Build() {
        const AnyGameId any;
        stale = false;
        names_installed.reset();
        Scripts::Folder folder(ReaderUi::ReaderBridge::UserProfileDirectory());
        const QList<ReaderApp::Names::Pack> packs = Packs();
        const QSet<QString> names_in = NamesInstalled();
        QHash<QString, ScriptGame> by_id;
        const auto names_of = [&](const QString& id, bool listed) {
            if (names_in.contains(id.toUpper())) {
                return ScriptGame::Names::Installed;
            }
            if (ReaderApp::Names::For(id.toUpper(), packs)) {
                return ScriptGame::Names::Ready;
            }
            return listed ? ScriptGame::Names::Available : ScriptGame::Names::None;
        };
        // The lists' games.
        for (auto list = lists.cbegin(); list != lists.cend(); ++list) {
            for (const Scripts::Entry& entry : list->index.entries) {
                ScriptGame game;
                game.id = entry.id;
                game.title = entry.title;
                game.console = list.key();
                game.in_list = true;
                game.reads = entry.reads;
                const auto user = folder.Script(entry.id);
                const auto bundled = ReadFile(Bundled + u'/' + entry.id + QStringLiteral(".lua"));
                switch (Scripts::StateOf(entry, user, bundled, folder.Recorded(entry.id))) {
                case Scripts::State::NotInstalled: game.script = ScriptGame::Script::NotInstalled; break;
                case Scripts::State::Installed: game.script = user ? ScriptGame::Script::Installed : ScriptGame::Script::BuiltIn; break;
                case Scripts::State::Update: game.script = ScriptGame::Script::Update; break;
                case Scripts::State::Own: game.script = ScriptGame::Script::Own; break;
                case Scripts::State::TooNew: game.script = ScriptGame::Script::TooNew; break;
                }
                game.names = names_of(entry.id, entry.names.has_value());
                by_id.insert(entry.id.toUpper(), game);
            }
        }
        // Scripts of the user's and the program's own that no list has.
        const auto take = [&](const QString& where, bool own) {
            for (const QString& name : QDir(where).entryList({QStringLiteral("*.lua")}, QDir::Files)) {
                const auto id = ReaderApp::Catalog::TitleIdOf(name);
                if (!id || by_id.contains(id->toUpper())) {
                    continue;
                }
                const auto head = ReadFile(where + u'/' + name);
                const ReaderApp::ProfileInfo info = ReaderApp::Catalog::Describe(*id, head ? QString::fromUtf8(head->left(4096)) : QString(), own);
                ScriptGame game;
                game.id = *id;
                game.title = info.title;
                game.console = ConsoleOfId(*id);
                game.reads = info.reads;
                game.script = own ? ScriptGame::Script::Own : ScriptGame::Script::BuiltIn;
                game.names = names_of(*id, false);
                by_id.insert(id->toUpper(), game);
            }
        };
        take(ReaderUi::ReaderBridge::UserProfileDirectory(), true);
        take(Bundled, false);
        // The library's, with or without a script.
        if (library) {
            for (const ScriptLibraryGame& own : library()) {
                const QString key = own.id.toUpper();
                if (own.id.isEmpty()) {
                    continue;
                }
                auto found = by_id.find(key);
                if (found == by_id.end()) {
                    ScriptGame game;
                    game.id = own.id;
                    game.title = own.title;
                    game.console = own.console.isEmpty() ? ConsoleOfId(own.id) : own.console;
                    game.names = names_of(own.id, false);
                    found = by_id.insert(key, game);
                }
                found->in_library = true;
                if (found->console.isEmpty()) {
                    found->console = own.console;
                }
            }
        }
        // By console in the page's order, then by title.
        QHash<QString, int> order;
        for (int i = 0; i < static_cast<int>(std::size(ListConsoles)); ++i) {
            order.insert(QString::fromLatin1(ListConsoles[i].key), i);
        }
        games = by_id.values();
        std::sort(games.begin(), games.end(), [&order](const ScriptGame& a, const ScriptGame& b) {
            const int ca = order.value(a.console, 99), cb = order.value(b.console, 99);
            if (ca != cb) {
                return ca < cb;
            }
            return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        });
    }

    QObject context;
    QThreadPool pool;
    std::atomic<bool> cancelled{false};
    QNetworkAccessManager* net;
    QPointer<QNetworkReply> reply;
    std::deque<Job> queue;
    std::function<void()> names_pending;
    std::function<void()> finally;
    QHash<QString, List> lists;
    QList<ScriptGame> games;
    std::optional<QSet<QString>> names_installed;
    ScriptWork work;
    QStringList problems;
    QString last;
    bool stale{true};
    bool fetched{};
};

} // namespace

std::unique_ptr<ScriptShelf> MakeReaderScriptShelf() {
    return std::make_unique<ReaderScriptShelf>();
}

} // namespace Home
