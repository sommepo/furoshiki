// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QHash>
#include <QRegularExpression>
#include "reader/desktop/catalog.h"
#include "reader/core/console.h"

namespace ReaderApp::Catalog {

ProfileInfo Describe(const QString& title_id, const QString& source, bool own) {
    QHash<QString, QString> said;
    for (const QString& raw : source.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (!line.startsWith(QStringLiteral("--"))) {
            break; // the comments at the top have ended
        }
        const QString text = line.mid(2).trimmed();
        if (!text.startsWith(QStringLiteral("reader:"))) {
            continue;
        }
        const QString words = text.mid(7).trimmed();
        const qsizetype space = words.indexOf(u' ');
        if (space <= 0) {
            continue;
        }
        const QString key = words.left(space);
        const QString value = words.mid(space + 1).trimmed();
        if (!value.isEmpty() && !said.contains(key)) {
            said.insert(key, value);
        }
    }
    return ProfileInfo{title_id, said.value(QStringLiteral("title"), title_id),
                       said.value(QStringLiteral("reads")), said.value(QStringLiteral("version")),
                       own, said.value(QStringLiteral("audio")).toLower(), [&said] {
                           const QString how = said.value(QStringLiteral("touch")).toLower();
                           return how == QStringLiteral("game") || how == QStringLiteral("press")
                                      ? how
                                      : QString();
                       }()};
}

std::optional<QString> TitleIdOf(const QString& file_name) {
    // A game's ID as the console writes it (reader/core/console.h), as it is now: a program that
    // plays several consoles (Furoshiki) changes the console, and with it the pattern, at run
    // time. It was made once, so the first console's pattern stood for every other's after.
    thread_local QString pattern;
    thread_local QRegularExpression name;
    if (const QString now = QString::fromLatin1(Reader::ConsoleTraits::GameIdPattern); now != pattern) {
        pattern = now;
        name = QRegularExpression(QRegularExpression::anchoredPattern(QStringLiteral("(%1)\\.lua").arg(now)));
    }
    const QRegularExpressionMatch match = name.match(file_name);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1);
}

} // namespace ReaderApp::Catalog
