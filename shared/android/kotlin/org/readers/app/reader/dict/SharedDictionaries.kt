// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import androidx.preference.PreferenceManager
import org.json.JSONObject

/**
 * The Readers folder: a folder of the device's storage that every reader app is given once
 * (Dictionaries, Readers folder…), so that a dictionary put there is imported by each of them
 * without being chosen in each. Android keeps every app's files apart, so each app imports
 * the files itself; what it has imported is remembered by name and size.
 */
object SharedDictionaries {
    private const val KEY_FOLDER = "reader_dict_folder"
    private const val KEY_SEEN = "reader_dict_folder_seen"

    /** A dictionary archive in the folder. */
    data class File(val uri: Uri, val name: String, val size: Long)

    /**
     * For an app that may read the device's files by their paths (Furoshiki, with all files
     * access): the folder itself, which then stands in for the one a user gives each app.
     */
    @Volatile
    var plainFolder: ((Context) -> java.io.File?)? = null

    /** What the Dictionaries page says of a folder found by itself ([plainFolder]), or null. */
    fun plainName(context: Context): String? = plainFolder?.invoke(context)?.name

    fun folder(context: Context): Uri? =
        PreferenceManager.getDefaultSharedPreferences(context).getString(KEY_FOLDER, null)?.let(Uri::parse)

    fun setFolder(context: Context, tree: Uri) {
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString(KEY_FOLDER, tree.toString()).apply()
    }

    /** The folder's name as the device shows it. */
    fun name(context: Context, tree: Uri): String =
        DocumentsContract.getTreeDocumentId(tree).substringAfterLast(':').ifEmpty { tree.lastPathSegment ?: "" }

    /** The archives in the folder this app has not imported yet. Off the main thread. */
    fun newFiles(context: Context): List<File> {
        val seen = seen(context)
        val plain = plainFolder?.invoke(context)
        val tree = folder(context)
        val files = when {
            plain != null -> plain.listFiles().orEmpty().filter { it.name.lowercase().endsWith(".zip") }
                .sortedBy { it.name }.map { File(Uri.fromFile(it), it.name, it.length()) }
            tree != null -> list(context, tree)
            else -> emptyList()
        }
        return files.filter { file -> seen.optLong(file.name, -1L) != file.size }
    }

    fun markSeen(context: Context, file: File) {
        val seen = seen(context).put(file.name, file.size)
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString(KEY_SEEN, seen.toString()).apply()
    }

    private fun seen(context: Context): JSONObject = try {
        JSONObject(PreferenceManager.getDefaultSharedPreferences(context).getString(KEY_SEEN, "{}")!!)
    } catch (_: Exception) {
        JSONObject()
    }

    private fun list(context: Context, tree: Uri): List<File> {
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        val out = ArrayList<File>()
        try {
            context.contentResolver.query(
                children,
                arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_SIZE),
                null, null, null
            )?.use { cursor ->
                while (cursor.moveToNext()) {
                    val name = cursor.getString(1) ?: continue
                    if (!name.lowercase().endsWith(".zip")) continue
                    out.add(File(DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0)), name, cursor.getLong(2)))
                }
            }
        } catch (_: Exception) {
            // The folder is gone, or this app was not given it again after a reinstall.
        }
        return out.sortedBy { it.name }
    }
}
