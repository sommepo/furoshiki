// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include "popup_style.h"

namespace Home::PopupStyle {

namespace {

/// The reader's keys and the program's.
struct Pair {
    const char* reader;
    const char* program;
};
constexpr Pair Keys[] = {
    {"reader_popup_theme", "popup.theme"},
    {"reader_popup_mark_unknown", "popup.mark_unknown"},
    {"reader_popup_lookups", "popup.lookups"},
    {"reader_popup_css", "popup.css"},
};

QString ProgramKey(const QString& reader_key) {
    for (const Pair& pair : Keys) {
        if (reader_key == QLatin1String(pair.reader)) {
            return QString::fromLatin1(pair.program);
        }
    }
    return {};
}

QString CssOf(const QString& folder, const QString& name) {
    if (name.isEmpty() || name == QStringLiteral("none")) {
        return {};
    }
    QFile file(folder + QLatin1Char('/') + name + QStringLiteral(".css"));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

PlaySetting SettingOf(const QString& folder, const QString& key) {
    for (const PlaySetting& setting : Settings(folder)) {
        if (setting.key == key) {
            return setting;
        }
    }
    return {};
}

/// An example the folder starts with, so that the user sees where the files go and what a rule
/// looks like (shared/popup/README.md has the classes).
constexpr char Example[] = R"(/* A popup style of your own: choose it as "Your own CSS" on Furoshiki's CONSOLES page,
   for every console, one console or one game. It comes after the theme. The classes are in
   shared/popup/README.md of the readers' repository; a few to start from: */

/* .popup { border-radius: 12px; } */
/* .entry.unknown .headword { background: rgba(255, 200, 0, 0.25); } */
/* .popup[data-console="ps2"] .tag { box-shadow: 0 0 8px #f8c; } */
)";

} // namespace

QString Folder(const QString& program_folder) {
    const QString folder = program_folder + QStringLiteral("/popup-styles");
    if (!QFileInfo::exists(folder)) {
        QDir().mkpath(folder);
        QFile example(folder + QStringLiteral("/example.css"));
        if (example.open(QIODevice::WriteOnly)) {
            example.write(Example);
        }
    }
    return folder;
}

QList<PlaySetting> Settings(const QString& folder) {
    PlaySetting theme;
    theme.key = QStringLiteral("popup.theme");
    theme.label = QStringLiteral("Popup's look");
    theme.kind = PlaySetting::Choice;
    theme.default_value = QStringLiteral("console");
    theme.choices = {QStringLiteral("console"), QStringLiteral("furoshiki"), QStringLiteral("yomitan-dark"),
                     QStringLiteral("paper"), QStringLiteral("paper-dark"), QStringLiteral("classic")};
    PlaySetting mark;
    mark.key = QStringLiteral("popup.mark_unknown");
    mark.label = QStringLiteral("Mark words not in Anki");
    mark.kind = PlaySetting::Switch;
    mark.default_value = QStringLiteral("1");
    PlaySetting lookups;
    lookups.key = QStringLiteral("popup.lookups");
    lookups.label = QStringLiteral("Times looked up");
    lookups.kind = PlaySetting::Choice;
    lookups.default_value = QStringLiteral("count");
    lookups.choices = {QStringLiteral("count"), QStringLiteral("where"), QStringLiteral("off")};
    PlaySetting css;
    css.key = QStringLiteral("popup.css");
    css.label = QStringLiteral("Your own CSS");
    css.kind = PlaySetting::Choice;
    css.default_value = QStringLiteral("none");
    css.choices = {QStringLiteral("none")};
    const QFileInfoList files =
        QDir(folder).entryInfoList({QStringLiteral("*.css")}, QDir::Files, QDir::Name);
    for (const QFileInfo& file : files) {
        css.choices.append(file.completeBaseName());
    }
    return {theme, mark, lookups, css};
}

std::optional<QString> Layered(const PlaySettings& play, const QString& folder, const QString& reader_key,
                               const QString& console, const QString& game) {
    const QString key = ProgramKey(reader_key);
    if (key.isEmpty()) {
        return std::nullopt;
    }
    const PlaySettings::Resolved resolved = play.Value(SettingOf(folder, key), console, game);
    if (resolved.from == PlaySettings::Layer::Default) {
        return std::nullopt; // no layer has it: the reader's own setting counts
    }
    if (key == QStringLiteral("popup.css")) {
        return CssOf(folder, resolved.value);
    }
    if (key == QStringLiteral("popup.mark_unknown")) {
        return resolved.value == QStringLiteral("1") ? QStringLiteral("true") : QStringLiteral("false");
    }
    return resolved.value;
}

QJsonObject Export(const PlaySettings& play, const QString& folder, const QString& console, const QString& game,
                   const std::function<QString(const QString&)>& fallback) {
    const auto value = [&](const char* reader_key) {
        const QString key = QString::fromLatin1(reader_key);
        if (const auto layered = Layered(play, folder, key, console, game)) {
            return *layered;
        }
        return fallback ? fallback(key) : QString();
    };
    const QString mark = value("reader_popup_mark_unknown");
    return QJsonObject{{QStringLiteral("format"), 1},
                       {QStringLiteral("kind"), QStringLiteral("reader-popup-style")},
                       {QStringLiteral("theme"), value("reader_popup_theme")},
                       {QStringLiteral("markUnknown"), mark != QStringLiteral("false") && mark != QStringLiteral("0")},
                       {QStringLiteral("lookups"), value("reader_popup_lookups")},
                       {QStringLiteral("css"), value("reader_popup_css")}};
}

QString Import(const QJsonObject& style, PlaySettings& play, const QString& folder, const QString& name,
               const QString& console, const QString& game) {
    if (style.value(QStringLiteral("kind")).toString() != QStringLiteral("reader-popup-style") ||
        style.value(QStringLiteral("format")).toInt() != 1) {
        return QStringLiteral("not a popup style file");
    }
    const QString theme = style.value(QStringLiteral("theme")).toString();
    if (!theme.isEmpty() && SettingOf(folder, QStringLiteral("popup.theme")).choices.contains(theme)) {
        play.Set(QStringLiteral("popup.theme"), theme, console, game);
    }
    if (style.contains(QStringLiteral("markUnknown"))) {
        play.Set(QStringLiteral("popup.mark_unknown"),
                 style.value(QStringLiteral("markUnknown")).toBool() ? QStringLiteral("1") : QStringLiteral("0"), console,
                 game);
    }
    const QString lookups = style.value(QStringLiteral("lookups")).toString();
    if (SettingOf(folder, QStringLiteral("popup.lookups")).choices.contains(lookups)) {
        play.Set(QStringLiteral("popup.lookups"), lookups, console, game);
    }
    const QString css = style.value(QStringLiteral("css")).toString();
    if (css.trimmed().isEmpty()) {
        play.Set(QStringLiteral("popup.css"), QStringLiteral("none"), console, game);
    } else {
        QString file_name = name;
        file_name.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("-"));
        if (file_name.isEmpty() || file_name == QStringLiteral("none")) {
            file_name = QStringLiteral("imported");
        }
        QSaveFile file(folder + QLatin1Char('/') + file_name + QStringLiteral(".css"));
        if (!file.open(QIODevice::WriteOnly) || file.write(css.toUtf8()) < 0 || !file.commit()) {
            return QStringLiteral("its CSS could not be written to %1").arg(folder);
        }
        play.Set(QStringLiteral("popup.css"), file_name, console, game);
    }
    return play.Save() ? QString() : QStringLiteral("the settings could not be written to %1").arg(play.File());
}

} // namespace Home::PopupStyle
