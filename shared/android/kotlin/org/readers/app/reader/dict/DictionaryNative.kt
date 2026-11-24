// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

/**
 * The dictionary engine (shared/dict, docs/dictionary-engine.md) in Furoshiki's native library:
 * android/app/src/main/cpp/dictionary_jni.cpp. An app without it (a test on the computer, an app
 * whose library does not have it) is told so by [available], and keeps its dictionaries in SQLite.
 *
 * Lists come back as one string: records apart by U+001E, fields by U+001F.
 */
internal object DictionaryNative {
    /** Told the banks done of an import, and asked whether it is to go on (from any thread). */
    interface Progress {
        fun step(done: Int, total: Int)
        fun keepGoing(): Boolean
    }

    val available: Boolean by lazy {
        try {
            System.loadLibrary("furoshiki")
            probe()
        } catch (e: Throwable) { // no library, or one without the engine
            false
        }
    }

    /** A list's records, each as its fields. */
    fun records(text: String): List<List<String>> =
        if (text.isEmpty()) emptyList()
        else text.split('\u001e').filter { it.isNotEmpty() }.map { it.split('\u001f') }

    @JvmStatic external fun probe(): Boolean
    @JvmStatic external fun open(folder: String): Long
    @JvmStatic external fun close(handle: Long)
    @JvmStatic external fun dictionaries(handle: Long): String
    @JvmStatic external fun hasEnabled(handle: Long): Boolean
    @JvmStatic external fun setEnabled(handle: Long, ids: LongArray, enabled: Boolean)
    @JvmStatic external fun setOrder(handle: Long, ids: LongArray)
    @JvmStatic external fun delete(handle: Long, ids: LongArray)
    @JvmStatic external fun compact(handle: Long)
    @JvmStatic external fun uncovered(handle: Long): Int
    @JvmStatic external fun findTerms(handle: Long, keys: Array<String>, game: String): String
    @JvmStatic external fun definition(handle: Long, dictionary: Long, entry: Int): String
    /** Definitions of (dictionary shl 32 or entry), UTF-8, apart by U+001E. */
    @JvmStatic external fun definitions(handle: Long, ids: LongArray): ByteArray
    @JvmStatic external fun findMeta(handle: Long, expressions: Array<String>): String
    @JvmStatic external fun findKanji(handle: Long, character: String): String
    @JvmStatic external fun findKanjiMeta(handle: Long, character: String): String
    @JvmStatic external fun media(handle: Long, dictionary: Long, path: String): ByteArray?
    @JvmStatic external fun styles(handle: Long): String
    /** Moves dictionary [id] of an earlier version's SQLite file at [path] into the library; as importZip's result. */
    @JvmStatic external fun importSqlite(handle: Long, path: String, id: Long, progress: Progress?, threads: Int): String
    @JvmStatic external fun importZip(handle: Long, path: String, progress: Progress?, threads: Int, aheadMb: Int): String
}
