// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDir>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <quazip.h>
#include <quazipfile.h>
#include "reader/desktop/names.h"

namespace ReaderApp::Names {

namespace {

std::optional<QByteArray> Entry(const QString& path, const QString& name) {
    QuaZip zip(path);
    if (!zip.open(QuaZip::mdUnzip) || !zip.setCurrentFile(name)) {
        return std::nullopt;
    }
    QuaZipFile file(&zip);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return file.readAll();
}

QJsonObject Object(const QString& path, const QString& name) {
    const auto bytes = Entry(path, name);
    return bytes ? QJsonDocument::fromJson(*bytes).object() : QJsonObject{};
}

} // namespace

std::optional<Pack> Read(const QString& path) {
    const QJsonObject index = Object(path, QStringLiteral("index.json"));
    const QJsonValue scope = index.value(QStringLiteral("furoshikiScope"));
    const QString key = index.value(QStringLiteral("furoshikiTitle")).toString();
    if (!scope.isArray() || key.isEmpty()) {
        return std::nullopt;
    }
    Pack pack{path, key, index.value(QStringLiteral("title")).toString().trimmed(),
              index.value(QStringLiteral("revision")).toString(), {}};
    for (const QJsonValue& game : scope.toArray()) {
        const QString id = game.toString().trimmed().toUpper();
        if (!id.isEmpty()) {
            pack.scope.append(id);
        }
    }
    if (pack.dictionary.isEmpty()) {
        return std::nullopt;
    }
    return pack;
}

QList<Pack> Find(const QStringList& folders) {
    QList<Pack> out;
    QSet<QString> keys;
    for (const QString& folder : folders) {
        const QDir dir(folder);
        for (const QString& name :
             dir.entryList({QStringLiteral("names-*.zip")}, QDir::Files, QDir::Name)) {
            if (auto pack = Read(dir.filePath(name)); pack && !keys.contains(pack->key)) {
                keys.insert(pack->key);
                out.append(*pack);
            }
        }
    }
    return out;
}

std::optional<Pack> For(const QString& game, const QList<Pack>& packs) {
    const QString id = game.trimmed().toUpper();
    if (id.isEmpty()) {
        return std::nullopt;
    }
    for (const Pack& pack : packs) {
        if (pack.scope.contains(id)) {
            return pack;
        }
    }
    return std::nullopt;
}

namespace {

struct Pictures {
    QMutex mutex;
    QHash<QString, QString> zips;   ///< A dictionary's title to its zip
    QHash<QString, QString> shown;  ///< "title\npath" to its data: address ("" for none)
};

Pictures& Kept() {
    static Pictures pictures;
    return pictures;
}

} // namespace

void Remember(const Pack& pack) {
    Pictures& kept = Kept();
    const QMutexLocker lock(&kept.mutex);
    if (kept.zips.value(pack.dictionary) != pack.path) {
        kept.zips.insert(pack.dictionary, pack.path);
        kept.shown.clear();
    }
}

QString Picture(const QString& dictionary, const QString& path) {
    // Only a picture of the zip's own img/ folder, by a plain name.
    static const QRegularExpression plain(QStringLiteral("^img/[A-Za-z0-9_.-]+\\.(jpg|jpeg|png|webp)$"));
    if (!plain.match(path).hasMatch()) {
        return {};
    }
    Pictures& kept = Kept();
    const QMutexLocker lock(&kept.mutex);
    const QString key = dictionary + u'\n' + path;
    if (const auto found = kept.shown.constFind(key); found != kept.shown.constEnd()) {
        return *found;
    }
    QString address;
    const QString zip = kept.zips.value(dictionary);
    if (!zip.isEmpty()) {
        if (const auto bytes = Entry(zip, path); bytes && !bytes->isEmpty() && bytes->size() < 512 * 1024) {
            const QString type = path.endsWith(QStringLiteral(".png")) ? QStringLiteral("png")
                                 : path.endsWith(QStringLiteral(".webp")) ? QStringLiteral("webp") : QStringLiteral("jpeg");
            address = QStringLiteral("data:image/%1;base64,%2").arg(type, QString::fromLatin1(bytes->toBase64()));
        }
    }
    if (kept.shown.size() > 400) {
        kept.shown.clear(); // a long evening's names: start again rather than grow
    }
    kept.shown.insert(key, address);
    return address;
}

Card CardOf(const QString& glossary_json) {
    Card card;
    const QJsonArray glossary = QJsonDocument::fromJson(glossary_json.toUtf8()).array();
    for (const QJsonValue& item : glossary) {
        const QJsonObject object = item.toObject();
        if (object.value(QStringLiteral("type")).toString() != QStringLiteral("structured-content")) {
            continue;
        }
        const QJsonObject data = object.value(QStringLiteral("content")).toObject().value(QStringLiteral("data")).toObject();
        if (data.value(QStringLiteral("furoshiki")).toString() != QStringLiteral("name")) {
            continue;
        }
        const auto text = [&data](const char* name) { return data.value(QLatin1String(name)).toString().trimmed(); };
        card.name = text("name");
        if (card.name.isEmpty()) {
            continue;
        }
        card.found = true;
        card.id = text("id");
        card.kind = text("kind");
        card.reading = text("reading");
        card.romaji = text("romaji");
        card.role = text("role");
        card.also = text("also").split(u'、', Qt::SkipEmptyParts);
        card.about = text("about");
        card.image = text("image");
        break;
    }
    return card;
}

bool Ensure(DictionaryDatabase& database, const Pack& pack, QString* error) {
    Remember(pack);
    std::optional<DictionaryInfo> installed;
    for (const DictionaryInfo& info : database.Dictionaries()) {
        if (info.title == pack.dictionary) {
            installed = info;
        }
    }
    if (installed && installed->revision == pack.revision) {
        return false;
    }
    if (installed) {
        database.Delete(installed->id);
    }
    DictionaryImporter importer{database};
    const DictionaryImporter::Result result = importer.Import(pack.path);
    if (!result.ok) {
        if (error) {
            *error = result.error;
        }
        return false;
    }
    if (installed && !installed->enabled) {
        for (const DictionaryInfo& info : database.Dictionaries()) {
            if (info.title == pack.dictionary) {
                database.SetEnabled(info.id, false);
            }
        }
    }
    return true;
}

} // namespace ReaderApp::Names
