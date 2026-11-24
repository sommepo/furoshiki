// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import org.json.JSONArray
import org.json.JSONObject
import org.readers.app.reader.dict.DictionaryKinds
import org.readers.app.reader.dict.GlossaryRenderer
import org.readers.app.reader.dict.KanjiEntry
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.dict.NamesDictionaries
import org.readers.app.reader.dict.TermEntry
import org.readers.app.reader.dict.TermMeta

/**
 * What the popup page (shared/popup/popup.js, Reader.show) draws for one headword: its word,
 * reading, inflections, pitch and frequency, one sense per definition with its tags and glosses
 * ([GlossaryRenderer.compact]), the word's other spellings, and how often it was looked up
 * ([history], already said in words, or null). Each sense says its dictionary's kind
 * ([DictionaryKinds]: the page's tabs), and a grammar or a character dictionary's sense carries
 * its glossary as the dictionary laid it out ("content"), which the page builds element by
 * element. Everything is text: the page never reads it as markup. The desktop makes the same
 * (shared/reader/desktop/popup_model.*).
 */
object PopupModel {
    /** [kinds]: the kinds the user gave dictionaries, by title (ReaderSettings.dictionaryKinds). */
    fun entry(result: LookupResult, history: String?, kinds: Map<String, String> = emptyMap(), withoutSenses: Boolean = false): JSONObject = JSONObject().apply {
        put("expression", result.expression)
        put("reading", result.displayReading)
        put("reasons", JSONArray(result.reasons))
        put("pitches", JSONArray(result.pitches.map { it.positions.joinToString(",", "[", "]") }))
        // For the graph and the text form: which reading, and where it falls.
        put("pitchAccents", JSONArray(result.pitches.map {
            JSONObject().put("dictionary", it.dictionary).put("reading", it.reading).put("positions", JSONArray(it.positions))
        }))
        put("frequencies", JSONArray(result.frequencies.map { JSONObject().put("dictionary", it.dictionary).put("value", it.display) }))
        if (!withoutSenses) {
            put("senses", senses(result, kinds))
            put("forms", JSONArray(forms(result)))
        }
        put("state", "add")
        put("unknown", false)
        // A name in the running game, from its names dictionary: the page tags it 名前.
        result.entries.firstOrNull()?.takeIf { it.scoped }?.let { first ->
            put("name", true)
            // Who or what it is, as a card, when its dictionary says (tools/titles.py): the page
            // draws that in place of the senses, with the picture the dictionary's zip has.
            if (!withoutSenses) NamesDictionaries.cardOf(first.glossaryJson)?.let { card ->
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

    // ---- The page's model as text (entryText) ----
    // A glossary is JSON already: it goes to the page as it is stored, after a check that it is
    // JSON (a dictionary's broken text must not break the page), not parsed into org.json's
    // objects and written out again, which took most of a hover with many dictionaries. The
    // desktop does the same (PopupModel::EntryText).

    /** A headword's senses, forms and card as the page's JSON, and the pictures they name. */
    /** A headword's page text; its pictures by key, each read (a data address) only when sent. */
    private class KeptText(val senses: String, val forms: String, val card: String?, val pictures: List<Pair<String, () -> String?>>) {
        val size = senses.length + forms.length + (card?.length ?: 0) + 64
    }

    /** Kept by headword (its entries' dictionaries and rows, and the kinds given), least used out first. */
    private val keptText = LinkedHashMap<String, KeptText>(64, 0.75f, true)
    private var keptTotal = 0L
    private const val KEPT_CHARS = 12L * 1024 * 1024

    private fun keyOf(result: LookupResult, kinds: Map<String, String>): String = buildString {
        append(result.expression).append('\n').append(result.reading)
        for (entry in result.entries) append('\n').append(entry.dictionaryId).append(':').append(entry.id).append(if (entry.scoped) 'n' else 'w')
        if (kinds.isNotEmpty()) append('\n').append(kinds.entries.map { "${it.key}=${it.value}" }.sorted().joinToString("\t"))
    }

    private fun makeText(result: LookupResult, kinds: Map<String, String>): KeptText {
        val entries = result.entries.filterNot(::isFormsList)
        val several = entries.map { it.dictionary }.distinct().size > 1
        val pictures = ArrayList<Pair<String, () -> String?>>()
        val senses = StringBuilder("[")
        var dictionary: String? = null
        var number = 0
        var lastTags = emptyList<String>()
        var any = false
        for (entry in entries) {
            val content = checkedArray(entry.glossaryJson)
            if (content == "[]") continue // nothing to show
            val sense = JSONObject()
            if (entry.dictionary != dictionary) {
                dictionary = entry.dictionary
                number = 0
                lastTags = emptyList()
                if (several) sense.put("heading", entry.dictionary)
            }
            number++
            val tags = entry.definitionTags.split(' ')
                .filter { it.isNotBlank() && it.toIntOrNull() == null && !(entry.scoped && it == NAME_TAG) }
            sense.put("dictionary", entry.dictionary)
            sense.put("kind", DictionaryKinds.of(entry, kinds))
            if (entry.media.isNotEmpty()) {
                val media = JSONObject()
                val all = entry.media
                for (path in all.keys) {
                    val key = entry.dictionary + "\n" + path
                    media.put(path, "pic:$key")
                    pictures.add(key to { all[path] })
                }
                sense.put("media", media)
            }
            if (entry.scoped) sense.put("name", true)
            sense.put("number", number)
            sense.put("tags", JSONArray(if (tags == lastTags) emptyList() else tags))
            // The page reads glosses and notes only where a sense has no content: every sense here has.
            sense.put("glosses", JSONArray())
            sense.put("notes", "")
            lastTags = tags
            val text = sense.toString()
            if (any) senses.append(',')
            senses.append(text, 0, text.length - 1).append(",\"content\":").append(content).append('}')
            any = true
        }
        senses.append(']')
        val card = result.entries.firstOrNull()?.takeIf { it.scoped }?.let { first ->
            NamesDictionaries.cardOf(first.glossaryJson)?.let { card ->
                JSONObject().apply {
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
                }.toString()
            }
        }
        return KeptText(senses.toString(), JSONArray(forms(result)).toString(), card, pictures)
    }

    private fun keptOrMade(result: LookupResult, kinds: Map<String, String>): KeptText {
        // A headword whose definitions are still to be read (DictionaryLookup.complete) or synthetic (0, 0) entries are not kept.
        if (result.entries.any { it.glossaryJson.isEmpty() || (it.dictionaryId == 0L && it.id == 0L) }) return makeText(result, kinds)
        val key = keyOf(result, kinds)
        synchronized(keptText) { keptText[key]?.let { return it } }
        val made = makeText(result, kinds)
        synchronized(keptText) {
            keptText.put(key, made)?.let { old -> keptTotal -= old.size }
            keptTotal += made.size
            val it = keptText.entries.iterator()
            while (keptTotal > KEPT_CHARS && keptText.size > 1 && it.hasNext()) {
                val eldest = it.next()
                keptTotal -= eldest.value.size
                it.remove()
            }
        }
        return made
    }

    /**
     * [entry] as the page's JSON text, its glossaries as they are stored (checked to be JSON, not
     * parsed), its pictures named once each: those not in [sent] go into [pictures] (key → data
     * address) and into [sent]. What a page popup sends; [entry] is for everything else.
     */
    /** The character a word begins with, when it is a kanji (or 々): what the Kanji tab is asked for. */
    fun kanjiOf(text: String): String? {
        if (text.isEmpty()) return null
        val code = text.codePointAt(0)
        val ideograph = code in 0x4E00..0x9FFF || code in 0x3400..0x4DBF || code in 0xF900..0xFAFF ||
            code in 0x20000..0x3134F || code == 0x3005
        return if (ideograph) String(Character.toChars(code)) else null
    }

    /**
     * A character's entries from the kanji dictionaries, as the page's JSON text (no brackets):
     * one for each dictionary, with its "kanji" (character, meanings, onyomi, kunyomi, stats,
     * tags), the first with the character's frequencies. The page draws them on its Kanji tab,
     * without card buttons. As the desktop's PopupModel::KanjiText.
     */
    fun kanjiText(kanji: List<KanjiEntry>, frequencies: List<TermMeta>): String {
        val shown = JSONArray()
        for (meta in frequencies) {
            if (meta.mode != "freq") continue
            // A number, a string, or {value, displayValue}.
            val value = runCatching { JSONArray("[" + meta.data + "]").get(0) }.getOrNull() ?: continue
            val said = when (value) {
                is JSONObject -> value.optString("displayValue").ifEmpty { value.optString("value") }
                else -> value.toString()
            }
            if (said.isNotEmpty()) shown.put(JSONObject().put("dictionary", meta.dictionary).put("value", said))
        }
        return kanji.mapIndexed { index, entry ->
            val character = JSONObject()
                .put("character", entry.character)
                .put("dictionary", entry.dictionary)
                .put("meanings", JSONArray(entry.meanings))
                .put("onyomi", JSONArray(entry.onyomi))
                .put("kunyomi", JSONArray(entry.kunyomi))
                .put("tags", entry.tags)
                .put("stats", runCatching { JSONObject(entry.statsJson) }.getOrDefault(JSONObject()))
            JSONObject()
                .put("expression", entry.character)
                .put("reading", "")
                .put("reasons", JSONArray())
                .put("senses", JSONArray())
                .put("forms", JSONArray())
                .put("frequencies", if (index == 0) shown else JSONArray())
                .put("state", "add")
                .put("unknown", false)
                .put("kanji", character)
                .toString()
        }.joinToString(",")
    }

    fun entryText(result: LookupResult, history: String?, kinds: Map<String, String>, sent: MutableSet<String>, pictures: JSONObject): String {
        val kept = keptOrMade(result, kinds)
        // A picture is read only for a page that has not had it.
        for ((key, data) in kept.pictures) if (key !in sent) data()?.let { sent.add(key); pictures.put(key, it) }
        val light = entry(result, history, kinds, withoutSenses = true).toString()
        return buildString(light.length + kept.senses.length + kept.forms.length + 64) {
            append(light, 0, light.length - 1)
            append(",\"senses\":").append(kept.senses)
            append(",\"forms\":").append(kept.forms)
            kept.card?.let { append(",\"card\":").append(it) }
            append('}')
        }
    }

    /** Makes and keeps [entryText]'s costly part ahead (as a line is walked). Any thread. */
    fun prepareText(result: LookupResult, kinds: Map<String, String> = emptyMap()) {
        keptOrMade(result, kinds)
    }

    /** Forgets every headword kept. */
    fun forgetPrepared() = synchronized(keptText) { keptText.clear(); keptTotal = 0L }

    /** [json] when it is a JSON array (spaces trimmed), else "[]". */
    internal fun checkedArray(json: String): String {
        var begin = 0
        var end = json.length
        while (begin < end && json[begin].isJsonSpace()) begin++
        while (end > begin && json[end - 1].isJsonSpace()) end--
        if (begin >= end || json[begin] != '[') return "[]"
        val stop = valueEnd(json, begin, end, 0)
        return if (stop == end) json.substring(begin, end) else "[]"
    }

    private fun Char.isJsonSpace() = this == ' ' || this == '\n' || this == '\r' || this == '\t'

    /** The end of the JSON value at [at], or -1 when it is not one. */
    private fun valueEnd(s: String, at: Int, end: Int, depth: Int): Int {
        var p = at
        while (p < end && s[p].isJsonSpace()) p++
        if (p >= end || depth > 200) return -1
        val c = s[p]
        if (c == '"') {
            p++
            while (p < end) {
                val ch = s[p]
                if (ch == '"') return p + 1
                if (ch < ' ') return -1
                if (ch == '\\') {
                    p++
                    if (p >= end) return -1
                    when (s[p]) {
                        'u' -> {
                            if (end - p < 5) return -1
                            for (i in 1..4) if (Character.digit(s[p + i], 16) < 0) return -1
                            p += 4
                        }
                        '"', '\\', '/', 'b', 'f', 'n', 'r', 't' -> {}
                        else -> return -1
                    }
                }
                p++
            }
            return -1
        }
        if (c == '[' || c == '{') {
            val close = if (c == '[') ']' else '}'
            p++
            while (p < end && s[p].isJsonSpace()) p++
            if (p < end && s[p] == close) return p + 1
            while (true) {
                if (c == '{') {
                    while (p < end && s[p].isJsonSpace()) p++
                    if (p >= end || s[p] != '"') return -1
                    p = valueEnd(s, p, end, depth + 1)
                    if (p < 0) return -1
                    while (p < end && s[p].isJsonSpace()) p++
                    if (p >= end || s[p] != ':') return -1
                    p++
                }
                p = valueEnd(s, p, end, depth + 1)
                if (p < 0) return -1
                while (p < end && s[p].isJsonSpace()) p++
                if (p < end && s[p] == ',') { p++; continue }
                if (p < end && s[p] == close) return p + 1
                return -1
            }
        }
        for (word in arrayOf("true", "false", "null")) if (s.startsWith(word, p)) return p + word.length
        if (p < end && s[p] == '-') p++
        if (p >= end || !s[p].isDigit()) return -1
        if (s[p] == '0') {
            p++
        } else {
            while (p < end && s[p].isDigit()) p++
        }
        if (p < end && s[p] == '.') {
            p++
            if (p >= end || !s[p].isDigit()) return -1
            while (p < end && s[p].isDigit()) p++
        }
        if (p < end && (s[p] == 'e' || s[p] == 'E')) {
            p++
            if (p < end && (s[p] == '+' || s[p] == '-')) p++
            if (p >= end || !s[p].isDigit()) return -1
            while (p < end && s[p].isDigit()) p++
        }
        return p
    }

    /** The tag a names dictionary gives its entries (tools/titles.py). */
    const val NAME_TAG = "名前"

    /**
     * The senses in the page's form: {heading?, dictionary, kind, number, tags, glosses, notes,
     * content?}. A heading names the dictionary where several dictionaries have senses; tags the
     * sense before had already are not said again. [kind] is its dictionary's ([DictionaryKinds]).
     * Every sense also has [content], its glossary whole, for the page to lay out as the
     * dictionary did (a word's when its layout is "full", a grammar or a kanji sense's always),
     * and [media], the pictures it shows.
     */
    fun senses(result: LookupResult, kinds: Map<String, String> = emptyMap()): JSONArray {
        val entries = result.entries.filterNot(::isFormsList)
        val several = entries.map { it.dictionary }.distinct().size > 1
        val out = JSONArray()
        var dictionary: String? = null
        var number = 0
        var lastTags = emptyList<String>()
        for (entry in entries) {
            val definitions = GlossaryRenderer.compact(entry.glossaryJson)
            // A glossary with no text the compact form keeps (a picture alone) still has its layout.
            val laid = runCatching { JSONArray(entry.glossaryJson) }.getOrNull()
            if (definitions.isEmpty() && (laid == null || laid.length() == 0)) continue
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
            val kind = DictionaryKinds.of(entry, kinds)
            sense.put("kind", kind)
            laid?.let { sense.put("content", it) }
            if (entry.media.isNotEmpty()) sense.put("media", JSONObject(entry.media))
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
