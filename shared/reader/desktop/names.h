// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <QList>
#include <QString>
#include <QStringList>
#include "reader/desktop/dictionary.h"

namespace ReaderApp::Names {

/**
 * A game's names dictionary as tools/titles.py makes it (docs/titles.md): a Yomitan zip,
 * "names-<title>.zip", whose index.json says which games it is for (furoshikiScope). The program
 * finds them in the user's profiles folder (installed with a script from the published list) and
 * among what it comes with (":/reader/names/").
 */
struct Pack {
    QString path;       ///< The zip
    QString key;        ///< The title's key (furoshikiTitle)
    QString dictionary; ///< Its title as a dictionary: "名前 · <title>"
    QString revision;
    QStringList scope;  ///< Game IDs, upper case
};

inline constexpr const char* BundledFolder = ":/reader/names";

/// The names zips in `folders`, the first folder's before the others' (one per title).
[[nodiscard]] QList<Pack> Find(const QStringList& folders);
/// The pack whose scope has `game`.
[[nodiscard]] std::optional<Pack> For(const QString& game, const QList<Pack>& packs);
/// A zip's index.json read as a pack; nothing when it is not a names dictionary.
[[nodiscard]] std::optional<Pack> Read(const QString& path);

/**
 * Puts `pack`'s dictionary in `database` unless it is there at its revision: one of an older
 * revision is replaced, on or off as the user had it. True when it imported; `error` says why
 * an import failed.
 */
bool Ensure(DictionaryDatabase& database, const Pack& pack, QString* error = nullptr);

/**
 * The card a names dictionary gives an entry (tools/titles.py, `card_gloss`): its one glossary
 * item is structured content whose root holds, as `data`, who or what the name is. `found` is
 * false for a glossary without one (a names dictionary made before the cards, any other
 * dictionary's entry).
 */
struct Card {
    bool found{};
    QString id;      ///< VNDB's "c6487", or "<kind>:<slug>" for one written by hand
    QString kind;    ///< "person", "place" or "thing"
    QString name;    ///< As the game writes it, family and given name apart
    QString reading;
    QString romaji;
    QString role;    ///< In words: "Main character", "Place"...
    QStringList also; ///< The other names
    QString about;   ///< A sentence or two (the dev line's dictionaries only)
    QString image;   ///< Its picture's path in the zip ("img/c6487.jpg"), or empty
};
[[nodiscard]] Card CardOf(const QString& glossary_json);

/**
 * A picture of the names dictionary titled `dictionary`, as a "data:" address a page can show:
 * read from the dictionary's zip (the one `Ensure` was last asked about under that title) and
 * kept, so that a name looked up again costs nothing. Empty when there is no such zip or picture.
 */
[[nodiscard]] QString Picture(const QString& dictionary, const QString& path);
/// Says which zip a names dictionary's pictures are in (Ensure does it by itself).
void Remember(const Pack& pack);

} // namespace ReaderApp::Names
