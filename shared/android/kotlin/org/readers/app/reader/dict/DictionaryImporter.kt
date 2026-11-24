// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import org.json.JSONArray
import org.json.JSONObject

/**
 * Imports a dictionary archive in the Yomitan format (index.json plus term_bank_N.json and
 * term_meta_bank_N.json files) into a [DictionaryDatabase], with its styles.css and the pictures
 * in it. Kanji and tag banks are skipped.
 *
 * A bank is read as bytes and its rows found in them (the desktop's importer does the same): only
 * the short fields are decoded, and a glossary or a meta value is stored as the bytes it was
 * written as. Banks are read and compressed on a few threads while one thread writes them in their
 * order, at most a few ahead, so memory stays bounded.
 */
class DictionaryImporter(private val database: DictionaryDatabase) {

    /** Why an archive was not imported; [alreadyThere] when the dictionary is installed already. */
    class ImportException(message: String, val alreadyThere: Boolean = false) : Exception(message)

    data class Result(val title: String, val terms: Long, val metas: Long, val media: Long = 0, val kanji: Long = 0)

    /** For the tests: true or false uses SQLite's JSON functions or not; null asks SQLite whether it has them. */
    internal var jsonFunctions: Boolean? = null

    /** For the tests: the bank text held at once, in bytes (else a quarter of the heap). */
    internal var heldBytes: Long? = null

    private class TermRow(
        val expression: String, val reading: String, val definitionTags: String, val rules: String,
        val score: Long, val glossary: ByteArray, val sequence: Long, val termTags: String
    )

    private class MetaRow(val expression: String, val mode: String, val data: String)

    /**
     * Rows ready for SQLite's JSON functions (see [import]): a chunk's short fields as one JSON
     * array of rows, with the terms' packed glossaries beside it in their order.
     */
    private class Chunk(val json: String, val count: Int, val glossaries: List<ByteArray>)

    private class Bank(
        terms: List<TermRow> = emptyList(),
        metas: List<MetaRow> = emptyList(),
        chunks: List<Chunk> = emptyList()
    ) {
        // Let go of once written (drop), so that the next big bank is not read beside it.
        var terms = terms
            private set
        var metas = metas
            private set
        var chunks = chunks
            private set

        fun drop() {
            terms = emptyList()
            metas = emptyList()
            chunks = emptyList()
        }
    }

    /**
     * Imports [archive]. [progress] is called with the number of banks done, the total and the
     * dictionary title; returning false from [keepGoing] abandons the import and leaves the
     * database unchanged.
     */
    fun import(
        archive: File,
        progress: (done: Int, total: Int, title: String) -> Unit = { _, _, _ -> },
        keepGoing: () -> Boolean = { true }
    ): Result {
        // Each import's speed, in the app's log, for whoever looks into a slow one.
        val started = System.nanoTime()
        streamed = 0
        fun seconds() = "%.2f".format((System.nanoTime() - started) / 1e9)
        val heap = Runtime.getRuntime().maxMemory() / 1048576
        if (database.onEngine) return importOnEngine(archive, progress, keepGoing, started)
        try {
            val result = importNow(archive, progress, keepGoing)
            org.readers.app.reader.ReaderLog.info(
                "[Reader] Dictionary imported ${result.title}: ${result.terms} terms, ${result.metas} metas, " +
                    "${result.media} pictures in ${seconds()} s (${archive.name}, ${archive.length() / 1048576} MB; " +
                    "$usedThreads threads, $streamed banks read in pieces, heap $heap MB)"
            )
            return result
        } catch (e: Exception) {
            org.readers.app.reader.ReaderLog.warning("[Reader] Dictionary import of ${archive.name} failed after ${seconds()} s: ${e.message}")
            throw e
        }
    }

