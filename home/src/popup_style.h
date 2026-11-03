// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The dictionary popup's look in the program's layers (play_settings.h): its theme, whether
// words not in Anki are marked, how often a word was looked up, and the user's own CSS, set
// for all consoles, one console or one game on the CONSOLES page like any other setting
// (popup.theme, popup.mark_unknown, popup.lookups, popup.css). The reader asks for them through
// ReaderSettings::SetLayered and takes its own settings (reader.ini) where no layer has one.
//
// The user's CSS is in files: <program>/popup-styles/<name>.css, each a choice of popup.css. A
// popup style travels as one file (Export, Import) that the Android apps read and write too:
//   { "format": 1, "kind": "reader-popup-style", "theme": "yomitan-dark",
//     "markUnknown": true, "lookups": "where", "css": "..." }

#include <functional>
#include <optional>
#include <QJsonObject>
#include <QList>
#include <QString>
#include "play_settings.h"

namespace Home::PopupStyle {

/// Where the user's CSS files are.
[[nodiscard]] QString Folder(const QString& program_folder);
/// The popup's settings as the CONSOLES page shows them, popup.css with a choice for each CSS
/// file in `folder` ("none" first).
[[nodiscard]] QList<PlaySetting> Settings(const QString& folder);
/// What a layer says for one of the reader's popup settings (reader_popup_theme and so on) on
/// `console` and `game`, or nothing where no layer has it. reader_popup_css is the chosen
/// file's text.
[[nodiscard]] std::optional<QString> Layered(const PlaySettings& play, const QString& folder,
                                             const QString& reader_key, const QString& console,
                                             const QString& game);
/// A popup style as one file's content, from what applies on that layer: `fallback` gives the
/// reader's own value of a reader key where no layer has one.
[[nodiscard]] QJsonObject Export(const PlaySettings& play, const QString& folder,
                                 const QString& console, const QString& game,
                                 const std::function<QString(const QString&)>& fallback);
/// Takes a popup style into that layer (all consoles: console empty), its CSS into a file of
/// the folder named after `name`. Returns what went wrong, or nothing.
[[nodiscard]] QString Import(const QJsonObject& style, PlaySettings& play, const QString& folder,
                             const QString& name, const QString& console, const QString& game);

} // namespace Home::PopupStyle
