// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "art_fetch.h"

#include <functional>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

namespace Home::Art {

namespace {
const QByteArray Agent = "Furoshiki";
const QString VndbApi = QStringLiteral("https://api.vndb.org/kana/");

QString Read(const QString& file) {
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
}

/// The app ID of a Steam library's game whose install folder is `folder`.
QString AppOfFolder(const QString& steamapps, const QString& folder) {
    static const QRegularExpression installdir(QStringLiteral("\"installdir\"\\s+\"([^\"]+)\"")), appid(QStringLiteral("\"appid\"\\s+\"(\\d+)\""));
    for (const QString& manifest : QDir(steamapps).entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files)) {
        const QString text = Read(steamapps + QLatin1Char('/') + manifest);
        if (installdir.match(text).captured(1).compare(folder, Qt::CaseInsensitive) == 0) {
            return appid.match(text).captured(1);
        }
    }
    return {};
}

/// The Steam libraries of this computer: Steam's own folder and those its libraryfolders.vdf names.
QStringList SteamLibraries(const QString& steam_root) {
    QString root = steam_root;
#ifdef _WIN32
    if (root.isEmpty()) {
        root = QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat).value(QStringLiteral("SteamPath")).toString();
    }
    if (root.isEmpty()) {
        root = QStringLiteral("C:/Program Files (x86)/Steam");
    }
#else
    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/.local/share/Steam");
    }
#endif
    QStringList libraries{QDir::fromNativeSeparators(root)};
    static const QRegularExpression path(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
    for (auto each = path.globalMatch(Read(root + QStringLiteral("/steamapps/libraryfolders.vdf"))); each.hasNext();) {
        const QString library = QDir::fromNativeSeparators(each.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("\\")));
        if (!libraries.contains(library, Qt::CaseInsensitive)) {
            libraries.append(library);
        }
    }
    return libraries;
}

QNetworkRequest Request(const QUrl& url) {
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", Agent);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QNetworkRequest Request(const QString& url) {
    return Request(QUrl(url));
}

QString SafeGameId(const QString& game_id) {
    QString safe = game_id.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    while (safe.contains(QStringLiteral(".."))) {
        safe.replace(QStringLiteral(".."), QStringLiteral("_"));
    }
    return safe;
}

QString PlainErrorBody(const QByteArray& body) {
    QString text = QString::fromUtf8(body.left(512));
    static const QRegularExpression tags(QStringLiteral("<[^>]+>"));
    text.remove(tags);
    text = text.simplified();
    return text.left(120);
}
} // namespace

QString CoverFile(const QString& shared, const QString& game_id) {
    const QString safe = SafeGameId(game_id);
    if (safe.isEmpty()) {
        return {};
    }
    return shared + QStringLiteral("/program/art/%1.cover.jpg").arg(safe);
}

QString SpineFile(const QString& shared, const QString& game_id) {
    const QString safe = SafeGameId(game_id);
    if (safe.isEmpty()) {
        return {};
    }
    return shared + QStringLiteral("/program/art/%1.spine.jpg").arg(safe);
}

QString VndbPlatform(const QString& system) {
    static const QList<QPair<QString, QString>> known{
        {QStringLiteral("PC"), QStringLiteral("win")},
        {QStringLiteral("Nintendo Switch"), QStringLiteral("swi")},
        {QStringLiteral("Nintendo 3DS"), QStringLiteral("n3d")},
        {QStringLiteral("PlayStation"), QStringLiteral("ps1")},
        {QStringLiteral("PlayStation 2"), QStringLiteral("ps2")},
        {QStringLiteral("PlayStation 3"), QStringLiteral("ps3")},
        {QStringLiteral("PlayStation Portable"), QStringLiteral("psp")},
        {QStringLiteral("PlayStation Vita"), QStringLiteral("psv")},
        {QStringLiteral("Dreamcast"), QStringLiteral("drc")},
        {QStringLiteral("Nintendo DS"), QStringLiteral("nds")},
    };
    for (const auto& [name, code] : known) {
        if (name == system) {
            return code;
        }
    }
    return {};
}

