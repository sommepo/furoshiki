// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.Deflater
import java.util.zip.Inflater

/** An installed dictionary. Lower [priority] is shown first. */
data class DictionaryInfo(
    val id: Long,
    val title: String,
    val revision: String,
    val author: String,
    val description: String,
    val frequencyMode: String,
    val priority: Int,
    val enabled: Boolean,
    val termCount: Long,
    val metaCount: Long,
    /**
     * The games it is for (Furoshiki's game IDs, from `furoshikiScope` in its index.json): a
     * names dictionary is used only while one of them runs. Empty: every game.
     */
    val scope: List<String> = emptyList(),
    /** Characters of its kanji banks (the engine's only). */
    val kanjiCount: Long = 0
)

/** One dictionary row for a term. [glossaryJson] is the entry's glossary array as JSON. */
/**
 * The pictures a glossary names: every "path" value in it, in order, once each. Only a
 * structured-content img and an "image" item have one; a path no picture has finds nothing.
 * The text is searched rather than parsed: a glossary is often long, and most have none.
 */
internal fun pathsIn(glossaryJson: String): List<String> {
    val key = "\"path\""
    val out = LinkedHashSet<String>()
    var at = glossaryJson.indexOf(key)
    while (at >= 0) {
        var p = at + key.length
        while (p < glossaryJson.length && glossaryJson[p].isWhitespace()) p++
        if (p >= glossaryJson.length || glossaryJson[p] != ':') { at = glossaryJson.indexOf(key, p); continue }
        p++
        while (p < glossaryJson.length && glossaryJson[p].isWhitespace()) p++
        if (p >= glossaryJson.length || glossaryJson[p] != '"') { at = glossaryJson.indexOf(key, p); continue }
        val begin = p
        var escaped = false
        p++
        while (p < glossaryJson.length && glossaryJson[p] != '"') {
            if (glossaryJson[p] == '\\') { escaped = true; p++ }
            p++
        }
        if (p >= glossaryJson.length) break
        val path = if (escaped) {
            runCatching { org.json.JSONArray("[" + glossaryJson.substring(begin, p + 1) + "]").getString(0) }.getOrDefault("")
        } else {
            glossaryJson.substring(begin + 1, p)
        }
        if (path.isNotEmpty()) out.add(path)
        at = glossaryJson.indexOf(key, p + 1)
    }
    return out.toList()
}

/**
 * A glossary's pictures by path, as data addresses, found and read only when asked: a lookup with
 * many dictionaries keeps about a hundred definitions, and reading every picture they name took a
 * third of a hover on the Y700, where the popup needs only those its page has not had.
 */
internal class LazyPictures(private val glossaryJson: String, private val read: (String) -> String?) : AbstractMap<String, String>() {
    private val paths: List<String> by lazy { pathsIn(glossaryJson) }
    private val made = HashMap<String, String?>()

    override val keys: Set<String> get() = LinkedHashSet(paths)
    override val size: Int get() = paths.size
    override fun isEmpty(): Boolean = paths.isEmpty()
    override fun containsKey(key: String): Boolean = key in paths
    override fun get(key: String): String? = if (key !in paths) null else synchronized(made) {
        if (made.containsKey(key)) made[key] else read(key).also { made[key] = it }
    }
    // Itself only: an entry compared or put in a hash set must not read every picture.
    override fun equals(other: Any?): Boolean = this === other
    override fun hashCode(): Int = System.identityHashCode(this)

    override val entries: Set<Map.Entry<String, String>>
        get() = paths.mapNotNull { path -> get(path)?.let { java.util.AbstractMap.SimpleImmutableEntry(path, it) } }.toSet()
}

data class TermEntry(
    val dictionaryId: Long,
    val dictionary: String,
    val priority: Int,
    val expression: String,
    val reading: String,
    val definitionTags: String,
    val rules: String,
    val score: Int,
    val glossaryJson: String,
    val sequence: Long,
    val termTags: String,
    /** From a dictionary for the running game only (a names dictionary): ranked first. */
    val scoped: Boolean = false,
    /**
     * The pictures its glossary shows, by their path in the dictionary's zip, as data addresses
     * (data:image/...;base64,...). Empty for most entries.
     */
    val media: Map<String, String> = emptyMap(),
    /** Its row in the terms table. */
    val id: Long = 0
)

