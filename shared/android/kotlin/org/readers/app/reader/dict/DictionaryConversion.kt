// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import org.readers.app.reader.ReaderLog
import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The dictionaries of the SQLite file (reader_dict.db) moved into the dictionary engine's library,
 * once, in the background, as the desktop does (docs/dictionary-engine.md, phase 3). Each is
 * written out as a Yomitan zip of its own rows (in their order, so that lookups rank as before)
 * and imported. Its place in the order and whether it is on are kept. Only when every dictionary
 * is in the library with all its terms and frequencies is the SQLite file deleted: until then
 * the app goes on using it, and a dictionary short of rows leaves it in use (said in the log).
 *
 * Both of the app's processes may start it; a lock between them lets one do it.
 */
object DictionaryConversion {
    private val running = AtomicBoolean(false)

    /**
     * How far it is, for the home screen's prompt and strip (the maintainer, 2026-11-24: "it needs
     * to be a prompt so they are aware it's happening, with a progress bar"): dictionaries done of
     * all, the one being moved and its banks done of all; once [finished], what failed and how
     * long it took; [resumed]: moved by an earlier start that was closed halfway.
     */
    data class State(
        val done: Int = 0,
        val total: Int = 0,
        val title: String = "",
        val step: Int = 0,
        val steps: Int = 0,
        val running: Boolean = false,
        val finished: Boolean = false,
        val failed: List<String> = emptyList(),
        val seconds: Double = 0.0,
        val resumed: Int = 0
    ) {
        /** The whole move's share done, 0 to 1. */
        val share: Double get() = if (total <= 0) 0.0 else ((done + (if (steps > 0) step.toDouble() / steps else 0.0)) / total).coerceIn(0.0, 1.0)
    }

    @Volatile
    var state = State()
        private set

    /** Whether a move was started in this process and has not ended (it may not have begun to count yet). */
    val busy: Boolean get() = running.get()

    /** Dictionaries converted and how many there are, while it runs; null otherwise. */
    val progress: Pair<Int, Int>? get() = state.takeIf { it.running }?.let { it.done to it.total }

    /** Asked to stop between dictionaries (and the one being moved cancelled): [abandon]. */
    @Volatile
    private var stopping = false
    private var worker: Thread? = null

    /**
     * The move given up, for importing the dictionaries again from their own files (the maintainer,
     * 2026-11-24: "can we at least give them the option to delete and reimport?"): the move is
     * stopped, what it moved so far is deleted from the library, and the old file is deleted. Returns
     * how many dictionaries the old file had. Blocks until the move has stopped: not on the main thread.
     */
    fun abandon(database: DictionaryDatabase, context: Context, engine: Long): Int {
        stopping = true
        try {
            worker?.join()
        } finally {
            stopping = false
        }
        val legacy = context.getDatabasePath(DictionaryDatabase.DATABASE_NAME)
        RandomAccessFile(File(context.filesDir, "dictionaries-conversion.lock"), "rw").channel.use { channel ->
            channel.lock().use {
                if (!legacy.exists()) return -1
                val titles = dictionaries(database.writableDatabase).map { it.title }.toSet()
                // Only those of the old file: one the user imported meanwhile went into the old file too.
                val moved = DictionaryNative.records(DictionaryNative.dictionaries(engine))
                    .filter { it.size >= 2 && it[1] in titles }.map { it[0].toLong() }
                if (moved.isNotEmpty()) DictionaryNative.delete(engine, moved.toLongArray())
                context.deleteDatabase(DictionaryDatabase.DATABASE_NAME)
                database.changed()
                state = State()
                ReaderLog.info("[Reader] Dictionary conversion given up: ${titles.size} removed, the SQLite file is deleted")
                return titles.size
            }
        }
    }

    fun start(database: DictionaryDatabase, context: Context, engine: Long) {
        if (!running.compareAndSet(false, true)) return
        worker = Thread({
            try {
                convert(database, context, engine)
            } catch (e: Throwable) {
                ReaderLog.warning("[Reader] Dictionary conversion stopped: ${e.message}")
            } finally {
                if (!state.finished) state = state.copy(running = false)
                running.set(false)
            }
        }, "dictionary-conversion").apply {
            // Below the game and the screen, not at the bottom (that is barely run on a phone).
            priority = Thread.NORM_PRIORITY - 1
            isDaemon = true
            start()
        }
    }

    private class Old(
        val id: Long, val title: String, val revision: String, val author: String, val description: String,
        val frequencyMode: String, val enabled: Boolean, val scope: String, val styles: String
    )

