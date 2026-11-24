// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include "popup_style.h"
#include "popup_design.h"
#include "reader/desktop/settings.h"

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
    {"reader_popup_css", "popup.design"},
    {"reader_popup_opacity", "popup.opacity"},
    {"reader_popup_radius", "popup.radius"},
    {"reader_popup_compact", "popup.compact"},
    {"reader_popup_hide_tags", "popup.hide_tags"},
    {"reader_popup_portrait", "popup.portrait"},
    {"reader_popup_glossary", "popup.glossary"},
    {"reader_popup_pitch", "popup.pitch"},
    {"reader_popup_width", "popup.width"},
};

QString ProgramKey(const QString& reader_key) {
    for (const Pair& pair : Keys) {
        if (reader_key == QLatin1String(pair.reader)) {
            return QString::fromLatin1(pair.program);
        }
    }
    return {};
}

PlaySetting SettingOf(const QString& folder, const QString& key) {
    for (const PlaySetting& setting : Settings(folder)) {
        if (setting.key == key) {
            return setting;
        }
    }
    return {};
}

} // namespace

QJsonObject LookOf(const std::function<QString(const QString&)>& value) {
    const auto text = [&](const char* key) { return value ? value(QString::fromLatin1(key)) : QString(); };
    const QString portrait = text("reader_popup_portrait");
    const QString hide = text("reader_popup_hide_tags");
    const QString glossary = text("reader_popup_glossary");
    const QString pitch = text("reader_popup_pitch");
    bool has_width = false;
    const int width = text("reader_popup_width").toInt(&has_width);
    bool has_radius = false;
    const int radius = text("reader_popup_radius").toInt(&has_radius);
    bool has_opacity = false;
    const int opacity = text("reader_popup_opacity").toInt(&has_opacity);
    bool has_compact = false;
    const int compact = text("reader_popup_compact").toInt(&has_compact);
    // Nothing chosen: 90 % solid, the published default since 2026-11-15 (ReaderSettings::PopupOpacity).
    return QJsonObject{{QStringLiteral("opacity"), has_opacity ? std::clamp(opacity, 0, 100) : 90},
                       {QStringLiteral("radius"), has_radius ? std::clamp(radius, -1, 28) : -1},
                       {QStringLiteral("compact"), has_compact ? std::clamp(compact, 0, 9) : 0},
                       {QStringLiteral("hideTags"), hide == QStringLiteral("true") || hide == QStringLiteral("1")},
                       {QStringLiteral("portrait"), portrait == QStringLiteral("large") || portrait == QStringLiteral("off")
                                                        ? portrait : QStringLiteral("small")},
                       {QStringLiteral("glossary"), glossary == QStringLiteral("compact") ? glossary : QStringLiteral("full")},
                       {QStringLiteral("pitch"), QStringList{QStringLiteral("graph"), QStringLiteral("text"), QStringLiteral("number"),
                                                             QStringLiteral("all"), QStringLiteral("off")}.contains(pitch)
                                                     ? pitch : QStringLiteral("over")},
                       {QStringLiteral("width"), (!has_width || width <= 0) ? 0 : std::clamp(width, 280, 1000)}};
}

QString Folder(const QString& program_folder) {
    const QString folder = program_folder + QStringLiteral("/popup-styles");
    QDir().mkpath(folder);
    return folder;
}

