// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QJsonArray>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QList>
#include <QString>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/reader_database.h"

namespace ReaderApp::PopupModel {

/**
 * What the popup page (shared/popup/popup.js, Reader.show) draws for one headword: its word,
 * reading, inflections, pitch and frequency, one sense per definition with its tags and glosses
 * (Glossary::CompactDefinitions), the word's other spellings, and how often it was looked up
 * (`history`, already said in words, or empty). Each sense says its dictionary's kind (KindOf:
 * the page's tabs), and a grammar or a character dictionary's sense carries its glossary as the
 * dictionary laid it out ("content"). Everything is text: the page never reads it as markup.
 * The Android app makes the same (PopupModel.kt). `kinds`: the kinds the user gave
 * dictionaries, by title (ReaderSettings::DictionaryKinds).
 */
[[nodiscard]] QJsonObject Entry(const LookupResult& result, const QString& history,
                                const QHash<QString, QString>& kinds = {}, bool without_senses = false);

/**
 * The same entry as the page's JSON text, its glossaries as they are stored (checked to be
 * JSON, not parsed), its pictures named once each: those not in `sent` are put in `pictures`
 * (key → data address) and in `sent`. Kept by headword, as Prepare keeps. What the web popup
 * sends; Entry is for everything else (and the tests).
 */
[[nodiscard]] QByteArray EntryText(const LookupResult& result, const QString& history,
                                   const QHash<QString, QString>& kinds, QSet<QString>& sent, QJsonObject& pictures);
/**
 * A character's entries from the kanji dictionaries, as the page's JSON text: one entry for
 * each dictionary (its "kanji": character, meanings, onyomi, kunyomi, stats, tags), the first
 * with the character's frequencies. The page draws them on its Kanji tab, without card buttons.
 */
[[nodiscard]] QByteArray KanjiText(const QList<KanjiEntry>& kanji, const QList<TermMeta>& frequencies);

/// Makes and keeps EntryText's costly part ahead (as a line is walked). Any thread.
void PrepareText(const LookupResult& result, const QHash<QString, QString>& kinds = {});

/**
 * Makes ahead, and keeps, what Entry costs (a headword's senses, forms and card), as a line is
 * walked before anyone hovers it. Entry keeps what it makes too; a headword is known by its
 * entries' dictionaries and rows, so dictionaries switched or replaced give other headwords.
 * Safe from any thread.
 */
void Prepare(const LookupResult& result, const QHash<QString, QString>& kinds = {});
/// Forgets every headword kept (a dictionary's glossaries changed under the same rows).
void ForgetPrepared();

/**
 * Pictures once each: every sense's media (path → data address) becomes path → "pic:KEY",
 * and the data of keys not in `sent` goes into `pictures` (KEY → data address) and into
 * `sent`. The page keeps the pictures it was given until it loads again.
 */
void SharePictures(QJsonArray& entries, QSet<QString>& sent, QJsonObject& pictures);

/**
 * What kind of dictionary an entry comes from, for the popup's tabs: "word", "grammar", "kanji"
 * or "name". The kind the user gave it (`kinds`, by title); a names dictionary of the running
 * game: names; its title: grammar|文法|文型|語法|DOJG|Bunpro → grammar, JMnedict|人名 → names,
 * kanji (not Kanjium)|漢字|字典|字源|字通|漢和 → kanji for an entry of one character (a compound there is a word); else words.
 * The same rule as Android's DictionaryKinds.
 */
[[nodiscard]] QString KindOf(const TermEntry& entry, const QHash<QString, QString>& kinds = {});

/// The kind a dictionary's title says, or empty (words).
[[nodiscard]] QString KindOfTitle(const QString& title);

/// The tag a names dictionary gives its entries (tools/titles.py), and the page's label for a
/// name in the running game (`"name": true` on the entry and on its senses).
inline const QString NameTag = QStringLiteral("名前");

/// The senses of a result in the page's form: {heading?, dictionary, kind, number, tags, glosses,
/// notes, content?}. A heading names the dictionary where several dictionaries have senses; a
/// grammar or kanji sense has its glossary whole as `content`.
[[nodiscard]] QJsonArray Senses(const LookupResult& result, const QHash<QString, QString>& kinds = {});

/// The other spellings a dictionary lists as an entry of its own ("forms" in JMdict).
[[nodiscard]] QStringList Forms(const LookupResult& result);

/// JMdict lists a word's other spellings as an entry of its own; that is not a sense.
[[nodiscard]] bool IsFormsList(const TermEntry& entry);

/// The box state's name in the page: "add", "working", "inDeck", "checking", "away".
[[nodiscard]] QString StateName(int state);

/// What a preview of the popup shows: a word not in Anki with a frequency and its lookups, one
/// Anki has, inflected, and a name of a game as its names dictionary gives it (no picture).
[[nodiscard]] QJsonArray Sample();

} // namespace ReaderApp::PopupModel