    private fun convert(database: DictionaryDatabase, context: Context, engine: Long) {
        val legacy = context.getDatabasePath(DictionaryDatabase.DATABASE_NAME)
        RandomAccessFile(File(context.filesDir, "dictionaries-conversion.lock"), "rw").channel.use { channel ->
            val lock = channel.tryLock() ?: return // another process of the app is converting
            lock.use {
                if (!legacy.exists()) return
                val started = System.nanoTime()
                fun seconds() = (System.nanoTime() - started) / 1e9
                // Through the app's own helper, which brings an older file's tables up to date first.
                val db = database.writableDatabase
                var converted = 0
                val failed = ArrayList<String>()
                try {
                    var old = dictionaries(db)
                    if (old.isEmpty()) {
                        context.deleteDatabase(DictionaryDatabase.DATABASE_NAME)
                        database.changed()
                        return
                    }
                    val already = DictionaryNative.records(DictionaryNative.dictionaries(engine))
                        .filter { it.size >= 2 }.map { it[1] }.toSet()
                    // Moved by an earlier start closed halfway: counted as done.
                    val resumed = old.count { it.title in already }
                    state = State(done = resumed, total = old.size, running = true, resumed = resumed)
                    // Again until none is left: one imported meanwhile went into the SQLite file.
                    while (true) {
                        val there = DictionaryNative.records(DictionaryNative.dictionaries(engine))
                            .filter { it.size >= 2 }.map { it[1] }.toSet()
                        val left = old.filter { it.title !in there && it.title !in failed }
                        if (left.isEmpty()) break
                        for (dictionary in left) {
                            if (stopping) return
                            state = state.copy(title = dictionary.title, step = 0, steps = 0)
                            try {
                                val told = object : DictionaryNative.Progress {
                                    override fun step(done: Int, total: Int) { state = state.copy(step = done, steps = total) }
                                    override fun keepGoing() = !stopping
                                }
                                // Read by the engine straight from the SQLite file (Dict::SqliteSource): what
                                // the app's SQLite has pending is written into the file first. It was written
                                // out as a zip here before, 38 s of the Y700's 8 dictionaries.
                                runCatching { db.rawQuery("PRAGMA wal_checkpoint(TRUNCATE)", emptyArray()).use { it.moveToFirst() } }
                                val f = DictionaryNative.records(
                                    DictionaryNative.importSqlite(engine, legacy.path, dictionary.id, told, 3)
                                ).firstOrNull().orEmpty()
                                if (stopping) return
                                if (f.getOrNull(0) != "1" && f.getOrNull(1) != "1") {
                                    ReaderLog.warning("[Reader] Converting ${dictionary.title} failed: ${f.getOrNull(3)}")
                                    failed.add(dictionary.title) // the others go on
                                } else {
                                    converted++
                                }
                            } catch (e: Exception) {
                                ReaderLog.warning("[Reader] Converting ${dictionary.title} failed: ${e.message}")
                                failed.add(dictionary.title)
                            }
                            state = state.copy(done = state.done + 1)
                        }
                        old = dictionaries(db)
                        state = state.copy(total = old.size)
                    }
                    // Every dictionary whole? Then its order and which are on, and the old file goes.
                    val now = DictionaryNative.records(DictionaryNative.dictionaries(engine))
                        .filter { it.size >= 10 }.associateBy { it[1] }
                    for (dictionary in old) {
                        if (dictionary.title in failed) continue
                        val mine = now[dictionary.title]
                        val terms = count(db, "terms", dictionary.id)
                        val metas = count(db, "term_meta", dictionary.id)
                        if (mine == null || mine[8].toLong() != terms || mine[9].toLong() != metas) {
                            ReaderLog.warning(
                                "[Reader] ${dictionary.title} has ${mine?.get(8)} terms and ${mine?.get(9)} metas in the " +
                                    "library, $terms and $metas in the SQLite file"
                            )
                            failed.add(dictionary.title)
                        }
                    }
                    if (failed.isNotEmpty()) {
                        // The SQLite file stays in use. What came short goes from the library, so that the
                        // next start moves it again rather than passing it over as moved.
                        val short = failed.mapNotNull { now[it]?.get(0)?.toLong() }
                        if (short.isNotEmpty()) DictionaryNative.delete(engine, short.toLongArray())
                        ReaderLog.warning("[Reader] Not converted whole, the SQLite file is kept: ${failed.joinToString(", ")}")
                        state = state.copy(running = false, finished = true, failed = failed, seconds = seconds())
                        return
                    }
                    val ids = old.map { now.getValue(it.title)[0].toLong() }
                    DictionaryNative.setOrder(engine, ids.toLongArray())
                    val off = old.filter { !it.enabled }.map { now.getValue(it.title)[0].toLong() }
                    if (off.isNotEmpty()) DictionaryNative.setEnabled(engine, off.toLongArray(), false)
                    DictionaryNative.compact(engine)
                } catch (e: Exception) {
                    ReaderLog.warning("[Reader] Dictionary conversion stopped: ${e.message}")
                    state = state.copy(running = false, finished = true, failed = listOf(e.message ?: "Unknown error"), seconds = seconds())
                    return
                }
                // This process's open connection stays on the deleted file, unused from now on:
                // DictionaryDatabase.onEngine goes by whether the file is there.
                context.deleteDatabase(DictionaryDatabase.DATABASE_NAME)
                database.changed()
                state = state.copy(running = false, finished = true, seconds = seconds())
                ReaderLog.info(
                    "[Reader] Dictionaries converted into the dictionary engine: $converted in " +
                        "%.1f s; the SQLite file is deleted".format(seconds())
                )
            }
        }
    }

    private fun dictionaries(db: SQLiteDatabase): List<Old> {
        val out = ArrayList<Old>()
        db.rawQuery(
            "SELECT id, title, revision, author, description, frequency_mode, enabled, scope, styles " +
                "FROM dictionaries ORDER BY priority, id", emptyArray()
        ).use { c ->
            while (c.moveToNext()) {
                out.add(
                    Old(
                        c.getLong(0), c.getString(1), c.getString(2), c.getString(3), c.getString(4),
                        c.getString(5), c.getInt(6) != 0, c.getString(7), c.getString(8)
                    )
                )
            }
        }
        return out
    }

    private fun count(db: SQLiteDatabase, table: String, id: Long): Long =
        db.rawQuery("SELECT COUNT(*) FROM $table WHERE dict = ?", arrayOf(id.toString())).use { c ->
            if (c.moveToFirst()) c.getLong(0) else 0
        }
}