QString PlainName(const QString& text) {
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_KC).toCaseFolded()) {
        if (c.isLetterOrNumber()) {
            out.append(c);
        }
    }
    return out;
}

QString SteamAppId(const QString& exe_path, const QString& exe_name, const QString& steam_root) {
    // From its path: .../steamapps/common/<its folder>/...; or a steam_appid.txt beside it.
    const QStringList parts = QDir::fromNativeSeparators(exe_path).split(QLatin1Char('/'));
    for (int i = 0; i + 2 < parts.size(); ++i) {
        if (parts[i].compare(QStringLiteral("steamapps"), Qt::CaseInsensitive) == 0 && parts[i + 1].compare(QStringLiteral("common"), Qt::CaseInsensitive) == 0) {
            const QString found = AppOfFolder(parts.mid(0, i + 1).join(QLatin1Char('/')), parts[i + 2]);
            if (!found.isEmpty()) {
                return found;
            }
        }
    }
    if (!exe_path.isEmpty()) {
        const QString beside = Read(QFileInfo(exe_path).absolutePath() + QStringLiteral("/steam_appid.txt")).trimmed();
        if (QRegularExpression(QStringLiteral("^\\d+$")).match(beside).hasMatch()) {
            return beside;
        }
    }
    // Else every game of every Steam library here, for one whose folder holds a file of that name.
    if (exe_name.isEmpty()) {
        return {};
    }
    for (const QString& library : SteamLibraries(steam_root)) {
        const QString common = library + QStringLiteral("/steamapps/common");
        for (const QString& folder : QDir(common).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDirIterator files(common + QLatin1Char('/') + folder, {exe_name}, QDir::Files, QDirIterator::Subdirectories);
            if (files.hasNext()) {
                const QString found = AppOfFolder(library + QStringLiteral("/steamapps"), folder);
                if (!found.isEmpty()) {
                    return found;
                }
            }
        }
    }
    return {};
}

Sources LoadSources() {
    QSettings settings;
    Sources out;
    out.vndb = settings.value(QStringLiteral("art/vndb"), true).toBool();
    out.screenscraper = settings.value(QStringLiteral("art/screenscraper"), false).toBool();
    out.ss_user = settings.value(QStringLiteral("art/ss_user")).toString();
    out.ss_password = settings.value(QStringLiteral("art/ss_password")).toString();
    out.ss_dev_id = settings.value(QStringLiteral("art/ss_dev_id")).toString();
    out.ss_dev_password = settings.value(QStringLiteral("art/ss_dev_password")).toString();
    return out;
}

void SaveSources(const Sources& sources) {
    QSettings settings;
    settings.setValue(QStringLiteral("art/vndb"), sources.vndb);
    settings.setValue(QStringLiteral("art/screenscraper"), sources.screenscraper);
    settings.setValue(QStringLiteral("art/ss_user"), sources.ss_user);
    settings.setValue(QStringLiteral("art/ss_password"), sources.ss_password);
    settings.setValue(QStringLiteral("art/ss_dev_id"), sources.ss_dev_id);
    settings.setValue(QStringLiteral("art/ss_dev_password"), sources.ss_dev_password);
}

namespace {

/// ScreenScraper's numbers of the consoles (tools/titles.py, SCREENSCRAPER_SYSTEMS), by VNDB's names.
int ScreenScraperSystem(const QString& platform) {
    static const QHash<QString, int> systems{{QStringLiteral("psp"), 61}, {QStringLiteral("ps1"), 57}, {QStringLiteral("ps2"), 58},
                                             {QStringLiteral("psv"), 62}, {QStringLiteral("drc"), 23}, {QStringLiteral("n3d"), 17},
                                             {QStringLiteral("swi"), 225}, {QStringLiteral("nds"), 15}};
    return systems.value(platform, 0);
}

QString PlainSerial(const QString& text) {
    QString out;
    for (const QChar c : text.toUpper()) {
        if ((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))) {
            out.append(c);
        }
    }
    return out;
}

} // namespace

