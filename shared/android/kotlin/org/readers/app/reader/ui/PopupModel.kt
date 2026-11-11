// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import org.json.JSONArray
import org.json.JSONObject
import org.readers.app.reader.dict.GlossaryRenderer
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.dict.NamesDictionaries
import org.readers.app.reader.dict.TermEntry

/**
 * What the popup page (shared/popup/popup.js, Reader.show) draws for one headword: its word,
 * reading, inflections, pitch and frequency, one sense per definition with its tags and glosses
 * ([GlossaryRenderer.compact]), the word's other spellings, and how often it was looked up
 * ([history], already said in words, or null). Everything is text: the page never reads it as
 * markup. The desktop makes the same (shared/reader/desktop/popup_model.*).
 */
object PopupModel {
    fun entry(result: LookupResult, history: String?): JSONObject = JSONObject().apply {
        put("expression", result.expression)
        put("reading", result.displayReading)
        put("reasons", JSONArray(result.reasons))
        put("pitches", JSONArray(result.pitches.map { it.positions.joinToString(",", "[", "]") }))
        put("frequencies", JSONArray(result.frequencies.map { JSONObject().put("dictionary", it.dictionary).put("value", it.display) }))
        put("senses", senses(result))
        put("forms", JSONArray(forms(result)))
        put("state", "add")
        put("unknown", false)
        // A name in the running game, from its names dictionary: the page tags it 名前.
        result.entries.firstOrNull()?.takeIf { it.scoped }?.let { first ->
            put("name", true)
            // Who or what it is, as a card, when its dictionary says (tools/titles.py): the page
            // draws that in place of the senses, with the picture the dictionary's zip has.
            NamesDictionaries.cardOf(first.glossaryJson)?.let { card ->
                put("card", JSONObject().apply {
                    put("name", card.name)
                    put("kind", card.kind)
                    put("also", JSONArray(card.also))
                    fun say(name: String, value: String) { if (value.isNotEmpty()) put(name, value) }
                    say("id", card.id)
                    say("reading", card.reading)
                    say("romaji", card.romaji)
                    say("role", card.role)
                    say("about", card.about)
                    say("image", NamesDictionaries.picture(first.dictionary, card.image))
                })
            }
        }
        if (!history.isNullOrEmpty()) put("history", history)
    }

    /** The tag a names dictionary gives its entries (tools/titles.py). */
    const val NAME_TAG = "名前"

    /**
     * The senses in the page's form: {heading?, dictionary, number, tags, glosses, notes}. A
     * heading names the dictionary where several dictionaries have senses; tags the sense
     * before had already are not said again.
     */
    fun senses(result: LookupResult): JSONArray {
        val entries = result.entries.filterNot(::isFormsList)
        val several = entries.map { it.dictionary }.distinct().size > 1
        val out = JSONArray()
        var dictionary: String? = null
        var number = 0
        var lastTags = emptyList<String>()
        for (entry in entries) {
            val definitions = GlossaryRenderer.compact(entry.glossaryJson)
            if (definitions.isEmpty()) continue
            val sense = JSONObject()
            if (entry.dictionary != dictionary) {
                dictionary = entry.dictionary
                number = 0
                lastTags = emptyList()
                if (several) sense.put("heading", entry.dictionary)
            }
            number++
            // Numbers among the tags only repeat the order of the senses; a name's 名前 is said
            // once, by the headword.
            val tags = entry.definitionTags.split(' ')
                .filter { it.isNotBlank() && it.toIntOrNull() == null && !(entry.scoped && it == NAME_TAG) }
            sense.put("dictionary", entry.dictionary)
            if (entry.scoped) sense.put("name", true)
            sense.put("number", number)
            sense.put("tags", JSONArray(if (tags == lastTags) emptyList() else tags))
            sense.put("glosses", JSONArray(definitions.flatMap { it.glosses.split(" | ").filter(String::isNotEmpty) }))
            sense.put("notes", definitions.map { it.notes }.filter { it.isNotEmpty() }.joinToString("; "))
            lastTags = tags
            out.put(sense)
        }
        return out
    }

    /** The other spellings a dictionary lists as an entry of its own ("forms" in JMdict). */
    fun forms(result: LookupResult): List<String> {
        val out = LinkedHashSet<String>()
        for (entry in result.entries.filter(::isFormsList)) {
            for (definition in GlossaryRenderer.compact(entry.glossaryJson)) {
                // JMdict marks a rare, irregular or search-only form with a sign (★, ㊗, ⚠...),
                // sometimes in brackets: the form is the word without them.
                definition.glosses.split(FORM_SEPARATOR).map { it.replace(MARKS, "").trim() }
                    .filter { it.isNotEmpty() }.forEach { out.add(it) }
            }
        }
        return out.toList()
    }

    /** JMdict lists a word's other spellings as an entry of its own; that is not a sense. */
    fun isFormsList(entry: TermEntry): Boolean = entry.definitionTags.split(' ').any { it == "forms" }

    /** The box state's name in the page. */
    fun stateName(state: LookupPopupView.CardState): String = when (state) {
        LookupPopupView.CardState.ADD -> "add"
        LookupPopupView.CardState.WORKING -> "working"
        LookupPopupView.CardState.IN_DECK -> "inDeck"
        LookupPopupView.CardState.CHECKING -> "checking"
        LookupPopupView.CardState.AWAY -> "away"
    }

    private val FORM_SEPARATOR = Regex("\\s*[|;、,]\\s*")
    private val MARKS = Regex("[（(][^）)]*[）)]|[\\u2600-\\u27BF\\u3280-\\u32BF\\u2B50\\uFE0F]|[\\uD83C-\\uD83E][\\uDC00-\\uDFFF]")
}