    /**
     * The import into the dictionary engine's library (docs/dictionary-engine.md): the engine reads
     * the zip itself, on several threads, and writes the dictionary's files; it joins the library
     * when whole. The caller compacts the library once a batch is over (DictionaryDatabase.compact).
     */
    private fun importOnEngine(
        archive: File,
        progress: (done: Int, total: Int, title: String) -> Unit,
        keepGoing: () -> Boolean,
        started: Long
    ): Result {
        // Its title, for the progress shown: index.json only.
        val shownTitle = runCatching {
            ZipFile(archive).use { zip ->
                val index = zip.entries().toList().filter { it.name == "index.json" || it.name.endsWith("/index.json") }
                    .minByOrNull { it.name.length }
                index?.let { e -> JSONObject(zip.getInputStream(e).use { it.readBytes().decodeToString() }).optString("title").trim() }
            }
        }.getOrNull().orEmpty().ifEmpty { archive.name }
        val threads = engineThreads.takeIf { it > 0 } ?: (Runtime.getRuntime().availableProcessors() - 1).coerceIn(1, 4)
        val told = object : DictionaryNative.Progress {
            override fun step(done: Int, total: Int) = progress(done, total, shownTitle)
            override fun keepGoing(): Boolean = keepGoing()
        }
        // The engine's threads take this thread's priority: one of Android's background threads
        // (the app's setup, a worker) keeps them on the slow cores, where Jitendex took 5.7 s on
        // the Y700 instead of under 2. For the import, it is an ordinary thread's.
        val handle = database.engineHandle
        if (handle == 0L) throw ImportException("Dictionary engine is not available")
        val before = android.os.Process.getThreadPriority(android.os.Process.myTid())
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DEFAULT)
        val f = try {
            DictionaryNative.records(
                DictionaryNative.importZip(handle, archive.path, told, threads, engineAheadMb.takeIf { it > 0 } ?: ENGINE_AHEAD_MB)
            ).firstOrNull().orEmpty()
        } finally {
            android.os.Process.setThreadPriority(before)
        }
        database.changed()
        val seconds = "%.2f".format((System.nanoTime() - started) / 1e9)
        if (f.size < 10 || f[0] != "1") {
            val error = f.getOrNull(3).orEmpty().ifEmpty { "The dictionary engine returned no response" }
            org.readers.app.reader.ReaderLog.warning("[Reader] Dictionary import of ${archive.name} failed after $seconds s: $error")
            throw ImportException(error, alreadyThere = f.getOrNull(1) == "1")
        }
        val result = Result(f[4], f[6].toLong(), f[7].toLong(), f[8].toLong(), f[9].toLong())
        org.readers.app.reader.ReaderLog.info(
            "[Reader] Dictionary imported ${result.title}: ${result.terms} terms, ${result.metas} metas, " +
                "${result.media} pictures, ${result.kanji} kanji in $seconds s (${archive.name}, ${archive.length() / 1048576} MB; " +
                "dictionary engine, $threads threads)"
        )
        return result
    }

    /** For the log: banks too big to hold, read in pieces; the threads that read banks. */
    private var streamed = 0
    private var usedThreads = 0

    private fun importNow(
        archive: File,
        progress: (done: Int, total: Int, title: String) -> Unit,
        keepGoing: () -> Boolean
    ): Result {
        ZipFile(archive).use { zip ->
            val entries = zip.entries().toList().filter { !it.isDirectory }
            val indexEntry = entries
                .filter { it.name == "index.json" || it.name.endsWith("/index.json") }
                .minByOrNull { it.name.length }
                ?: throw ImportException("Not a dictionary archive: index.json is missing")
            val prefix = indexEntry.name.removeSuffix("index.json")
            val index = try {
                JSONObject(zip.getInputStream(indexEntry).use { it.readBytes().decodeToString() })
            } catch (e: Exception) {
                throw ImportException("index.json cannot be read: ${e.message}")
            }
            val title = index.optString("title").trim()
            if (title.isEmpty()) throw ImportException("index.json has no title")
            val format = index.optInt("format", index.optInt("version", 3))
            if (format != 1 && format != 3) {
                throw ImportException("Unsupported dictionary format $format")
            }

            fun banks(kind: String): List<ZipEntry> {
                val pattern = Regex(Regex.escape(prefix) + kind + "_(\\d+)\\.json")
                return entries.mapNotNull { e ->
                    pattern.matchEntire(e.name)?.let { m -> m.groupValues[1].toInt() to e }
                }.sortedBy { it.first }.map { it.second }
            }
            val termBanks = banks("term_bank")
            val metaBanks = banks("term_meta_bank")
            if (termBanks.isEmpty() && metaBanks.isEmpty()) {
                throw ImportException("\"$title\" has no term or frequency data")
            }
            val total = termBanks.size + metaBanks.size
            // The dictionary's own stylesheet for its structured content, as Yomitan reads it.
            val styles = entries.firstOrNull { it.name == prefix + "styles.css" }
                ?.let { e -> zip.getInputStream(e).use { it.readBytes().decodeToString() } }.orEmpty()
            val media = entries.filter { it.name.startsWith(prefix) && MEDIA.containsMatchIn(it.name) }

            val db = database.writableDatabase
            if (db.rawQuery("SELECT 1 FROM dictionaries WHERE title = ?", arrayOf(title))
                    .use { it.moveToFirst() }
            ) {
                throw ImportException("\"$title\" is already imported", alreadyThere = true)
            }

            var terms = 0L
            var metas = 0L
            var mediaCount = 0L
            val threads = (Runtime.getRuntime().availableProcessors() - 1).coerceIn(1, 4)
            usedThreads = threads
            val pool = Executors.newFixedThreadPool(threads)
            val packers = ThreadLocal.withInitial { Packer() }
            // Rows go into SQLite a chunk at a time through its JSON functions where it has them: a
            // value bound costs a call through JNI, which costs more than the row it writes (more
            // so in a debuggable build). Else each row's values are bound, many rows a statement.
            val withJson = jsonFunctions ?: runCatching {
                db.rawQuery("SELECT json_extract(json_quote('a'), '$')", emptyArray()).use { it.moveToFirst() && it.getString(0) == "a" }
            }.getOrDefault(false)
            val made = java.util.Collections.synchronizedList(ArrayList<Packer>())
            db.beginTransaction()
            try {
                val priority = db.rawQuery(
                    "SELECT COALESCE(MAX(priority), -1) + 1 FROM dictionaries",
                    emptyArray()
                ).use { if (it.moveToFirst()) it.getInt(0) else 0 }
                db.execSQL(
                    "INSERT INTO dictionaries(title, revision, author, description, " +
                        "frequency_mode, priority, scope, styles) VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
                    arrayOf<Any>(
                        title,
                        index.optString("revision"),
                        index.optString("author"),
                        index.optString("description"),
                        index.optString("frequencyMode"),
                        priority,
                        // Furoshiki's names dictionaries say which games they are for (docs/titles.md).
                        DictionaryDatabase.scopeText(
                            index.optJSONArray("furoshikiScope")?.let { games -> (0 until games.length()).map { games.optString(it) } }
                                .orEmpty()
                        ),
                        styles
                    )
                )
                val dictId = db.rawQuery("SELECT last_insert_rowid()", emptyArray())
                    .use { it.moveToFirst(); it.getLong(0) }

                // Rows go in many to a statement: each call into SQLite costs more on Android than
                // the row it writes.
                fun many(sql: String, values: String, count: Int) =
                    db.compileStatement(sql + List(count) { values }.joinToString(","))
                val termSql = "INSERT INTO terms(dict, expression, reading, def_tags, rules, score, " +
                    "glossary, sequence, term_tags) VALUES"
                val metaSql = "INSERT INTO term_meta(dict, expression, mode, data) VALUES"
                val insertTerm = many(termSql, "(?,?,?,?,?,?,?,?,?)", 1)
                val insertTerms = many(termSql, "(?,?,?,?,?,?,?,?,?)", TERMS_AT_ONCE)
                val insertMeta = many(metaSql, "(?,?,?,?)", 1)
                val insertMetas = many(metaSql, "(?,?,?,?)", METAS_AT_ONCE)
                val termsFromJson = HashMap<Int, android.database.sqlite.SQLiteStatement>()
                val metaFromJson = db.compileStatement(
                    "INSERT INTO term_meta(dict, expression, mode, data) SELECT ?1, json_extract(value, '$[0]'), " +
                        "coalesce(CAST(json_extract(value, '$[1]') AS TEXT), ''), " +
                        "CASE json_type(value, '$[2]') WHEN 'text' THEN json_quote(json_extract(value, '$[2]')) " +
                        "WHEN 'true' THEN 'true' WHEN 'false' THEN 'false' WHEN 'null' THEN 'null' " +
                        "ELSE CAST(json_extract(value, '$[2]') AS TEXT) END FROM json_each(?2)"
                )
                db.execSQL("PRAGMA cache_size=-65536") // 64 MB while the indexes are written all over the file
                var done = 0
                progress(done, total, title)

                val jobs = termBanks.map { it to true } + metaBanks.map { it to false }
                val ahead = ArrayDeque<Pair<Future<Bank>, Long>>()
                var next = 0
                // The bank text held at once (read ahead, and the bank being written) is bounded
                // by the app's heap: some dictionaries come in banks of 60 MB and more, and four
                // of them at once ran a 256 MB heap out (2026-11-23, 明鏡 on the Y700).
                val budget = heldBytes ?: (Runtime.getRuntime().maxMemory() / 8)
                var held = 0L
                fun sizeOf(entry: ZipEntry) = if (entry.size >= 0) entry.size else entry.compressedSize.coerceAtLeast(0) * 6
                fun fill() {
                    while (next < jobs.size && ahead.size < threads + 1) {
                        val size = sizeOf(jobs[next].first)
                        // A bank bigger than the budget is left for the writer to read itself.
                        if (withJson && size > budget) break
                        if (held != 0L && held + size > budget) break
                        val (entry, isTerms) = jobs[next++]
                        held += size
                        ahead.addLast(pool.submit<Bank> {
                            val packer = packers.get()
                            if (packer !in made) made.add(packer)
                            readBank(zip, entry, isTerms, format, packer, withJson)
                        } to size)
                    }
                }
                fun writeChunk(chunk: Chunk) {
                    if (chunk.glossaries.isEmpty()) {
                        metaFromJson.bindLong(1, dictId)
                        metaFromJson.bindString(2, chunk.json)
                        metaFromJson.executeInsert()
                        metas += chunk.count
                    } else {
                        val statement = termsFromJson.getOrPut(chunk.count) {
                            db.compileStatement(termsFromJsonSql(chunk.count))
                        }
                        statement.bindLong(1, dictId)
                        statement.bindString(2, chunk.json)
                        chunk.glossaries.forEachIndexed { k, glossary -> statement.bindBlob(k + 3, glossary) }
                        statement.executeInsert()
                        terms += chunk.count
                    }
                }
                fill()
                while (ahead.isNotEmpty() || next < jobs.size) {
                    if (!keepGoing()) throw ImportException("Import cancelled")
                    if (ahead.isEmpty()) {
                        // A bank bigger than the budget: read here and written a chunk at a time,
                        // so that only it and one chunk are held.
                        val (entry, isTerms) = jobs[next++]
                        val packer = packers.get().also { if (it !in made) made.add(it) }
                        streamed++
                        try {
                            zip.getInputStream(entry).use { input ->
                                inPieces(input) { piece ->
                                    chunks(piece, BankScanner(piece), isTerms, format, packer, ::writeChunk)
                                }
                            }
                        } catch (e: BankScanner.Malformed) {
                            throw ImportException("${entry.name} cannot be read: malformed JSON")
                        }
                        fill()
                        progress(++done, total, title)
                        continue
                    }
                    val (future, size) = ahead.removeFirst()
                    val bank = try {
                        future.get()
                    } catch (e: java.util.concurrent.ExecutionException) {
                        throw e.cause ?: e
                    }
                    fill()
                    bank.chunks.forEach(::writeChunk)
                    var at = 0
                    while (at < bank.terms.size) {
                        val count = if (bank.terms.size - at >= TERMS_AT_ONCE) TERMS_AT_ONCE else 1
                        val statement = if (count == 1) insertTerm else insertTerms
                        for (k in 0 until count) {
                            val row = bank.terms[at + k]
                            val base = k * 9
                            statement.bindLong(base + 1, dictId)
                            statement.bindString(base + 2, row.expression)
                            statement.bindString(base + 3, row.reading)
                            statement.bindString(base + 4, row.definitionTags)
                            statement.bindString(base + 5, row.rules)
                            statement.bindLong(base + 6, row.score)
                            statement.bindBlob(base + 7, row.glossary)
                            statement.bindLong(base + 8, row.sequence)
                            statement.bindString(base + 9, row.termTags)
                        }
                        statement.executeInsert()
                        at += count
                        terms += count
                    }
                    at = 0
                    while (at < bank.metas.size) {
                        val count = if (bank.metas.size - at >= METAS_AT_ONCE) METAS_AT_ONCE else 1
                        val statement = if (count == 1) insertMeta else insertMetas
                        for (k in 0 until count) {
                            val row = bank.metas[at + k]
                            val base = k * 4
                            statement.bindLong(base + 1, dictId)
                            statement.bindString(base + 2, row.expression)
                            statement.bindString(base + 3, row.mode)
                            statement.bindString(base + 4, row.data)
                        }
                        statement.executeInsert()
                        at += count
                        metas += count
                    }
                    bank.drop()
                    held -= size
                    fill()
                    progress(++done, total, title)
                }

                // The pictures its glossaries show, by their path in the zip.
                if (media.isNotEmpty()) {
                    val insertMedia = db.compileStatement("INSERT OR REPLACE INTO media(dict, path, data) VALUES(?, ?, ?)")
                    for (entry in media) {
                        if (!keepGoing()) throw ImportException("Import cancelled")
                        insertMedia.bindLong(1, dictId)
                        insertMedia.bindString(2, entry.name.removePrefix(prefix))
                        insertMedia.bindBlob(3, zip.getInputStream(entry).use { it.readBytes() })
                        insertMedia.executeInsert()
                        mediaCount++
                    }
                }

                db.execSQL(
                    "UPDATE dictionaries SET terms = ?, metas = ? WHERE id = ?",
                    arrayOf<Any>(terms, metas, dictId)
                )
                db.setTransactionSuccessful()
            } catch (e: ImportException) {
                throw e
            } catch (e: Exception) {
                throw ImportException("\"$title\" could not be imported: ${e.message}")
            } finally {
                runCatching { db.execSQL("PRAGMA cache_size=-2000") }
                pool.shutdownNow()
                pool.awaitTermination(30, java.util.concurrent.TimeUnit.SECONDS)
                made.forEach { it.close() }
                db.endTransaction()
                database.changed()
            }
            return Result(title, terms, metas, mediaCount)
        }
    }

    /** The terms of a chunk (a JSON array of [expression, reading, tags, rules, score, sequence, tags]). */
    private fun termsFromJsonSql(count: Int): String =
        "INSERT INTO terms(dict, expression, reading, def_tags, rules, score, glossary, sequence, term_tags) " +
            "SELECT ?1, e, CASE WHEN r = e THEN '' ELSE r END, d, u, s, CASE k " +
            (0 until count).joinToString(" ") { "WHEN $it THEN ?${it + 3}" } + " END, q, t FROM (SELECT key AS k, " +
            "json_extract(value, '$[0]') AS e, coalesce(CAST(json_extract(value, '$[1]') AS TEXT), '') AS r, " +
            "coalesce(CAST(json_extract(value, '$[2]') AS TEXT), '') AS d, " +
            "coalesce(CAST(json_extract(value, '$[3]') AS TEXT), '') AS u, " +
            "coalesce(CAST(json_extract(value, '$[4]') AS INTEGER), 0) AS s, " +
            "coalesce(CAST(json_extract(value, '$[5]') AS INTEGER), 0) AS q, " +
            "coalesce(CAST(json_extract(value, '$[6]') AS TEXT), '') AS t FROM json_each(?2))"

    private fun readBank(zip: ZipFile, entry: ZipEntry, terms: Boolean, format: Int, packer: Packer, withJson: Boolean): Bank {
        val bytes = readBytes(zip, entry)
        val termRows = ArrayList<TermRow>()
        val metaRows = ArrayList<MetaRow>()
        val scanner = BankScanner(bytes)
        if (withJson) {
            try {
                return Bank(chunks = chunks(bytes, scanner, terms, format, packer))
            } catch (e: BankScanner.Malformed) {
                throw ImportException("${entry.name} cannot be read: malformed JSON")
            }
        }
        try {
            scanner.rows { f ->
                if (terms) {
                    if (f.size < 10 || bytes[f[0]] != '"'.code.toByte()) return@rows
                    val expression = scanner.string(f[0], f[1])
                    val reading = scanner.text(f[2], f[3])
                    val glossary: ByteArray
                    var sequence = 0L
                    var termTags = ""
                    if (format >= 3) {
                        glossary = if (f.size > 11) packer.pack(bytes, f[10], f[11]) else packer.pack("[]".toByteArray(), 0, -1)
                        if (f.size > 13) sequence = scanner.number(f[12], f[13])
                        if (f.size > 15) termTags = scanner.text(f[14], f[15])
                    } else {
                        // Format 1: the glosses are the row's remaining values, kept as strings.
                        val glosses = JSONArray()
                        var i = 10
                        while (i + 1 < f.size) {
                            glosses.put(scanner.text(f[i], f[i + 1]))
                            i += 2
                        }
                        glossary = packer.pack(glosses.toString().toByteArray(), 0, -1)
                    }
                    termRows.add(
                        TermRow(
                            expression,
                            // An empty reading means the expression is its own reading.
                            if (reading == expression) "" else reading,
                            scanner.text(f[4], f[5]),
                            scanner.text(f[6], f[7]),
                            scanner.number(f[8], f[9]),
                            glossary, sequence, termTags
                        )
                    )
                } else {
                    if (f.size < 6 || bytes[f[0]] != '"'.code.toByte()) return@rows
                    metaRows.add(
                        MetaRow(
                            scanner.string(f[0], f[1]), scanner.text(f[2], f[3]),
                            String(bytes, f[4], f[5] - f[4], Charsets.UTF_8)
                        )
                    )
                }
            }
        } catch (e: BankScanner.Malformed) {
            throw ImportException("${entry.name} cannot be read: malformed JSON")
        }
        return Bank(termRows, metaRows)
    }

    /** A bank as chunks for SQLite's JSON functions: each row's short fields as their own JSON. */
    private fun chunks(bytes: ByteArray, scanner: BankScanner, terms: Boolean, format: Int, packer: Packer): List<Chunk> {
        val out = ArrayList<Chunk>()
        chunks(bytes, scanner, terms, format, packer) { out.add(it) }
        return out
    }

    /** The chunks of a bank, each to [emit] as soon as it is made. */
    private fun chunks(bytes: ByteArray, scanner: BankScanner, terms: Boolean, format: Int, packer: Packer, emit: (Chunk) -> Unit) {
        val json = StringBuilder()
        val glossaries = ArrayList<ByteArray>()
        var count = 0
        val most = if (terms) TERMS_AT_ONCE else META_CHUNK
        fun slice(f: IntArray, at: Int): String =
            if (2 * at + 1 < f.size) String(bytes, f[2 * at], f[2 * at + 1] - f[2 * at], Charsets.UTF_8) else "\"\""
        fun flush() {
            if (count == 0) return
            json.append(']')
            emit(Chunk(json.toString(), count, ArrayList(glossaries)))
            json.setLength(0)
            glossaries.clear()
            count = 0
        }
        scanner.rows { f ->
            if (f.size < (if (terms) 10 else 6) || bytes[f[0]] != '"'.code.toByte()) return@rows
            json.append(if (count == 0) "[[" else ",[")
            if (terms) {
                json.append(slice(f, 0)).append(',').append(slice(f, 1)).append(',').append(slice(f, 2)).append(',')
                    .append(slice(f, 3)).append(',').append(slice(f, 4)).append(',')
                if (format >= 3) {
                    json.append(if (f.size > 13) slice(f, 6) else "0").append(',').append(slice(f, 7))
                    glossaries.add(if (f.size > 11) packer.pack(bytes, f[10], f[11]) else packer.pack("[]".toByteArray(), 0, -1))
                } else {
                    json.append("0,\"\"")
                    val glosses = JSONArray()
                    var i = 10
                    while (i + 1 < f.size) {
                        glosses.put(scanner.text(f[i], f[i + 1]))
                        i += 2
                    }
                    glossaries.add(packer.pack(glosses.toString().toByteArray(), 0, -1))
                }
            } else {
                json.append(slice(f, 0)).append(',').append(slice(f, 1)).append(',').append(slice(f, 2))
            }
            json.append(']')
            if (++count == most) flush()
        }
        flush()
    }

    /**
     * A bank read from [input] a piece at a time: each piece is about [piece] bytes of whole
     * rows, given to [each] as a JSON array of them ("[row,row]"), so that a bank of any size is
     * never held whole. Throws [BankScanner.Malformed] when the bank is not an array of rows.
     */
    internal fun inPieces(input: java.io.InputStream, piece: Int = 4 * 1024 * 1024, each: (ByteArray) -> Unit) {
        var buffer = ByteArray(piece * 2)
        var length = 0
        var eof = false
        fun more(): Boolean {
            if (eof) return false
            if (length == buffer.size) buffer = buffer.copyOf(buffer.size * 2)
            val n = input.read(buffer, length, buffer.size - length)
            if (n < 0) { eof = true; return false }
            length += n
            return true
        }
        // The opening bracket, after a byte order mark and spaces.
        var p = 0
        while (true) {
            while (p >= length) if (!more()) throw BankScanner.Malformed()
            val c = buffer[p].toInt() and 0xFF
            if (c == 0xEF || c == 0xBB || c == 0xBF || c == 0x20 || c == 0x0A || c == 0x0D || c == 0x09) { p++; continue }
            if (c != '['.code) throw BankScanner.Malformed()
            p++
            break
        }
        var start = p // the first row of the piece in hand
        var depth = 1
        var inString = false
        var escaped = false
        var lastEnd = -1 // just after the last whole row seen
        while (true) {
            if (p >= length) {
                if (lastEnd > start && lastEnd - start >= piece) {
                    emit(buffer, start, lastEnd, each)
                    start = lastEnd
                }
                // Keep what is not given yet at the front, and read on.
                if (start > 0) {
                    System.arraycopy(buffer, start, buffer, 0, length - start)
                    length -= start
                    p -= start
                    if (lastEnd >= 0) lastEnd -= start
                    start = 0
                }
                if (!more()) throw BankScanner.Malformed() // the bank ended inside its array
            }
            val c = buffer[p].toInt()
            p++
            if (inString) {
                if (escaped) escaped = false
                else if (c == '\\'.code) escaped = true
                else if (c == '"'.code) inString = false
                continue
            }
            when (c) {
                '"'.code -> inString = true
                '['.code, '{'.code -> depth++
                ']'.code, '}'.code -> {
                    depth--
                    if (depth == 1) lastEnd = p
                    if (depth == 0) {
                        // The bank's own closing bracket: what is left is the last piece.
                        if (lastEnd > start) emit(buffer, start, lastEnd, each)
                        return
                    }
                }
            }
        }
    }

    /** Rows [from, to) of [buffer] (commas between them) as a JSON array, to [each]. */
    private fun emit(buffer: ByteArray, from: Int, to: Int, each: (ByteArray) -> Unit) {
        var a = from
        while (a < to && (buffer[a] == ','.code.toByte() || buffer[a] <= 0x20)) a++
        val out = ByteArray(to - a + 2)
        out[0] = '['.code.toByte()
        System.arraycopy(buffer, a, out, 1, to - a)
        out[out.size - 1] = ']'.code.toByte()
        each(out)
    }

    private fun readBytes(zip: ZipFile, entry: ZipEntry): ByteArray =
        // Into an array of its size: a growing buffer copied itself twice over for a 60 MB bank.
        zip.getInputStream(entry).use { input ->
            if (entry.size in 0..Int.MAX_VALUE.toLong()) {
                ByteArray(entry.size.toInt()).also { java.io.DataInputStream(input).readFully(it) }
            } else {
                input.readBytes()
            }
        }

    companion object {
        /** For measuring (the app's DictionaryTest): the engine's threads and read-ahead, 0 for the defaults. */
        @Volatile
        var engineThreads = 0
        @Volatile
        var engineAheadMb = 0

        /** Bank text the engine reads ahead of its writer at most: a phone's memory, not a computer's. */
        private const val ENGINE_AHEAD_MB = 64
        /** Meta rows to a chunk given SQLite as one JSON text. */
        private const val META_CHUNK = 4000
        /** Rows to a statement: within SQLite's 999 values in one statement. */
        private const val TERMS_AT_ONCE = 100
        private const val METAS_AT_ONCE = 200

        /** A picture or other file a structured-content glossary may show (Yomitan's "path"). */
        private val MEDIA = Regex("\\.(png|jpe?g|gif|webp|avif|svg|bmp|ico)$", RegexOption.IGNORE_CASE)
    }
}