Finder::Finder(QObject* parent) : QObject(parent), net(new QNetworkAccessManager(this)) {}

bool Finder::Start(const Want& want_, const QString& shared_) {
    if (busy) {
        return false;
    }
    want = want_;
    shared = shared_;
    busy = true;
    got_cover = got_spine = false;
    cover_from.clear();
    spine_from.clear();
    problem.clear();
    sources = LoadSources();
    QDir().mkpath(shared + QStringLiteral("/program/art"));
    if (want.system == QStringLiteral("PC")) {
        Steam();
    } else {
        ScreenScraper();
    }
    return true;
}

void Finder::ScreenScraper() {
    // The user's account and developer ID, where they turned it on: the game of this console by
    // its serial, else by its name exactly (tools/titles.py's rules), its Japanese box first.
    const int system = ScreenScraperSystem(VndbPlatform(want.system));
    if (!sources.ScreenScraperReady() || system == 0) {
        AfterScreenScraper();
        return;
    }
    if (want.system == QStringLiteral("Nintendo Switch") || want.game_id.isEmpty() || want.game_id.startsWith(QStringLiteral("LINES-"))) {
        ScreenScraperSearch({want.title, want.english}); // its roms carry no title IDs there: by name
        return;
    }
    const QList<QPair<QString, QString>> params{
        {QStringLiteral("systemeid"), QString::number(system)},
        {QStringLiteral("romtype"), QStringLiteral("iso")},
        {QStringLiteral("romnom"), want.game_id + QStringLiteral(".iso")},
        {QStringLiteral("serialnum"), want.game_id},
    };
    QNetworkReply* reply = net->get(Request(ScreenScraperUrl(QStringLiteral("jeuInfos.php"), params)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, system] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QJsonObject game = QJsonDocument::fromJson(body).object().value(QStringLiteral("response")).toObject().value(QStringLiteral("jeu")).toObject();
        if (game.isEmpty() && !body.trimmed().startsWith('{') && !body.isEmpty() && reply->error() != QNetworkReply::ContentNotFoundError) {
            const QString clean = PlainErrorBody(body);
            if (!clean.isEmpty()) {
                problem = QStringLiteral("ScreenScraper: %1").arg(clean);
            }
        }
        // Its own serial among the game's roms', and of this console: never another game.
        bool same = game.value(QStringLiteral("systeme")).toObject().value(QStringLiteral("id")).toVariant().toInt() == system;
        bool serial = false;
        const QString wanted = PlainSerial(want.game_id);
        QJsonArray roms = game.value(QStringLiteral("roms")).toArray();
        if (game.value(QStringLiteral("rom")).isObject()) {
            roms.append(game.value(QStringLiteral("rom")));
        }
        for (const QJsonValue& rom : roms) {
            const QJsonObject r = rom.toObject();
            serial = serial || PlainSerial(r.value(QStringLiteral("romserial")).toString()) == wanted;
            for (const QJsonValue& s : r.value(QStringLiteral("serials")).toArray()) {
                serial = serial || PlainSerial(s.toString()) == wanted;
            }
        }
        if (same && serial) {
            ScreenScraperGame(game);
        } else if (problem.isEmpty()) {
            ScreenScraperSearch({want.title, want.english});
        } else {
            AfterScreenScraper();
        }
    });
}

