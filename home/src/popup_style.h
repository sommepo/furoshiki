// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The dictionary popup's look in the program's layers (play_settings.h): its theme, the design over
// it (popup_design.h), whether words not in Anki are marked, how often a word was looked up, set
// for all consoles, one console or one game on the CONSOLES page like any other setting
// (popup.theme, popup.design, popup.mark_unknown, popup.lookups). The reader asks for them through
// ReaderSettings::SetLayered and takes its own settings (reader.ini) where no layer has one.
//
// The designs are files: <program>/popup-styles/<name>.json, each a choice of popup.design (the
// user's CSS files of before are no longer drawn). A popup style travels as one file (Export,
// Import) that the Android apps read and write too; its "css" is the design's stylesheet:
//   { "format": 1, "kind": "reader-popup-style", "theme": "yomitan-dark", "design": { ... },
//     "markUnknown": true, "lookups": "where", "css": "...",
//     "look": { "opacity": 85, "radius": -1, "compact": 2, "hideTags": false, "portrait": "small" } }

#include <functional>
#include <optional>
#include <QJsonObject>
#include <QList>
#include <QString>
#include "play_settings.h"

namespace Home::PopupStyle {

/// The look's own settings as the page takes them (Reader.show's "look"), from what `value`
/// says for each of the reader's keys (reader_popup_opacity, _radius, _compact, _hide_tags,
/// _portrait).
[[nodiscard]] QJsonObject LookOf(const std::function<QString(const QString&)>& value);

/// Where the user's CSS files are.
[[nodiscard]] QString Folder(const QString& program_folder);
/// The popup's settings as the CONSOLES page shows them, popup.design with a choice for each
/// design in `folder` ("none" first).
[[nodiscard]] QList<PlaySetting> Settings(const QString& folder);
/// What a layer says for one of the reader's popup settings (reader_popup_theme and so on) on
/// `console` and `game`, or nothing where no layer has it. reader_popup_css is the stylesheet of
/// the design in use there over its own look, else empty (never the reader's own).
[[nodiscard]] std::optional<QString> Layered(const PlaySettings& play, const QString& folder,
                                             const QString& reader_key, const QString& console,
                                             const QString& game);
/// A popup style as one file's content, from what applies on that layer: `fallback` gives the
/// reader's own value of a reader key where no layer has one.
[[nodiscard]] QJsonObject Export(const PlaySettings& play, const QString& folder,
                                 const QString& console, const QString& game,
                                 const std::function<QString(const QString&)>& fallback);
/// Takes a popup style into that layer (all consoles: console empty), its design into a file of
/// the folder named after `name`. Returns what went wrong, or nothing.
[[nodiscard]] QString Import(const QJsonObject& style, PlaySettings& play, const QString& folder,
                             const QString& name, const QString& console, const QString& game);

} // namespace Home::PopupStyle
