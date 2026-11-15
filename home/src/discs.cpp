// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "discs.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace Home::Discs {

namespace {
const QRegularExpression& Pattern() {
    static const QRegularExpression pattern(QStringLiteral(R"(\s*\((?:Disc|Disk|CD)\s*(\d+)(?:\s+of\s+\d+)?\))"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}
} // namespace

int Number(const QString& name) {
    const QRegularExpressionMatch match = Pattern().match(name);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString GroupKey(const QString& path) {
    const QFileInfo file(path);
    QString name = file.fileName();
    name.remove(Pattern());
    return QDir::cleanPath(file.path() + QLatin1Char('/') + name).toLower();
}

QString WithoutDisc(const QString& title) {
    QString out = title;
    out.remove(Pattern());
    return out.trimmed();
}

QStringList Of(const QString& path) {
    const QFileInfo file(path);
    if (Number(file.fileName()) == 0) {
        return {path};
    }
    const QString key = GroupKey(path);
    const QString suffix = file.suffix().toLower();
    QList<QFileInfo> found;
    for (const QFileInfo& other : QDir(file.path()).entryInfoList(QDir::Files)) {
        if (other.suffix().toLower() == suffix && Number(other.fileName()) > 0 && GroupKey(other.filePath()) == key) {
            found.append(other);
        }
    }
    std::sort(found.begin(), found.end(),
              [](const QFileInfo& a, const QFileInfo& b) { return Number(a.fileName()) < Number(b.fileName()); });
    QStringList out;
    bool has = false;
    for (const QFileInfo& other : found) {
        out.append(other.filePath());
        has = has || QFileInfo(other.filePath()) == file;
    }
    if (!has) {
        out.prepend(path);
    }
    return out;
}

} // namespace Home::Discs