void Finder::ScreenScraperSearch(const QStringList& names) {
    QStringList left;
    for (const QString& name : names) {
        if (!name.trimmed().isEmpty()) {
            left.append(name.trimmed());
        }
    }
    const int system = ScreenScraperSystem(VndbPlatform(want.system));
    if (left.isEmpty()) {
        AfterScreenScraper();
        return;
    }
    const QString search = left.takeFirst();
    const QList<QPair<QString, QString>> params{
        {QStringLiteral("systemeid"), QString::number(system)},
        {QStringLiteral("recherche"), search},
    };
    QNetworkReply* reply = net->get(Request(ScreenScraperUrl(QStringLiteral("jeuRecherche.php"), params)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, left, system] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (!body.trimmed().startsWith('{') && !body.isEmpty() && reply->error() != QNetworkReply::ContentNotFoundError) {
            const QString clean = PlainErrorBody(body);
            if (!clean.isEmpty() && problem.isEmpty()) {
                problem = QStringLiteral("ScreenScraper: %1").arg(clean);
            }
            AfterScreenScraper();
            return;
        }
        const QSet<QString> wanted = QSet<QString>{PlainName(want.title), PlainName(want.english)} - QSet<QString>{QString()};
        for (const QJsonValue& value : QJsonDocument::fromJson(body).object().value(QStringLiteral("response")).toObject().value(QStringLiteral("jeux")).toArray()) {
            const QJsonObject game = value.toObject();
            if (game.value(QStringLiteral("systeme")).toObject().value(QStringLiteral("id")).toVariant().toInt() != system) {
                continue;
            }
            for (const QJsonValue& name : game.value(QStringLiteral("noms")).toArray()) {
                const QString plain = PlainName(name.toObject().value(QStringLiteral("text")).toString());
                if (!plain.isEmpty() && wanted.contains(plain)) {
                    ScreenScraperGame(game);
                    return;
                }
            }
        }
        ScreenScraperSearch(left);
    });
}

void Finder::ScreenScraperGame(const QJsonObject& game) {
    // Its box front and spine, the Japanese ones before any other region's.
    static const QStringList regions{QStringLiteral("jp"), QStringLiteral("wor"), QStringLiteral("ss"), QStringLiteral("us"), QStringLiteral("eu")};
    const auto best = [&game](const QString& kind) {
        QString url;
        int rank = 99;
        for (const QJsonValue& value : game.value(QStringLiteral("medias")).toArray()) {
            const QJsonObject media = value.toObject();
            if (media.value(QStringLiteral("type")).toString() != kind || media.value(QStringLiteral("url")).toString().isEmpty()) {
                continue;
            }
            const int at = static_cast<int>(regions.indexOf(media.value(QStringLiteral("region")).toString()));
            const int r = at < 0 ? 50 : at;
            if (r < rank) {
                rank = r;
                url = media.value(QStringLiteral("url")).toString();
            }
        }
        return url;
    };
    const QString front = best(QStringLiteral("box-2D")), side = best(QStringLiteral("box-2D-side"));
    const auto spine = [this, side] {
        if (side.isEmpty() || got_spine) {
            AfterScreenScraper();
            return;
        }
        Save(side, SpineFile(shared, want.game_id), true, [this](bool ok) {
            if (ok) {
                got_spine = true;
                spine_from = QStringLiteral("ScreenScraper");
            }
            AfterScreenScraper();
        });
    };
    if (front.isEmpty() || got_cover) {
        spine();
        return;
    }
    Save(front, CoverFile(shared, want.game_id), false, [this, spine](bool ok) {
        if (ok) {
            got_cover = true;
            cover_from = QStringLiteral("ScreenScraper");
        }
        spine();
    });
}

QUrl Finder::ScreenScraperUrl(const QString& page, const QList<QPair<QString, QString>>& params) const {
    QList<QPair<QString, QString>> all = params;
    all.append({QStringLiteral("devid"), sources.ss_dev_id});
    all.append({QStringLiteral("devpassword"), sources.ss_dev_password});
    all.append({QStringLiteral("softname"), QStringLiteral("Furoshiki")});
    all.append({QStringLiteral("output"), QStringLiteral("json")});
    all.append({QStringLiteral("ssid"), sources.ss_user});
    all.append({QStringLiteral("sspassword"), sources.ss_password});
    QByteArray encoded;
    for (const auto& [key, val] : all) {
        if (!encoded.isEmpty()) {
            encoded += '&';
        }
        encoded += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(val);
    }
    QUrl url(QStringLiteral("https://api.screenscraper.fr/api2/") + page);
    url.setQuery(QString::fromLatin1(encoded), QUrl::StrictMode);
    return url;
}