/**
 * A character of a kanji dictionary (kanji_bank): its readings, meanings and stats as the
 * dictionary gives them (KANJIDIC: strokes, grade, jlpt, freq...). [statsJson] is a JSON object.
 */
data class KanjiEntry(
    val dictionaryId: Long,
    val dictionary: String,
    val character: String,
    val onyomi: List<String>,
    val kunyomi: List<String>,
    val tags: String,
    val meanings: List<String>,
    val statsJson: String
)

/** A frequency, pitch or other "meta" row for a term. [data] is its JSON value. */
data class TermMeta(
    val dictionary: String,
    val frequencyMode: String,
    val expression: String,
    val mode: String,
    val data: String
)

/**
 * Dictionaries imported from Yomitan-format archives, stored for lookup by expression or reading.
 *
 * In Furoshiki (whose native library has the dictionary engine, docs/dictionary-engine.md) they
 * are in the engine's library, [library]: each dictionary's files, mapped, and one index of them
 * all. Elsewhere (tests on a computer, a named database), and until the dictionaries of an
 * earlier SQLite file are converted, in SQLite, with glossaries kept compressed.
 */
class DictionaryDatabase(
    context: Context,
    private val name: String? = DATABASE_NAME,
    library: File? = if (name == DATABASE_NAME) File(context.filesDir, LIBRARY_FOLDER) else null
) : SQLiteOpenHelper(context, name, null, VERSION) {

    /** The engine's library, or 0 (SQLite only). */
    private val engine: Long = if (library != null && DictionaryNative.available) {
        runCatching { DictionaryNative.open(library.path) }.getOrElse {
            org.readers.app.reader.ReaderLog.warning("[Reader] The dictionary library could not be opened: ${it.message}")
            0L
        }
    } else 0L

    /** The SQLite file the dictionaries were in before the engine: used until it is converted. */
    private val legacy: File? = if (engine != 0L && name != null) context.getDatabasePath(name) else null

    /** Definitions lately read from the engine, by (dictionary shl 32 or entry), about 2 M characters. */
    private val definitions = object : android.util.LruCache<Long, String>(2 * 1024 * 1024) {
        override fun sizeOf(key: Long, value: String) = value.length.coerceAtLeast(1)
    }

    /** For measuring (the app's DictionaryTest): nanoseconds reading definitions, their pictures; definitions read from the engine, definitions given. */
    @Volatile
    var steps: LongArray? = null

    /** Whether lookups and changes go to the engine now (the SQLite file is gone, or never was). */
    val onEngine: Boolean get() = engine != 0L && (legacy == null || !legacy.exists())

    @Volatile
    private var wasOnEngine: Boolean = onEngine

    /**
     * Checks [onEngine] and clears in-memory caches once if another process finished converting
     * SQLite into the engine (IDs restart at 1 on the engine, and SQLite's [stylesKnown] would be stale).
     */
    private fun useEngine(): Boolean {
        val now = onEngine
        if (now && !wasOnEngine) {
            wasOnEngine = true
            changed()
        }
        return now
    }

    /** The engine's library, for the importer (0 when [onEngine] is false). */
    internal val engineHandle: Long get() = if (useEngine()) engine else 0L

    private val appContext = context.applicationContext

    /**
     * Whether an earlier SQLite file's dictionaries are still to be moved into the engine's
     * library. They are moved only when asked ([startConversion]), from the app's home screen, so
     * never from the game's process; until then, and while they move, the SQLite file is used.
     */
    val conversionNeeded: Boolean get() = engine != 0L && legacy != null && legacy.exists()

    /** Starts moving them (DictionaryConversion), once a process; its progress is DictionaryConversion.state. */
    fun startConversion() {
        if (conversionNeeded) DictionaryConversion.start(this, appContext, engine)
    }

    /** The move given up and the old file's dictionaries removed, to be imported again (DictionaryConversion.abandon). Returns -1 if already finished. */
    fun abandonConversion(): Int = if (conversionNeeded) DictionaryConversion.abandon(this, appContext, engine) else -1

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE dictionaries (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL UNIQUE,
                revision TEXT NOT NULL,
                author TEXT NOT NULL,
                description TEXT NOT NULL,
                frequency_mode TEXT NOT NULL,
                priority INTEGER NOT NULL,
                enabled INTEGER NOT NULL DEFAULT 1,
                terms INTEGER NOT NULL DEFAULT 0,
                metas INTEGER NOT NULL DEFAULT 0,
                scope TEXT NOT NULL DEFAULT '',
                styles TEXT NOT NULL DEFAULT ''
            )"""
        )
        db.execSQL(MEDIA_TABLE)
        db.execSQL(
            """CREATE TABLE terms (
                id INTEGER PRIMARY KEY,
                dict INTEGER NOT NULL,
                expression TEXT NOT NULL,
                reading TEXT NOT NULL,
                def_tags TEXT NOT NULL,
                rules TEXT NOT NULL,
                score INTEGER NOT NULL,
                glossary BLOB NOT NULL,
                sequence INTEGER NOT NULL,
                term_tags TEXT NOT NULL
            )"""
        )
        db.execSQL("CREATE INDEX terms_expression ON terms(expression)")
        db.execSQL("CREATE INDEX terms_reading ON terms(reading)")
        db.execSQL(
            """CREATE TABLE term_meta (
                id INTEGER PRIMARY KEY,
                dict INTEGER NOT NULL,
                expression TEXT NOT NULL,
                mode TEXT NOT NULL,
                data TEXT NOT NULL
            )"""
        )
        db.execSQL("CREATE INDEX term_meta_expression ON term_meta(expression)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        // Version 2: the games a dictionary is for, "" for every game (the desktop's
        // DictionaryDatabase makes the same change at the same version).
        if (oldVersion < 2) db.execSQL("ALTER TABLE dictionaries ADD COLUMN scope TEXT NOT NULL DEFAULT ''")
        // Version 3: a dictionary's own stylesheet (styles.css) and the pictures in its zip, which
        // its structured content shows by their paths (the desktop's makes the same change).
        if (oldVersion < 3) {
            db.execSQL("ALTER TABLE dictionaries ADD COLUMN styles TEXT NOT NULL DEFAULT ''")
            db.execSQL(MEDIA_TABLE)
        }
    }

    @Volatile
    private var stylesKnown: Map<String, String>? = null

    /**
     * Rebuilds the engine's index of every dictionary's keys, once a batch of imports is over (a
     * dictionary it does not cover is asked on its own, a little slower). Nothing on SQLite.
     */
    fun compact() {
        if (useEngine()) DictionaryNative.compact(engine)
    }

    /** Called after a dictionary came in, went or was switched: what is kept of the list is stale. */
    fun changed() {
        stylesKnown = null
        pictures.evictAll()
        synchronized(definitions) { definitions.evictAll() }
    }

    /**
     * Pictures lately shown, as data addresses, by dictionary and path: a dictionary's few icons
     * appear in most of its entries, and each was read and encoded again every time.
     */
    private val pictures = object : android.util.LruCache<String, String>(8 * 1024 * 1024) {
        override fun sizeOf(key: String, value: String) = value.length.coerceAtLeast(1) // characters
    }

    /** The stylesheets (styles.css) of the enabled dictionaries that have one, by title. */
    fun styles(): Map<String, String> {
        if (useEngine()) {
            // Not kept: another process (the app's dictionaries page) may switch one.
            val out = HashMap<String, String>()
            DictionaryNative.records(DictionaryNative.styles(engine)).forEach { if (it.size >= 2) out[it[0]] = it[1] }
            return out
        }
        return stylesKnown ?: HashMap<String, String>().also { out ->
            readableDatabase.rawQuery("SELECT title, styles FROM dictionaries WHERE enabled = 1 AND styles != ''", emptyArray())
                .use { c -> while (c.moveToNext()) out[c.getString(0)] = c.getString(1) }
            stylesKnown = out
        }
    }

    /**
     * The engine's picture of [dictionaryId] at [path] as a data address, kept a while (a
     * dictionary's few icons appear in most of its entries).
     */
    private fun picture(dictionaryId: Long, path: String): String? {
        val key = "$dictionaryId\n$path"
        pictures.get(key)?.let { return it }
        val bytes = DictionaryNative.media(engine, dictionaryId, path) ?: return null
        val address = "data:" + mimeOf(path) + ";base64," + android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP)
        pictures.put(key, address)
        return address
    }

    /** The pictures a glossary names ("path"), as data addresses. */
    private fun mediaOf(db: SQLiteDatabase?, dictionaryId: Long, glossaryJson: String): Map<String, String> {
        val paths = pathsIn(glossaryJson)
        if (paths.isEmpty()) return emptyMap()
        val out = HashMap<String, String>()
        for (path in paths) {
            val key = "$dictionaryId\n$path"
            pictures.get(key)?.let { out[path] = it; continue }
            if (db == null) {
                DictionaryNative.media(engine, dictionaryId, path)?.let { bytes ->
                    val address = "data:" + mimeOf(path) + ";base64," +
                        android.util.Base64.encodeToString(bytes, android.util.Base64.NO_WRAP)
                    out[path] = address
                    pictures.put(key, address)
                }
                continue
            }
            db.rawQuery("SELECT data FROM media WHERE dict = ? AND path = ?", arrayOf(dictionaryId.toString(), path)).use { c ->
                if (c.moveToFirst()) {
                    val address = "data:" + mimeOf(path) + ";base64," +
                        android.util.Base64.encodeToString(c.getBlob(0), android.util.Base64.NO_WRAP)
                    out[path] = address
                    pictures.put(key, address)
                }
            }
        }
        return out
    }

    fun dictionaries(): List<DictionaryInfo> {
        if (useEngine()) {
            return DictionaryNative.records(DictionaryNative.dictionaries(engine)).filter { it.size >= 12 }.map { f ->
                DictionaryInfo(
                    f[0].toLong(), f[1], f[2], f[3], f[4], f[5], f[6].toInt(), f[7] == "1",
                    f[8].toLong(), f[9].toLong(), scopeList(f[10]), f[11].toLong()
                )
            }
        }
        val out = ArrayList<DictionaryInfo>()
        readableDatabase.rawQuery(
            "SELECT id, title, revision, author, description, frequency_mode, priority, enabled, " +
                "terms, metas, scope FROM dictionaries ORDER BY priority, id",
            emptyArray()
        ).use { c ->
            while (c.moveToNext()) {
                out.add(
                    DictionaryInfo(
                        c.getLong(0), c.getString(1), c.getString(2), c.getString(3),
                        c.getString(4), c.getString(5), c.getInt(6), c.getInt(7) != 0,
                        c.getLong(8), c.getLong(9), scopeList(c.getString(10))
                    )
                )
            }
        }
        return out
    }

    fun hasEnabledDictionaries(): Boolean = if (useEngine()) DictionaryNative.hasEnabled(engine) else readableDatabase.rawQuery(
        "SELECT 1 FROM dictionaries WHERE enabled = 1 LIMIT 1",
        emptyArray()
    ).use { it.moveToFirst() }

    fun setEnabled(id: Long, enabled: Boolean) = setEnabled(listOf(id), enabled)

    /** Many at once: one change of the library. */
    fun setEnabled(ids: List<Long>, enabled: Boolean) {
        if (useEngine()) {
            DictionaryNative.setEnabled(engine, ids.toLongArray(), enabled)
            changed()
            return
        }
        for (id in ids) writableDatabase.execSQL(
            "UPDATE dictionaries SET enabled = ? WHERE id = ?",
            arrayOf<Any>(if (enabled) 1 else 0, id)
        )
        changed()
    }

    /** Stores a new display order: the first id becomes the highest priority. */
    fun setOrder(ids: List<Long>) {
        if (useEngine()) {
            DictionaryNative.setOrder(engine, ids.toLongArray())
            changed()
            return
        }
        val db = writableDatabase
        db.beginTransaction()
        try {
            ids.forEachIndexed { index, id ->
                db.execSQL(
                    "UPDATE dictionaries SET priority = ? WHERE id = ?",
                    arrayOf<Any>(index, id)
                )
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
        }
    }

    fun delete(id: Long) = delete(listOf(id))

    /** Many at once: one change of the library, then each one's files. */
    fun delete(ids: List<Long>) {
        if (useEngine()) {
            DictionaryNative.delete(engine, ids.toLongArray())
            changed()
            return
        }
        for (id in ids) deleteSqlite(id)
    }

    private fun deleteSqlite(id: Long) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            db.delete("terms", "dict = ?", arrayOf(id.toString()))
            db.delete("term_meta", "dict = ?", arrayOf(id.toString()))
            db.delete("media", "dict = ?", arrayOf(id.toString()))
            db.delete("dictionaries", "id = ?", arrayOf(id.toString()))
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
        }
        changed()
    }

    /**
     * Term rows of enabled dictionaries whose expression or reading is one of [terms]: of the
     * dictionaries for every game, and of those scoped to [game] (a game ID) when one is given.
     */
    fun findTerms(terms: Collection<String>, game: String = "", withGlossaries: Boolean = true): List<TermEntry> {
        if (terms.isEmpty()) return emptyList()
        if (useEngine()) {
            return DictionaryNative.records(DictionaryNative.findTerms(engine, terms.toTypedArray(), game.trim()))
                .filter { it.size >= 12 }.map { f ->
                    val dictionary = f[0].toLong()
                    val entry = f[3].toLong()
                    val glossary = if (withGlossaries) DictionaryNative.definition(engine, dictionary, entry.toInt()) else ""
                    TermEntry(
                        dictionaryId = dictionary, dictionary = f[1], priority = f[2].toInt(),
                        expression = f[4], reading = f[5], definitionTags = f[6], rules = f[7],
                        score = f[8].toInt(), glossaryJson = glossary, sequence = f[9].toLong(), termTags = f[10],
                        scoped = f[11] == "1",
                        media = if (withGlossaries) LazyPictures(glossary) { path -> picture(dictionary, path) } else emptyMap(),
                        // The dictionary and its entry, for withGlossaries.
                        id = (dictionary shl 32) or (entry and 0xFFFFFFFFL)
                    )
                }
        }
        val out = ArrayList<TermEntry>()
        val seen = HashSet<Long>()
        val db = readableDatabase
        for (chunk in terms.chunked(MAX_PARAMETERS)) {
            val marks = chunk.joinToString(",") { "?" }
            // No game: "||", which no scope holds.
            val args = (chunk + "|${game.trim().uppercase()}|").toTypedArray()
            for (column in arrayOf("expression", "reading")) {
                db.rawQuery(
                    """SELECT t.id, t.dict, d.title, d.priority, t.expression, t.reading,
                              t.def_tags, t.rules, t.score, ${if (withGlossaries) "t.glossary" else "NULL"}, t.sequence, t.term_tags,
                              d.scope != ''
                       FROM terms t JOIN dictionaries d ON d.id = t.dict
                       WHERE t.$column IN ($marks) AND d.enabled = 1
                         AND (d.scope = '' OR instr(d.scope, ?) > 0)""",
                    args
                ).use { c ->
                    while (c.moveToNext()) {
                        if (!seen.add(c.getLong(0))) continue
                        val glossary = if (withGlossaries) unpack(c.getBlob(9)) else ""
                        out.add(
                            TermEntry(
                                dictionaryId = c.getLong(1),
                                dictionary = c.getString(2),
                                priority = c.getInt(3),
                                expression = c.getString(4),
                                reading = c.getString(5),
                                definitionTags = c.getString(6),
                                rules = c.getString(7),
                                score = c.getInt(8),
                                glossaryJson = glossary,
                                sequence = c.getLong(10),
                                termTags = c.getString(11),
                                scoped = c.getInt(12) != 0,
                                media = if (withGlossaries) mediaOf(db, c.getLong(1), glossary) else emptyMap(),
                                id = c.getLong(0)
                            )
                        )
                    }
                }
            }
        }
        return out
    }

    /**
     * [entries] found without their glossaries, with them (and their pictures). A lookup ranks
     * every row its text matches, in every dictionary, by their short fields alone, and reads
     * the glossaries of those it keeps: most of a hover's time was in unpacking the others.
     */
    fun withGlossaries(entries: List<TermEntry>): List<TermEntry> {
        if (entries.isEmpty()) return entries
        if (useEngine()) {
            val clock = steps
            val started = System.nanoTime()
            // Those not read lately, all in one call (a call each took most of a hover's time with
            // many dictionaries); the same words come back as a line is read.
            val known = HashMap<Long, String>()
            synchronized(definitions) { for (e in entries) definitions.get(e.id)?.let { known[e.id] = it } }
            val missing = entries.map { it.id }.filter { it !in known }.distinct()
            if (missing.isNotEmpty()) {
                val texts = String(DictionaryNative.definitions(engine, missing.toLongArray()), Charsets.UTF_8).split('\u001e')
                synchronized(definitions) {
                    missing.forEachIndexed { index, id ->
                        val text = texts.getOrElse(index) { "[]" }
                        known[id] = text
                        definitions.put(id, text)
                    }
                }
            }
            val read = System.nanoTime()
            return entries.map { e ->
                val glossary = known[e.id] ?: "[]"
                e.copy(glossaryJson = glossary, media = LazyPictures(glossary) { path -> picture(e.dictionaryId, path) })
            }.also {
                if (clock != null) {
                    clock[0] += read - started
                    clock[1] += System.nanoTime() - read
                    clock[2] += missing.size.toLong()
                    clock[3] += entries.size.toLong()
                }
            }
        }
        val db = readableDatabase
        val read = HashMap<Long, Pair<String, Map<String, String>>>()
        for (chunk in entries.map { it.id }.distinct().chunked(MAX_PARAMETERS)) {
            db.rawQuery(
                "SELECT id, dict, glossary FROM terms WHERE id IN (${chunk.joinToString(",") { "?" }})",
                chunk.map { it.toString() }.toTypedArray()
            ).use { c ->
                while (c.moveToNext()) {
                    val glossary = unpack(c.getBlob(2))
                    read[c.getLong(0)] = glossary to mediaOf(db, c.getLong(1), glossary)
                }
            }
        }
        return entries.map { e -> read[e.id]?.let { (g, m) -> e.copy(glossaryJson = g, media = m) } ?: e }
    }

    /** A character in the enabled kanji dictionaries (the engine's; none on SQLite). */
    fun findKanji(character: String): List<KanjiEntry> {
        if (!useEngine() || character.isEmpty()) return emptyList()
        return DictionaryNative.records(DictionaryNative.findKanji(engine, character)).filter { it.size >= 7 }.map { f ->
            val meanings = runCatching { org.json.JSONArray(f[5]).let { a -> (0 until a.length()).map { a.optString(it) } } }
                .getOrDefault(emptyList())
            KanjiEntry(
                f[0].toLong(), f[1], character, f[2].split(' ').filter { it.isNotEmpty() },
                f[3].split(' ').filter { it.isNotEmpty() }, f[4], meanings, f[6].ifEmpty { "{}" }
            )
        }
    }

    /** A character's frequencies in the enabled dictionaries (kanji_meta_bank). */
    fun findKanjiMeta(character: String): List<TermMeta> {
        if (!useEngine() || character.isEmpty()) return emptyList()
        return DictionaryNative.records(DictionaryNative.findKanjiMeta(engine, character))
            .filter { it.size >= 5 }.map { f -> TermMeta(f[0], f[1], f[2], f[3], f[4]) }
    }

    /** Frequency and pitch rows of enabled dictionaries for the given expressions. */
    fun findMeta(expressions: Collection<String>): List<TermMeta> {
        if (expressions.isEmpty()) return emptyList()
        if (useEngine()) {
            return DictionaryNative.records(DictionaryNative.findMeta(engine, expressions.toTypedArray()))
                .filter { it.size >= 5 }.map { f -> TermMeta(f[0], f[1], f[2], f[3], f[4]) }
        }
        val out = ArrayList<TermMeta>()
        val db = readableDatabase
        for (chunk in expressions.chunked(MAX_PARAMETERS)) {
            val marks = chunk.joinToString(",") { "?" }
            db.rawQuery(
                """SELECT d.title, d.frequency_mode, m.expression, m.mode, m.data
                   FROM term_meta m JOIN dictionaries d ON d.id = m.dict
                   WHERE m.expression IN ($marks) AND d.enabled = 1
                   ORDER BY d.priority, d.id""",
                chunk.toTypedArray()
            ).use { c ->
                while (c.moveToNext()) {
                    out.add(
                        TermMeta(
                            c.getString(0), c.getString(1), c.getString(2), c.getString(3),
                            c.getString(4)
                        )
                    )
                }
            }
        }
        return out
    }

    companion object {
        const val DATABASE_NAME = "reader_dict.db"
        /** The engine's library, in the app's files. */
        const val LIBRARY_FOLDER = "dictionaries"
        private const val VERSION = 3
        private const val MEDIA_TABLE = "CREATE TABLE IF NOT EXISTS media (dict INTEGER NOT NULL, path TEXT NOT NULL, " +
            "data BLOB NOT NULL, PRIMARY KEY(dict, path)) WITHOUT ROWID"

        fun mimeOf(path: String): String = when (val extension = path.substringAfterLast('.').lowercase()) {
            "svg" -> "image/svg+xml"
            "jpg", "jpeg" -> "image/jpeg"
            "ico" -> "image/x-icon"
            else -> "image/$extension"
        }
        private const val MAX_PARAMETERS = 400
        private const val PACK_RAW: Byte = 0

        /** A dictionary's scope as stored: "|ID|ID|" (upper case), or "" for every game. */
        fun scopeText(games: Collection<String>): String {
            val ids = games.map { it.trim().uppercase() }.filter { it.isNotEmpty() && '|' !in it }.distinct()
            return if (ids.isEmpty()) "" else ids.joinToString("|", "|", "|")
        }

        fun scopeList(text: String?): List<String> = text.orEmpty().split('|').filter { it.isNotEmpty() }

        /** Compresses a glossary for storage; short ones are stored as they are. */
        fun pack(json: String): ByteArray = Packer().use { it.pack(json.toByteArray(Charsets.UTF_8), 0, -1) }

        fun unpack(blob: ByteArray): String {
            if (blob.isEmpty()) return "[]"
            if (blob[0] == PACK_RAW) return String(blob, 1, blob.size - 1, Charsets.UTF_8)
            val inflater = Inflater()
            try {
                inflater.setInput(blob, 1, blob.size - 1)
                val out = ByteArrayOutputStream(blob.size * 4)
                val buffer = ByteArray(4096)
                while (!inflater.finished()) {
                    val n = inflater.inflate(buffer)
                    if (n == 0 && (inflater.needsInput() || inflater.needsDictionary())) break
                    out.write(buffer, 0, n)
                }
                return out.toString("UTF-8")
            } finally {
                inflater.end()
            }
        }
    }
}

/**
 * Compresses glossaries for storage, as [DictionaryDatabase.pack] does, with one compressor kept
 * for many (an importer's thread makes one): a compressor made for every glossary costs more
 * than the compressing.
 */
class Packer : AutoCloseable {
    private val deflater = Deflater(Deflater.BEST_SPEED)
    private var buffer = ByteArray(16384)

    /** [raw]'s bytes from [from] to [to] (-1: its end), UTF-8 JSON. */
    fun pack(raw: ByteArray, from: Int, to: Int): ByteArray {
        val end = if (to < 0) raw.size else to
        val length = end - from
        if (length < 96) {
            val out = ByteArray(length + 1)
            System.arraycopy(raw, from, out, 1, length)
            return out // out[0] is 0: stored as it is
        }
        deflater.reset()
        deflater.setInput(raw, from, length)
        deflater.finish()
        if (buffer.size < length + 64) buffer = ByteArray(length + 64)
        var used = 0
        while (!deflater.finished()) {
            if (used == buffer.size) buffer = buffer.copyOf(buffer.size * 2)
            used += deflater.deflate(buffer, used, buffer.size - used)
        }
        val out = ByteArray(used + 1)
        out[0] = 1
        System.arraycopy(buffer, 0, out, 1, used)
        return out
    }

    override fun close() = deflater.end()
}
