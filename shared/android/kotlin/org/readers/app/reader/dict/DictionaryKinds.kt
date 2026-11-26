// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import org.json.JSONObject

/**
 * What kind of dictionary a result comes from, for the popup's tabs (shared/popup/README.md,
 * "Kinds"): words, grammar, kanji or names. Yomitan's format does not say: a grammar dictionary
 * ("Dictionary of Japanese Grammar", Bunpro...) is a term dictionary like JMdict, and a
 * character dictionary is a kanji bank (which the importer skips) or, for some, a term
 * dictionary of single characters. So, in this order:
 *
 *  1. the kind the user gave the dictionary (the dictionaries page; [parse] reads the setting);
 *  2. a names dictionary of the running game (its `furoshikiScope`): names;
 *  3. its title: "grammar", 文法, 文型, 語法, DOJG, Bunpro, 絵でわかる日本語, 日本語教師 → grammar;
 *     JMnedict, 人名 → names; "kanji" (not Kanjium, a pitch dictionary), 漢字, 字典, 字源, 字通, 漢和 → kanji, for an entry of one character only (a compound
 *     in such a dictionary is a word);
 *  4. words.
 *
 * The desktop has the same rule (shared/reader/desktop/popup_model.cpp, `KindOf`).
 */
object DictionaryKinds {
    const val WORD = "word"
    const val GRAMMAR = "grammar"
    const val KANJI = "kanji"
    const val NAME = "name"
    val ALL = listOf(WORD, GRAMMAR, KANJI, NAME)

    private val GRAMMAR_TITLE = Regex("grammar|文法|文型|語法|dojg|bunpro|絵でわかる日本語|日本語教師", RegexOption.IGNORE_CASE)
    private val NAMES_TITLE = Regex("jmnedict|人名", RegexOption.IGNORE_CASE)
    private val KANJI_TITLE = Regex("kanji(?!um)|漢字|字典|字源|字通|漢和", RegexOption.IGNORE_CASE)

    /** The kind its title says, or null when it says none (words). */
    fun ofTitle(title: String): String? = when {
        GRAMMAR_TITLE.containsMatchIn(title) -> GRAMMAR
        NAMES_TITLE.containsMatchIn(title) -> NAME
        KANJI_TITLE.containsMatchIn(title) -> KANJI
        else -> null
    }

    /** The kind of [entry]'s dictionary; [chosen]: the kinds the user gave dictionaries, by title. */
    fun of(entry: TermEntry, chosen: Map<String, String> = emptyMap()): String {
        chosen[entry.dictionary]?.takeIf { it in ALL }?.let { return it }
        if (entry.scoped) return NAME
        return when (val byTitle = ofTitle(entry.dictionary)) {
            KANJI -> if (entry.expression.codePointCount(0, entry.expression.length) == 1) KANJI else WORD
            null -> WORD
            else -> byTitle
        }
    }

    /** The setting's text (a JSON object, title → kind) as a map; kinds it does not know are left out. */
    fun parse(text: String?): Map<String, String> {
        if (text.isNullOrBlank()) return emptyMap()
        val json = runCatching { JSONObject(text) }.getOrNull() ?: return emptyMap()
        val out = HashMap<String, String>()
        for (key in json.keys()) json.optString(key).takeIf { it in ALL }?.let { out[key] = it }
        return out
    }

    /** [chosen] with [title]'s kind set ([kind] null: automatic again), as the setting's text. */
    fun with(chosen: Map<String, String>, title: String, kind: String?): String {
        val out = JSONObject()
        for ((key, value) in chosen) if (key != title) out.put(key, value)
        if (kind != null && kind in ALL) out.put(title, kind)
        return out.toString()
    }
}