void Finder::AfterScreenScraper() {
    // VNDB for what is still missing, where the user has it on.
    if (sources.vndb && !(got_cover && got_spine)) {
        Vndb();
    } else {
        Finish();
    }
}

void Finder::Steam() {
    const QString app = SteamAppId(want.exe_path, want.exe_name);
    if (app.isEmpty()) {
        ScreenScraper();
        return;
    }
    // Steam's portrait art, as its library shows a game: a box's front, near enough.
    Save(QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900_2x.jpg").arg(app), CoverFile(shared, want.game_id), false, [this, app](bool ok) {
        if (ok) {
            got_cover = true;
            cover_from = QStringLiteral("Steam");
            ScreenScraper(); // a spine, perhaps
            return;
        }
        Save(QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900.jpg").arg(app), CoverFile(shared, want.game_id), false, [this](bool small) {
            if (small) {
                got_cover = true;
                cover_from = QStringLiteral("Steam");
            }
            ScreenScraper();
        });
    });
}

void Finder::Vndb() {
    if (VndbPlatform(want.system).isEmpty()) {
        Finish();
        return;
    }
    if (!want.vndb.isEmpty()) {
        Releases(want.vndb);
        return;
    }
    // The entry whose name is exactly the title's (its own, an alias, or its English one): never
    // a looser match, which finds another game as readily.
    const QJsonObject query{{QStringLiteral("filters"), QJsonArray{QStringLiteral("search"), QStringLiteral("="), want.title}},
                            {QStringLiteral("fields"), QStringLiteral("id,title,alttitle,aliases,titles.title")},
                            {QStringLiteral("results"), 20}};
    QNetworkRequest request = Request(VndbApi + QStringLiteral("vn"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = net->post(request, QJsonDocument(query).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QSet<QString> wanted = QSet<QString>{PlainName(want.title), PlainName(want.english)} - QSet<QString>{QString()};
        QString vn;
        for (const QJsonValue& value : QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("results")).toArray()) {
            const QJsonObject entry = value.toObject();
            QStringList names{entry.value(QStringLiteral("title")).toString(), entry.value(QStringLiteral("alttitle")).toString()};
            for (const QJsonValue& alias : entry.value(QStringLiteral("aliases")).toArray()) {
                names.append(alias.toString());
            }
            for (const QJsonValue& other : entry.value(QStringLiteral("titles")).toArray()) {
                names.append(other.toObject().value(QStringLiteral("title")).toString());
            }
            for (const QString& name : names) {
                const QString plain = PlainName(name);
                if (!plain.isEmpty() && wanted.contains(plain)) {
                    vn = entry.value(QStringLiteral("id")).toString();
                }
            }
            if (!vn.isEmpty()) {
                break;
            }
        }
        if (vn.isEmpty()) {
            Finish();
        } else {
            Releases(vn);
        }
    });
}