/**
 * Finds the rows of a bank (a JSON array of arrays) in its UTF-8 bytes: each row's values as
 * pairs of offsets, begin and end, with only what is asked for decoded.
 */
internal class BankScanner(private val b: ByteArray) {
    class Malformed : Exception()

    private fun space(from: Int): Int {
        var p = from
        while (p < b.size) {
            val c = b[p].toInt()
            if (c != 0x20 && c != 0x0A && c != 0x0D && c != 0x09) break
            p++
        }
        return p
    }

    private fun stringEnd(from: Int): Int {
        var p = from + 1
        while (p < b.size) {
            val c = b[p].toInt()
            if (c == '\\'.code) p++ else if (c == '"'.code) return p + 1
            p++
        }
        throw Malformed()
    }

    private fun valueEnd(from: Int): Int {
        if (from >= b.size) throw Malformed()
        val first = b[from].toInt()
        if (first == '"'.code) return stringEnd(from)
        if (first == '['.code || first == '{'.code) {
            var depth = 0
            var p = from
            while (p < b.size) {
                when (b[p].toInt()) {
                    '"'.code -> { p = stringEnd(p); continue }
                    '['.code, '{'.code -> depth++
                    ']'.code, '}'.code -> if (--depth == 0) return p + 1
                }
                p++
            }
            throw Malformed()
        }
        var p = from
        while (p < b.size) {
            val c = b[p].toInt()
            if (c == ','.code || c == ']'.code || c == '}'.code || c == 0x20 || c == 0x0A || c == 0x0D || c == 0x09) break
            p++
        }
        if (p == from) throw Malformed()
        return p
    }

