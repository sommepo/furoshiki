// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.anki

import java.io.File
import org.json.JSONObject

/**
 * What the deck cards go to holds, kept on this device: for each note, what is in the field
 * Anki tells notes apart by. The popup asks it whether a word is in Anki before it is drawn,
 * so the right box is there at once; Anki itself is asked afterwards, to confirm.
 *
 * It is filled by reading every note of the deck once (in pages), brought up to date with the
 * notes changed since ([REFRESH_AFTER_MS]), and read whole again now and then
 * ([REFILL_AFTER_MS]), which is how it learns of notes that were deleted. What Anki says about
 * a word in between goes into it too ([learn], [add]). It is kept in a file, so that it
 * answers from the start when the app starts again, also while Anki cannot be reached.
 *
 * The same as the desktop's (shared/reader/desktop/anki_index.*). Safe to use from several
 * threads.
 */
class AnkiIndex {
    /**
     * Which notes it holds: those of one Anki ([anki]: the transport and its address), in one
     * deck (or the whole collection when [scope] is null), by one field. Another source makes
     * it start over.
     */
    data class Source(val anki: String, val scope: String?, val field: String)

    enum class Answer {
        /** Not filled for this source yet: only Anki can tell. */
        NOT_READY,
        ABSENT,
        PRESENT,
    }

    data class Found(val answer: Answer, val notes: List<Long> = emptyList())

    /** What it should do next: nothing, read what changed, or read everything. */
    enum class Due { NOTHING, REFRESH, FILL }

    private var held: Source? = null
    private var filledAt = 0L
    private var refreshedAt = 0L
    private val byValue = HashMap<String, MutableList<Long>>()
    private val valueOf = HashMap<Long, String>()

    @Synchronized
    fun find(source: Source, value: String): Found {
        if (held != source) return Found(Answer.NOT_READY)
        val notes = byValue[normal(value)]
        return if (notes.isNullOrEmpty()) Found(Answer.ABSENT) else Found(Answer.PRESENT, notes.toList())
    }

    @Synchronized
    fun whatIsDue(source: Source, now: Long): Due = when {
        held != source || now - filledAt >= REFILL_AFTER_MS || now < filledAt -> Due.FILL
        now - refreshedAt >= REFRESH_AFTER_MS -> Due.REFRESH
        else -> Due.NOTHING
    }

    /**
     * Reads every note of [source] from Anki and holds those instead of what it held. Stops
     * early, keeping what it held, when [cancelled] says so. Throws [AnkiException].
     */
    fun fill(client: AnkiClient, source: Source, now: Long, cancelled: () -> Boolean = { false }) {
        val ids = client.findNotesWhere(AnkiSearch.hasField(source.scope, source.field))
        val values = read(client, ids, source.field, cancelled) ?: return
        synchronized(this) {
            held = source
            filledAt = now
            refreshedAt = now
            byValue.clear()
            valueOf.clear()
            for ((id, value) in values) put(id, value)
        }
    }

    /**
     * Reads the notes changed in the last day (Anki's "edited:1") and holds them as they are
     * now. A source it is not filled for is filled instead. Throws [AnkiException].
     */
    fun refresh(client: AnkiClient, source: Source, now: Long, cancelled: () -> Boolean = { false }) {
        if (synchronized(this) { held != source }) {
            fill(client, source, now, cancelled)
            return
        }
        val ids = client.findNotesWhere(AnkiSearch.hasField(source.scope, source.field) + " edited:1")
        val values = read(client, ids, source.field, cancelled) ?: return
        synchronized(this) {
            if (held != source) return
            refreshedAt = now
            for ((id, value) in values) put(id, value)
        }
    }

    /** What Anki said it has for [value]: no notes, or these. */
    @Synchronized
    fun learn(source: Source, value: String, notes: List<Long>) {
        if (held != source) return
        val key = normal(value)
        // Notes that Anki no longer gives for this value were deleted or changed.
        byValue[key]?.toList()?.forEach { if (it !in notes) drop(it) }
        for (id in notes) put(id, key)
    }

    /** A note that was just added with [value]. */
    @Synchronized
    fun add(source: Source, value: String, note: Long) {
        if (held == source) put(note, normal(value))
    }

    /** Keeps what it holds in [file]. False when that failed, or when it holds nothing yet. */
    fun save(file: File): Boolean {
        val text = synchronized(this) {
            val source = held ?: return false
            val notes = JSONObject()
            for ((id, value) in valueOf) notes.put(id.toString(), value)
            JSONObject()
                .put("version", FILE_VERSION)
                .put("anki", source.anki)
                .put("scope", source.scope ?: JSONObject.NULL)
                .put("field", source.field)
                .put("filled_at", filledAt)
                .put("refreshed_at", refreshedAt)
                .put("notes", notes)
                .toString()
        }
        return runCatching {
            file.parentFile?.mkdirs()
            val temporary = File(file.path + ".new")
            temporary.writeText(text)
            temporary.renameTo(file) || run { file.delete(); temporary.renameTo(file) }
        }.getOrDefault(false)
    }

    /** Reads back what [save] kept. False when that failed; a file of another form is left. */
    fun load(file: File): Boolean {
        val root = runCatching { JSONObject(file.readText()) }.getOrNull() ?: return false
        if (root.optInt("version") != FILE_VERSION) return false
        val notes = root.optJSONObject("notes") ?: return false
        synchronized(this) {
            held = Source(
                root.optString("anki"),
                if (root.isNull("scope")) null else root.optString("scope"),
                root.optString("field")
            )
            filledAt = root.optLong("filled_at")
            refreshedAt = root.optLong("refreshed_at")
            byValue.clear()
            valueOf.clear()
            for (id in notes.keys()) put(id.toLong(), notes.optString(id))
        }
        return true
    }

    @get:Synchronized
    val size: Int get() = valueOf.size

    private fun put(id: Long, value: String) {
        drop(id)
        valueOf[id] = value
        val notes = byValue.getOrPut(value) { ArrayList() }
        if (id !in notes) notes.add(id)
    }

    private fun drop(id: Long) {
        val old = valueOf.remove(id) ?: return
        val notes = byValue[old] ?: return
        notes.remove(id)
        if (notes.isEmpty()) byValue.remove(old)
    }

    /** The values of [field] of [ids], read a page at a time; null when called off. */
    private fun read(client: AnkiClient, ids: List<Long>, field: String, cancelled: () -> Boolean): Map<Long, String>? {
        val out = HashMap<Long, String>()
        for (page in ids.chunked(PAGE_SIZE)) {
            if (cancelled()) return null
            for ((id, value) in client.fieldOf(page, field)) out[id] = normal(value)
        }
        return out
    }

    companion object {
        const val PAGE_SIZE = 200
        const val REFRESH_AFTER_MS = 3 * 60 * 1000L
        const val REFILL_AFTER_MS = 30 * 60 * 1000L
        private const val FILE_VERSION = 1
        private val MARKUP = Regex("<[^>]*>")

        /** A field's value as it is compared: without markup, spaces at its ends or case. */
        fun normal(value: String): String = value.replace(MARKUP, "")
            .replace("&nbsp;", " ").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&")
            .trim().lowercase()
    }
}
