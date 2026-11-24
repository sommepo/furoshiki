// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import org.json.JSONArray
import org.json.JSONObject

/** How common a term is according to one frequency dictionary. */
data class Frequency(
    val dictionary: String,
    val display: String,
    val value: Double?,
    val rankBased: Boolean
)

/** Pitch accent of one reading: the mora after which the pitch drops (0 = none). */
data class PitchAccent(val dictionary: String, val reading: String, val positions: List<Int>)

/** One headword found at the looked-up position, with everything the dictionaries say about it. */
data class LookupResult(
    val expression: String,
    val reading: String,
    /** The part of the scanned text this headword covers. */
    val matchedText: String,
    /** Inflections undone to reach the headword, outermost first. */
    val reasons: List<String>,
    /** Dictionary entries, most preferred dictionary first. */
    val entries: List<TermEntry>,
    val frequencies: List<Frequency>,
    val pitches: List<PitchAccent>
) {
    /** The reading to show: empty readings mean the expression is already kana. */
    val displayReading: String get() = if (reading.isEmpty() || reading == expression) "" else reading
}

/**
 * Finds the dictionary words that start at the beginning of a piece of text: the longest
 * stretch first, each stretch tried as written, in the other kana script and deinflected.
 */
class DictionaryLookup(
    private val database: DictionaryDatabase,
    private val deinflector: Deinflector = Deinflector()
) {
    private class Candidate(val sourceLength: Int, val deinflection: Deinflection)

    /**
     * Looks up the words starting at the first character of [text]. Returns at most
     * [maxResults] headwords, best first. [game] is the running game's ID: dictionaries scoped
     * to it are used, and their entries come first among words of the same length; other
     * scoped dictionaries are not.
     */
    fun lookup(text: String, maxResults: Int = 24, game: String = "", readFirst: Int = Int.MAX_VALUE): List<LookupResult> {
        val scan = scannable(text)
        if (scan.isEmpty()) return emptyList()

        // Every (prefix, deinflection) pair, keyed by the term to look for.
        val byTerm = HashMap<String, MutableList<Candidate>>()
        var length = scan.length
        while (length > 0) {
            if (Character.isHighSurrogate(scan[length - 1])) {
                length--
                continue
            }
            val source = scan.substring(0, length)
            for (variant in variants(source)) {
                for (deinflection in deinflector.deinflect(variant)) {
                    byTerm.getOrPut(deinflection.term) { ArrayList() }
                        .add(Candidate(length, deinflection))
                }
            }
            length--
        }

        val clock = steps
        var at = System.nanoTime()
        fun step(index: Int) {
            if (clock == null) return
            val now = System.nanoTime()
            clock[index] += now - at
            at = now
        }
        step(0)
        val rows = database.findTerms(byTerm.keys, game, withGlossaries = false)
        step(1)
        clock?.let { it[6] += rows.size.toLong() }
        if (rows.isEmpty()) return emptyList()

        // A headword is one expression + reading. Keep, per headword, the best way it matched.
        class Headword(val expression: String, val reading: String) {
            var sourceLength = 0
            var reasons: List<String> = emptyList()
            var validated = false
            val entries = ArrayList<TermEntry>()
        }
        val headwords = LinkedHashMap<String, Headword>()
        for (row in rows) {
            val wordClass = WordClass.parse(row.rules)
            var best: Candidate? = null
            var bestValidated = false
            for (key in arrayOf(row.expression, row.reading)) {
                if (key.isEmpty()) continue
                for (candidate in byTerm[key] ?: continue) {
                    val required = candidate.deinflection.conditions
                    val exact = required == 0
                    val classMatches = exact || (required and wordClass) != 0
                    // A dictionary without word classes cannot confirm an inflection itself;
                    // such rows are only kept when another dictionary confirms the headword.
                    val acceptable = classMatches || wordClass == 0
                    if (!acceptable) continue
                    val better = best == null ||
                        (classMatches && !bestValidated) ||
                        (classMatches == bestValidated && isBetter(candidate, best))
                    if (better) {
                        best = candidate
                        bestValidated = classMatches
                    }
                }
            }
            val chosen = best ?: continue
            val key = row.expression + "\u0000" + row.reading
            val headword = headwords.getOrPut(key) { Headword(row.expression, row.reading) }
            headword.entries.add(row)
            if (bestValidated) {
                val improves = !headword.validated ||
                    chosen.sourceLength > headword.sourceLength ||
                    (
                        chosen.sourceLength == headword.sourceLength &&
                            chosen.deinflection.reasons.size < headword.reasons.size
                        )
                if (improves) {
                    headword.sourceLength = chosen.sourceLength
                    headword.reasons = chosen.deinflection.reasons
                }
                headword.validated = true
            } else if (!headword.validated && chosen.sourceLength > headword.sourceLength) {
                headword.sourceLength = chosen.sourceLength
                headword.reasons = chosen.deinflection.reasons
            }
        }

        val confirmed = headwords.values.filter { it.validated }
        if (confirmed.isEmpty()) return emptyList()
        val ordered = confirmed.sortedWith(
            compareByDescending<Headword> { it.sourceLength }
                // A name in the running game before any other word of its length.
                .thenByDescending { h -> h.entries.any { it.scoped } }
                .thenBy { it.reasons.size }
                .thenBy { h -> h.entries.minOf { it.priority } }
                .thenByDescending { h -> h.entries.maxOf { it.score } }
                .thenBy { it.expression }
        ).take(maxResults)

        step(2)
        val metas = database.findMeta(ordered.map { it.expression }.toSet())
            .groupBy { it.expression }
        step(3)
        // [readFirst]: only the first headwords' definitions now, the others' by complete().
        val read = database.withGlossaries(ordered.take(readFirst).flatMap { it.entries }.distinctBy { it.id })
            .associateBy { it.id }
        step(4)
        return ordered.map { h ->
            val meta = metas[h.expression].orEmpty()
            LookupResult(
                expression = h.expression,
                reading = h.reading,
                matchedText = text.substring(0, h.sourceLength.coerceAtMost(text.length)),
                reasons = h.reasons,
                // A list of its own: fillIn() puts the definitions read later in place.
                entries = h.entries.map { read[it.id] ?: it }.sortedWith(
                    compareByDescending<TermEntry> { it.scoped }.thenBy { it.priority }.thenByDescending { it.score }
                ).toMutableList(),
                frequencies = frequencies(meta, h.reading),
                pitches = pitches(meta, h.reading.ifEmpty { h.expression })
            )
        }.also { step(5) }
    }

    /**
     * The definitions of [results] (of a lookup made with readFirst) not read yet, in one call,
     * by entry id, off the main thread. The popup shows the first headwords first and these
     * straight after (a hover with many dictionaries read about a hundred definitions before
     * showing anything); fillIn() puts them in place.
     */
    fun readRest(results: List<LookupResult>): Map<Long, TermEntry> {
        val unread = results.flatMap { it.entries }.filter { it.glossaryJson.isEmpty() }.distinctBy { it.id }
        if (unread.isEmpty()) return emptyMap()
        return database.withGlossaries(unread).associateBy { it.id }
    }

    /**
     * Puts [read] (readRest) into [results] themselves, on the thread that shows them: the
     * popup and the card keep each result by identity.
     */
    fun fillIn(results: List<LookupResult>, read: Map<Long, TermEntry>) {
        for (result in results) {
            val entries = result.entries as? MutableList<TermEntry> ?: continue
            for (i in entries.indices) if (entries[i].glossaryJson.isEmpty()) read[entries[i].id]?.let { entries[i] = it }
        }
    }

    /** Whether some of [result]'s definitions are still to be read (complete()). */
    fun unread(result: LookupResult): Boolean = result.entries.any { it.glossaryJson.isEmpty() }

    /**
     * For measuring (the app's DictionaryTest): when set, each lookup adds the nanoseconds of its
     * steps (the candidates, the rows found, the ranking, the frequencies, the definitions, the
     * results made) and, at [6], the rows found.
     */
    @Volatile
    var steps: LongArray? = null

    private fun isBetter(a: Candidate, b: Candidate): Boolean {
        if (a.sourceLength != b.sourceLength) return a.sourceLength > b.sourceLength
        return a.deinflection.reasons.size < b.deinflection.reasons.size
    }

    /** The leading part of [text] worth scanning: up to the first break, at most a few words. */
    private fun scannable(text: String): String {
        var end = 0
        while (end < text.length && end < MAX_SCAN_LENGTH) {
            val cp = text.codePointAt(end)
            if (!JapaneseText.isTermChar(cp) && cp != '々'.code && cp != '〇'.code &&
                cp != '・'.code && cp != '～'.code && cp != '〜'.code
            ) {
                break
            }
            end += Character.charCount(cp)
        }
        return text.substring(0, end)
    }

    /** [source] as written plus the spellings a dictionary may use for it. */
    private fun variants(source: String): Set<String> {
        val out = LinkedHashSet<String>()
        out.add(source)
        out.add(JapaneseText.katakanaToHiragana(source))
        val ascii = JapaneseText.fullWidthToAscii(source)
        out.add(ascii)
        if (ascii.any { it in 'A'..'Z' }) out.add(ascii.lowercase())
        return out
    }

    private fun frequencies(meta: List<TermMeta>, reading: String): List<Frequency> {
        val out = ArrayList<Frequency>()
        for (m in meta) {
            if (m.mode != "freq") continue
            var value: Any? = parse(m.data)
            if (value is JSONObject && value.has("frequency")) {
                // {reading, frequency}: only for the matching reading.
                val forReading = value.optString("reading")
                if (reading.isNotEmpty() && forReading.isNotEmpty() &&
                    JapaneseText.katakanaToHiragana(forReading) !=
                    JapaneseText.katakanaToHiragana(reading)
                ) {
                    continue
                }
                value = value.opt("frequency")
            }
            val (number, display) = when (value) {
                is JSONObject -> {
                    val n = value.optDouble("value").takeUnless { it.isNaN() }
                    n to value.optString("displayValue").ifEmpty { n?.let(::formatNumber) ?: "" }
                }

                is Number -> value.toDouble() to formatNumber(value.toDouble())

                is String -> value.toDoubleOrNull() to value

                else -> continue
            }
            if (display.isEmpty()) continue
            out.add(Frequency(m.dictionary, display, number, m.frequencyMode != "occurrence-based"))
        }
        return out
    }

    private fun pitches(meta: List<TermMeta>, reading: String): List<PitchAccent> {
        val out = ArrayList<PitchAccent>()
        val wanted = JapaneseText.katakanaToHiragana(reading)
        for (m in meta) {
            if (m.mode != "pitch") continue
            val value = parse(m.data) as? JSONObject ?: continue
            val forReading = value.optString("reading")
            if (JapaneseText.katakanaToHiragana(forReading) != wanted) continue
            val list = value.optJSONArray("pitches") ?: continue
            val positions = ArrayList<Int>()
            for (i in 0 until list.length()) {
                when (val position = list.optJSONObject(i)?.opt("position")) {
                    is Number -> positions.add(position.toInt())
                    // Yomitan's other form: high and low by mora ("LHHL").
                    is String -> patternPosition(position)?.let(positions::add)
                }
            }
            if (positions.isNotEmpty()) out.add(PitchAccent(m.dictionary, forReading, positions))
        }
        return out
    }

    /** Where a pattern of high and low morae ("LHHL") falls: after which mora, 0 for none. */
    private fun patternPosition(pattern: String): Int? {
        if (pattern.isEmpty() || pattern.any { it != 'H' && it != 'L' }) return null
        for (i in 0 until pattern.length - 1) if (pattern[i] == 'H' && pattern[i + 1] == 'L') return i + 1
        return 0
    }

    private fun parse(json: String): Any? {
        val trimmed = json.trim()
        return try {
            when {
                trimmed.startsWith("{") -> JSONObject(trimmed)
                trimmed.startsWith("[") -> JSONArray(trimmed)
                trimmed.startsWith("\"") -> JSONArray("[$trimmed]").get(0)
                else -> trimmed.toDoubleOrNull()
            }
        } catch (_: Exception) {
            null
        }
    }

    private fun formatNumber(value: Double): String =
        if (value == Math.floor(value) && !value.isInfinite()) value.toLong().toString() else value.toString()

    companion object {
        /** Longest stretch of text tried as one word. */
        const val MAX_SCAN_LENGTH = 20

        /**
         * A single rank for sorting cards by frequency: the harmonic mean of the term's ranks in
         * rank-based frequency dictionaries, or null when there is none.
         */
        fun harmonicRank(frequencies: List<Frequency>): Long? {
            val ranks = frequencies.filter { it.rankBased }.mapNotNull { it.value }.filter { it > 0 }
            if (ranks.isEmpty()) return null
            return Math.round(ranks.size / ranks.sumOf { 1.0 / it })
        }
    }
}