    /** Calls [each] with every row's values as begin, end, begin, end... */
    fun rows(each: (IntArray) -> Unit) {
        var p = 0
        if (b.size >= 3 && b[0] == 0xEF.toByte() && b[1] == 0xBB.toByte() && b[2] == 0xBF.toByte()) p = 3
        p = space(p)
        if (p >= b.size || b[p] != '['.code.toByte()) throw Malformed()
        p = space(p + 1)
        var fields = IntArray(32)
        while (p < b.size && b[p] != ']'.code.toByte()) {
            if (b[p] != '['.code.toByte()) {
                p = space(valueEnd(p))
            } else {
                var n = 0
                p = space(p + 1)
                while (p < b.size && b[p] != ']'.code.toByte()) {
                    val end = valueEnd(p)
                    if (n + 2 > fields.size) fields = fields.copyOf(fields.size * 2)
                    fields[n++] = p
                    fields[n++] = end
                    p = space(end)
                    if (p < b.size && b[p] == ','.code.toByte()) p = space(p + 1)
                }
                if (p >= b.size) throw Malformed()
                p = space(p + 1)
                each(fields.copyOf(n))
            }
            if (p < b.size && b[p] == ','.code.toByte()) p = space(p + 1)
        }
    }

    /** A JSON string's text. */
    fun string(begin: Int, end: Int): String {
        var slash = -1
        for (i in begin + 1 until end - 1) if (b[i] == '\\'.code.toByte()) { slash = i; break }
        if (slash < 0) return String(b, begin + 1, end - begin - 2, Charsets.UTF_8)
        return try {
            JSONArray("[" + String(b, begin, end - begin, Charsets.UTF_8) + "]").getString(0)
        } catch (e: Exception) {
            throw Malformed()
        }
    }

    /** A string or number as text; anything else as nothing. */
    fun text(begin: Int, end: Int): String {
        val first = b[begin].toInt()
        if (first == '"'.code) return string(begin, end)
        if (first == '-'.code || first in '0'.code..'9'.code) {
            val raw = String(b, begin, end - begin, Charsets.US_ASCII)
            raw.toLongOrNull()?.let { return it.toString() }
            val number = raw.toDoubleOrNull() ?: return ""
            return if (number == Math.floor(number) && Math.abs(number) < 9.0e15) number.toLong().toString() else number.toString()
        }
        return ""
    }

    fun number(begin: Int, end: Int): Long {
        val raw = if (b[begin] == '"'.code.toByte()) string(begin, end) else String(b, begin, end - begin, Charsets.US_ASCII)
        return raw.toLongOrNull() ?: raw.toDoubleOrNull()?.toLong() ?: 0L
    }
}