QList<PlaySetting> Settings(const QString& folder) {
    PlaySetting theme;
    theme.key = QStringLiteral("popup.theme");
    theme.label = QStringLiteral("Popup theme");
    theme.kind = PlaySetting::Choice;
    // "auto" (since 2026-11-17): the look of the shelf's theme (ReaderSettings::PopupThemeOfShelf).
    theme.default_value = QStringLiteral("auto");
    theme.choices = {QStringLiteral("auto"), QStringLiteral("console")};
    theme.choices += ReaderApp::ReaderSettings::PopupThemes();
    theme.choices.append(QStringLiteral("classic"));
    PlaySetting mark;
    mark.key = QStringLiteral("popup.mark_unknown");
    mark.label = QStringLiteral("Highlight words not in Anki");
    mark.kind = PlaySetting::Switch;
    mark.default_value = QStringLiteral("1");
    PlaySetting lookups;
    lookups.key = QStringLiteral("popup.lookups");
    lookups.label = QStringLiteral("Lookup count");
    lookups.kind = PlaySetting::Choice;
    lookups.default_value = QStringLiteral("count");
    lookups.choices = {QStringLiteral("count"), QStringLiteral("where"), QStringLiteral("off")};
    // The design over the look (popup_design.h): "none", the look as it is, or one of the designs.
    PlaySetting design;
    design.key = QStringLiteral("popup.design");
    design.label = QStringLiteral("Custom theme");
    design.kind = PlaySetting::Choice;
    design.default_value = QStringLiteral("none");
    design.choices = {QStringLiteral("none")};
    for (const PopupDesign::Design& one : PopupDesign::InFolder(folder)) {
        design.choices.append(one.name);
    }
    // The look's own settings (shared/popup/README.md, "look"): 0 and -1 leave a look as it is.
    const auto number = [](const char* key, const char* label, double low, double high, double step, const char* otherwise) {
        PlaySetting setting;
        setting.key = QString::fromLatin1(key);
        setting.label = QString::fromLatin1(label);
        setting.kind = PlaySetting::Number;
        setting.low = low;
        setting.high = high;
        setting.step = step;
        setting.default_value = QString::fromLatin1(otherwise);
        return setting;
    };
    PlaySetting hide;
    hide.key = QStringLiteral("popup.hide_tags");
    hide.label = QStringLiteral("Hide grammar tags");
    hide.kind = PlaySetting::Switch;
    hide.default_value = QStringLiteral("0");
    PlaySetting portrait;
    portrait.key = QStringLiteral("popup.portrait");
    portrait.label = QStringLiteral("Character portraits");
    portrait.kind = PlaySetting::Choice;
    portrait.default_value = QStringLiteral("small");
    portrait.choices = {QStringLiteral("small"), QStringLiteral("large"), QStringLiteral("off")};
    // How a word's definitions are laid out: as its dictionary sets them (Yomitan's way, with the
    // dictionary's own styles and pictures), or compact (each sense's glosses on a line).
    PlaySetting glossary;
    glossary.key = QStringLiteral("popup.glossary");
    glossary.label = QStringLiteral("Definition layout");
    glossary.kind = PlaySetting::Choice;
    glossary.default_value = QStringLiteral("full");
    glossary.choices = {QStringLiteral("full"), QStringLiteral("compact")};
    PlaySetting pitch;
    pitch.key = QStringLiteral("popup.pitch");
    pitch.label = QStringLiteral("Pitch accent");
    pitch.kind = PlaySetting::Choice;
    pitch.default_value = QStringLiteral("over");
    pitch.choices = {QStringLiteral("over"), QStringLiteral("graph"), QStringLiteral("text"), QStringLiteral("number"), QStringLiteral("all"),
                     QStringLiteral("off")};
    // The design right after the look it is made over.
    return {theme,
            design,
            mark,
            lookups,
            number("popup.opacity", "Opacity", 0, 100, 5, "90"),
            number("popup.radius", "Corner radius", -1, 28, 1, "-1"),
            number("popup.width", "Maximum width", 0, 1000, 20, "0"),
            glossary,
            number("popup.compact", "Meanings before \"more\"", 0, 9, 1, "0"),
            pitch,
            hide,
            portrait};
}

