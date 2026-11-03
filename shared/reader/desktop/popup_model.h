// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/reader_database.h"

namespace ReaderApp::PopupModel {

/**
 * What the popup page (shared/popup/popup.js, Reader.show) draws for one headword: its word,
 * reading, inflections, pitch and frequency, one sense per definition with its tags and glosses
 * (Glossary::CompactDefinitions), the word's other spellings, and how often it was looked up
 * (`history`, already said in words, or empty). Everything is text: the page never reads it as
 * markup. The Android app makes the same (PopupModel.kt).
 */
[[nodiscard]] QJsonObject Entry(const LookupResult& result, const QString& history);

/// The senses of a result in the page's form: {heading?, dictionary, number, tags, glosses,
/// notes}. A heading names the dictionary where several dictionaries have senses.
[[nodiscard]] QJsonArray Senses(const LookupResult& result);

/// The other spellings a dictionary lists as an entry of its own ("forms" in JMdict).
[[nodiscard]] QStringList Forms(const LookupResult& result);

/// JMdict lists a word's other spellings as an entry of its own; that is not a sense.
[[nodiscard]] bool IsFormsList(const TermEntry& entry);

/// The box state's name in the page: "add", "working", "inDeck", "checking", "away".
[[nodiscard]] QString StateName(int state);

} // namespace ReaderApp::PopupModel
