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
#include <QSet>
#include <QSettings>

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

QNetworkRequest Request(const QString& url) {
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", Agent);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}
} // namespace

QString CoverFile(const QString& shared, const QString& game_id) {
    return shared + QStringLiteral("/program/art/%1.cover.jpg").arg(game_id);
}

QString SpineFile(const QString& shared, const QString& game_id) {
    return shared + QStringLiteral("/program/art/%1.spine.jpg").arg(game_id);
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

Finder::Finder(QObject* parent) : QObject(parent), net(new QNetworkAccessManager(this)) {}

bool Finder::Start(const Want& want_, const QString& shared_) {
    if (busy) {
        return false;
    }
    want = want_;
    shared = shared_;
    busy = true;
    got_cover = got_spine = false;
    from.clear();
    QDir().mkpath(shared + QStringLiteral("/program/art"));
    if (want.system == QStringLiteral("PC")) {
        Steam();
    } else {
        Vndb();
    }
    return true;
}

void Finder::Steam() {
    const QString app = SteamAppId(want.exe_path, want.exe_name);
    if (app.isEmpty()) {
        Vndb();
        return;
    }
    // Steam's portrait art, as its library shows a game: a box's front, near enough.
    Save(QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900_2x.jpg").arg(app), CoverFile(shared, want.game_id), false, [this, app](bool ok) {
        if (ok) {
            got_cover = true;
            from.append(QStringLiteral("STEAM"));
            Vndb(); // a spine, perhaps
            return;
        }
        Save(QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900.jpg").arg(app), CoverFile(shared, want.game_id), false, [this](bool small) {
            if (small) {
                got_cover = true;
                from.append(QStringLiteral("STEAM"));
            }
            Vndb();
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
        const QSet<QString> wanted{PlainName(want.title), PlainName(want.english)};
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
                if (!name.isEmpty() && wanted.contains(PlainName(name))) {
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
                    from.append(QStringLiteral("VNDB SPINE"));
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
                from.append(QStringLiteral("VNDB"));
            }
            spine();
        });
    });
}

void Finder::Save(const QString& url, const QString& file, bool spine, std::function<void(bool)> then) {
    QNetworkReply* reply = net->get(Request(url));
    connect(reply, &QNetworkReply::finished, this, [reply, file, spine, then] {
        reply->deleteLater();
        QImage picture;
        if (reply->error() == QNetworkReply::NoError) {
            picture.loadFromData(reply->readAll());
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
        then(picture.save(file, "JPEG", 90));
    });
}

void Finder::Finish() {
    busy = false;
    const bool found = got_cover || got_spine;
    emit Done(want.game_id, found,
              found ? QStringLiteral("Box art found: %1").arg(from.join(QStringLiteral(", ")).toLower()) : QStringLiteral("No box art found for %1; its spine is made in its console's look").arg(want.title));
}

} // namespace Home::Art