std::optional<QString> Layered(const PlaySettings& play, const QString& folder, const QString& reader_key,
                               const QString& console, const QString& game) {
    const QString key = ProgramKey(reader_key);
    if (key.isEmpty()) {
        return std::nullopt;
    }
    const PlaySettings::Resolved resolved = play.Value(SettingOf(folder, key), console, game);
    if (key == QStringLiteral("popup.design")) {
        // What the page draws over the look: the design's own stylesheet where it is made over the
        // look in use there, else nothing (the user's CSS of before is no longer drawn: designs
        // took its place, 2026-11-10). Never the reader's own setting: a design is the program's.
        const QString theme = ReaderApp::ReaderSettings::ResolvePopupTheme(
            play.Value(SettingOf(folder, QStringLiteral("popup.theme")), console, game).value);
        const auto chosen = PopupDesign::Load(folder, resolved.value);
        return chosen && chosen->base == theme ? PopupDesign::Css(*chosen) : QString();
    }
    if (resolved.from == PlaySettings::Layer::Default) {
        return std::nullopt; // no layer has it: the reader's own setting counts
    }
    if (key == QStringLiteral("popup.mark_unknown") || key == QStringLiteral("popup.hide_tags")) {
        return resolved.value == QStringLiteral("1") ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (SettingOf(folder, key).kind == PlaySetting::Number) {
        return QString::number(static_cast<int>(resolved.value.toDouble())); // "70", not "70.0"
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
    // The design in use there, whole, for a Furoshiki to take as a design; its stylesheet as "css"
    // for an app that draws it that way (the Android apps).
    QJsonObject design;
    if (const auto chosen = PopupDesign::Load(folder, play.Value(SettingOf(folder, QStringLiteral("popup.design")), console, game).value)) {
        design = PopupDesign::ToJson(*chosen);
    }
    return QJsonObject{{QStringLiteral("format"), 1},
                       {QStringLiteral("design"), design},
                       {QStringLiteral("kind"), QStringLiteral("reader-popup-style")},
                       {QStringLiteral("theme"), value("reader_popup_theme")},
                       {QStringLiteral("markUnknown"), mark != QStringLiteral("false") && mark != QStringLiteral("0")},
                       {QStringLiteral("lookups"), value("reader_popup_lookups")},
                       {QStringLiteral("css"), value("reader_popup_css")},
                       // Since the looks' own settings: a reader from before them passes these by.
                       {QStringLiteral("look"), LookOf([&](const QString& key) { return value(key.toLatin1().constData()); })}};
}

QString Import(const QJsonObject& style, PlaySettings& play, const QString& folder, const QString& name,
               const QString& console, const QString& game) {
    if (style.value(QStringLiteral("kind")).toString() != QStringLiteral("reader-popup-style") ||
        style.value(QStringLiteral("format")).toInt() != 1) {
        return QStringLiteral("Not a popup style file");
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
    if (const QJsonObject look = style.value(QStringLiteral("look")).toObject(); !look.isEmpty()) {
        const auto whole = [&](const char* name, const char* key, int low, int high) {
            if (look.contains(QLatin1String(name))) {
                play.Set(QString::fromLatin1(key), QString::number(std::clamp(look.value(QLatin1String(name)).toInt(), low, high)),
                         console, game);
            }
        };
        whole("opacity", "popup.opacity", 0, 100);
        whole("radius", "popup.radius", -1, 28);
        whole("compact", "popup.compact", 0, 9);
        whole("width", "popup.width", 0, 1000);
        for (const char* name : {"glossary", "pitch"}) {
            const QString chosen = look.value(QLatin1String(name)).toString();
            const QString key = QStringLiteral("popup.") + QLatin1String(name);
            if (SettingOf(folder, key).choices.contains(chosen)) {
                play.Set(key, chosen, console, game);
            }
        }
        if (look.contains(QStringLiteral("hideTags"))) {
            play.Set(QStringLiteral("popup.hide_tags"), look.value(QStringLiteral("hideTags")).toBool() ? QStringLiteral("1") : QStringLiteral("0"),
                     console, game);
        }
        const QString portrait = look.value(QStringLiteral("portrait")).toString();
        if (SettingOf(folder, QStringLiteral("popup.portrait")).choices.contains(portrait)) {
            play.Set(QStringLiteral("popup.portrait"), portrait, console, game);
        }
    }
    // Its design, as a design of this folder (a file's CSS alone is no longer taken: designs took
    // its place).
    if (const QJsonObject json = style.value(QStringLiteral("design")).toObject(); !json.isEmpty()) {
        PopupDesign::Design design = PopupDesign::FromJson(json);
        QString file_name = name;
        file_name.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("-"));
        design.name = file_name.isEmpty() || file_name == QStringLiteral("none") ? PopupDesign::FreeName(folder, design.base) : file_name;
        if (design.base.isEmpty()) {
            design.base = style.value(QStringLiteral("theme")).toString();
        }
        if (!PopupDesign::Save(folder, design)) {
            return QStringLiteral("Couldn't save design to %1").arg(folder);
        }
        play.Set(QStringLiteral("popup.design"), design.name, console, game);
    } else {
        play.Set(QStringLiteral("popup.design"), QStringLiteral("none"), console, game);
    }
    return play.Save() ? QString() : QStringLiteral("Couldn't save settings to %1").arg(play.File());
}

} // namespace Home::PopupStyle
