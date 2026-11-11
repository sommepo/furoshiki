// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.dict

import android.content.Context
import java.io.File
import java.util.zip.ZipFile
import org.json.JSONObject

/**
 * Games' names dictionaries as tools/titles.py makes them (docs/titles.md): a Yomitan zip,
 * "names-<title>.zip", whose index.json says which games it is for (furoshikiScope). Found in
 * the user's profiles folder (installed with a script from the
 * published list) and among the app's assets (reader/names/, a build of one's own). The desktop
 * has the same (shared/reader/desktop/names.*).
 */
object NamesDictionaries {
    const val ASSETS = "reader/names"
    private val ZIP = Regex("names-[a-z0-9][a-z0-9-]*\\.zip")

    /** One names dictionary: its file, its title's key, its title as a dictionary, its games. */
    data class Pack(val file: File, val key: String, val dictionary: String, val revision: String, val scope: List<String>)

    /** A zip's index.json read as a pack; null when it is not a names dictionary. */
    fun read(file: File): Pack? = try {
        ZipFile(file).use { zip ->
            val index = JSONObject(zip.getInputStream(zip.getEntry("index.json") ?: return null).use { it.readBytes().decodeToString() })
            val scope = index.optJSONArray("furoshikiScope") ?: return null
            val key = index.optString("furoshikiTitle")
            val title = index.optString("title").trim()
            if (key.isEmpty() || title.isEmpty()) return null
            Pack(file, key, title, index.optString("revision"),
                (0 until scope.length()).map { scope.optString(it).trim().uppercase() }.filter { it.isNotEmpty() })
        }
    } catch (_: Exception) {
        null
    }

    /** The names zips in [folders], the first folder's before the others' (one per title). */
    fun find(folders: List<File>): List<Pack> {
        val out = LinkedHashMap<String, Pack>()
        for (folder in folders) {
            for (file in folder.listFiles().orEmpty().filter { ZIP.matches(it.name) }.sortedBy { it.name }) {
                read(file)?.let { out.putIfAbsent(it.key, it) }
            }
        }
        return out.values.toList()
    }

    /** The pack whose scope has [game]. */
    fun forGame(game: String, packs: List<Pack>): Pack? {
        val id = game.trim().uppercase()
        return if (id.isEmpty()) null else packs.firstOrNull { id in it.scope }
    }

    /**
     * The folders to look in: the user's profiles folder, then the app's own assets copied once
     * into its files (a zip is read from a file).
     */
    fun folders(context: Context, userProfiles: File?): List<File> {
        val bundled = File(context.filesDir, ASSETS)
        val names = try {
            context.assets.list(ASSETS).orEmpty().filter { ZIP.matches(it) }
        } catch (_: Exception) {
            emptyList()
        }
        if (names.isNotEmpty()) {
            bundled.mkdirs()
            for (name in names) {
                val bytes = context.assets.open("$ASSETS/$name").use { it.readBytes() }
                val file = File(bundled, name)
                if (!file.isFile || !file.readBytes().contentEquals(bytes)) file.writeBytes(bytes)
            }
            bundled.listFiles().orEmpty().filter { it.name !in names }.forEach { it.delete() }
        }
        return listOfNotNull(userProfiles, bundled)
    }

    /**
     * Puts [pack]'s dictionary in [database] unless it is there at its revision: one of an older
     * revision is replaced, on or off as the user had it. True when it imported. Off the main thread.
     */
    fun ensure(database: DictionaryDatabase, pack: Pack): Boolean {
        remember(pack)
        val installed = database.dictionaries().lastOrNull { it.title == pack.dictionary }
        if (installed != null && installed.revision == pack.revision) return false
        if (installed != null) database.delete(installed.id)
        DictionaryImporter(database).import(pack.file)
        if (installed != null && !installed.enabled) {
            database.dictionaries().filter { it.title == pack.dictionary }.forEach { database.setEnabled(it.id, false) }
        }
        return true
    }

    /**
     * The card a names dictionary gives an entry (tools/titles.py, card_gloss): its one glossary
     * item is structured content whose root holds, as `data`, who or what the name is. Null for
     * a glossary without one (a names dictionary made before the cards, any other dictionary).
     */
    data class Card(
        val id: String, val kind: String, val name: String, val reading: String, val romaji: String,
        val role: String, val also: List<String>, val about: String, val image: String,
    )

    fun cardOf(glossaryJson: String): Card? = try {
        val glossary = org.json.JSONArray(glossaryJson)
        (0 until glossary.length()).firstNotNullOfOrNull { index ->
            val item = glossary.optJSONObject(index)?.takeIf { it.optString("type") == "structured-content" }
            val data = item?.optJSONObject("content")?.optJSONObject("data")?.takeIf { it.optString("furoshiki") == "name" }
            val name = data?.optString("name")?.trim().orEmpty()
            if (data == null || name.isEmpty()) null else Card(
                id = data.optString("id").trim(), kind = data.optString("kind").trim().ifEmpty { "person" }, name = name,
                reading = data.optString("reading").trim(), romaji = data.optString("romaji").trim(),
                role = data.optString("role").trim(), also = data.optString("also").split('、').map { it.trim() }.filter { it.isNotEmpty() },
                about = data.optString("about").trim(), image = data.optString("image").trim())
        }
    } catch (_: Exception) {
        null
    }

    private val zips = HashMap<String, File>()
    private val shown = HashMap<String, String>()
    private val PLAIN = Regex("img/[A-Za-z0-9_.-]+\\.(jpg|jpeg|png|webp)")

    /** Says which zip a names dictionary's pictures are in ([ensure] does it by itself). */
    @Synchronized
    fun remember(pack: Pack) {
        if (zips[pack.dictionary] != pack.file) {
            zips[pack.dictionary] = pack.file
            shown.clear()
        }
    }

    /**
     * A picture of the names dictionary titled [dictionary], as a "data:" address the popup's page
     * can show: read from the dictionary's zip (the one [ensure] was last asked about under that
     * title) and kept. Empty when there is no such zip or picture.
     */
    @Synchronized
    fun picture(dictionary: String, path: String): String {
        if (!PLAIN.matches(path)) return ""
        val key = "$dictionary\n$path"
        shown[key]?.let { return it }
        val address = try {
            zips[dictionary]?.let { file ->
                ZipFile(file).use { zip ->
                    val entry = zip.getEntry(path)
                    if (entry == null || entry.size > 512 * 1024) null else {
                        val type = when (path.substringAfterLast('.')) { "png" -> "png"; "webp" -> "webp"; else -> "jpeg" }
                        "data:image/$type;base64," + android.util.Base64.encodeToString(zip.getInputStream(entry).use { it.readBytes() }, android.util.Base64.NO_WRAP)
                    }
                }
            }
        } catch (_: Exception) {
            null
        }.orEmpty()
        if (shown.size > 400) shown.clear()
        shown[key] = address
        return address
    }
}