void Finder::Releases(const QString& vn) {
    const QString platform = VndbPlatform(want.system);
    const QJsonObject query{{QStringLiteral("filters"), QJsonArray{QStringLiteral("and"), QJsonArray{QStringLiteral("vn"), QStringLiteral("="), QJsonArray{QStringLiteral("id"), QStringLiteral("="), vn}},
                                                               QJsonArray{QStringLiteral("platform"), QStringLiteral("="), platform}}},
                            {QStringLiteral("fields"), QStringLiteral("languages.lang,platforms,images{type,url,dims,sexual}")},
                            {QStringLiteral("results"), 100}};
    QNetworkRequest request = Request(VndbApi + QStringLiteral("release"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = net->post(request, QJsonDocument(query).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, platform] {
        reply->deleteLater();
        // A Japanese release's pictures first; a release of this console alone before one of several.
        QString front, side;
        int front_rank = -1, side_rank = -1;
        for (const QJsonValue& value : QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("results")).toArray()) {
            const QJsonObject release = value.toObject();
            bool japanese = false;
            for (const QJsonValue& language : release.value(QStringLiteral("languages")).toArray()) {
                japanese = japanese || language.toObject().value(QStringLiteral("lang")).toString() == QStringLiteral("ja");
            }
            const QJsonArray platforms = release.value(QStringLiteral("platforms")).toArray();
            if (!platforms.contains(platform)) {
                continue; // only this console's boxes, never another's
            }
            const int rank = (japanese ? 2 : 0) + (platforms.size() == 1 ? 1 : 0);
            for (const QJsonValue& picture : release.value(QStringLiteral("images")).toArray()) {
                const QJsonObject image = picture.toObject();
                if (image.value(QStringLiteral("sexual")).toDouble() >= 1) {
                    continue;
                }
                const QJsonArray dims = image.value(QStringLiteral("dims")).toArray();
                const QString type = image.value(QStringLiteral("type")).toString(), url = image.value(QStringLiteral("url")).toString();
                if (type == QStringLiteral("pkgfront") && rank > front_rank) {
                    front = url;
                    front_rank = rank;
                } else if (type == QStringLiteral("pkgside") && dims.size() == 2 && dims[1].toInt() >= 4 * std::max(1, dims[0].toInt()) && rank > side_rank) {
                    side = url; // a spine stands at least four times as tall as it is wide
                    side_rank = rank;
                }
            }
        }
        const auto spine = [this, side] {
            if (side.isEmpty() || got_spine) {
                Finish();
                return;
            }
            Save(side, SpineFile(shared, want.game_id), true, [this](bool ok) {
                if (ok) {
                    got_spine = true;
                    spine_from = QStringLiteral("VNDB");
                }
                Finish();
            });
        };
        if (front.isEmpty() || got_cover) {
            spine();
            return;
        }
        Save(front, CoverFile(shared, want.game_id), false, [this, spine](bool ok) {
            if (ok) {
                got_cover = true;
                cover_from = QStringLiteral("VNDB");
            }
            spine();
        });
    });
}

void Finder::Save(const QString& url, const QString& file, bool spine, std::function<void(bool)> then) {
    const QUrl parsed(url);
    if (file.isEmpty() || (parsed.scheme() != QStringLiteral("https") && parsed.scheme() != QStringLiteral("http"))) {
        then(false);
        return;
    }
    QNetworkReply* reply = net->get(Request(parsed));
    connect(reply, &QNetworkReply::finished, this, [reply, file, spine, then] {
        reply->deleteLater();
        QImage picture;
        if (reply->error() == QNetworkReply::NoError) {
            constexpr qint64 MaxImageBytes = 20ll * 1024 * 1024;
            const QByteArray bytes = reply->read(MaxImageBytes + 1);
            if (bytes.size() <= MaxImageBytes) {
                picture.loadFromData(bytes);
            }
        }
        if (picture.isNull() || picture.width() < 40) {
            then(false);
            return;
        }
        // At most as large as the built-in ones are kept: 1400 high for a cover, 1000 for a spine.
        const int most = spine ? 1000 : 1400;
        if (picture.height() > most) {
            picture = picture.scaledToHeight(most, Qt::SmoothTransformation);
        }
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || !picture.save(&out, "JPEG", 90)) {
            then(false);
            return;
        }
        then(out.commit());
    });
}

void Finder::Finish() {
    busy = false;
    const bool found = got_cover || got_spine;
    QString said;
    if (got_cover && got_spine) {
        said = cover_from == spine_from ? QStringLiteral("Box art found: cover & spine (%1)").arg(cover_from)
                                        : QStringLiteral("Box art found: cover (%1), spine (%2)").arg(cover_from, spine_from);
    } else if (got_cover) {
        said = QStringLiteral("Box art found: cover (%1)").arg(cover_from);
    } else if (got_spine) {
        said = QStringLiteral("Box art found: spine (%1)").arg(spine_from);
    } else {
        said = QStringLiteral("No box art found for %1").arg(want.title);
    }
    if (!problem.isEmpty()) {
        said += QStringLiteral(" (%1)").arg(problem);
    }
    emit Done(want.game_id, found, said);
}

} // namespace Home::Art
