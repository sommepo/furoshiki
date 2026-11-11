// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.os.Environment
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/** Where the app keeps things, as the desktop program's `program/consoles/<key>` has them. */
object Places {
    /** In the "home" preferences: games without a script are listed too (they play, unread). */
    const val KEY_ALL_GAMES = "all_games"

    /** A pinned shortcut's intent: the game to start at once. */
    const val ACTION_PLAY = "io.github.sommepo.furoshiki.PLAY"

    const val SYSTEM_PSP = "ppsspp"
    private const val SD_ID = "00000000000000000000000000000000"

    /** The consoles that need the user's BIOS, and their folders in Games/BIOS. */
    val BIOS_FOLDERS = mapOf(
        "ps1" to "PS1", "ps2" to "PS2", "psvita" to "Vita",
    )
    const val SCANNED = "io.github.sommepo.furoshiki.SCANNED"

    /** The cores this APK may carry: the file in the app's library folder, by console key. */
    val CORES = linkedMapOf(
        "psp" to "libreader_core_ppsspp.so", "dc" to "libreader_core_flycast.so", "3ds" to "libreader_core_azahar.so",
        "ps1" to "libreader_core_station.so", "ps2" to "libreader_core_armsx2.so",
        "psvita" to "libreader_core_vita3k.so",
    )

    private fun root(context: Context): File = context.getExternalFilesDir(null) ?: context.filesDir

    fun console(context: Context, key: String, folder: String): String =
        File(root(context), "consoles/$key/$folder").apply { mkdirs() }.path

    /** Where a console's core keeps its games' saves, as the desktop program's save sync has it. */
    fun savedata(context: Context, key: String): String {
        val saves = console(context, key, "saves")
        return when (key) {
            "psp" -> File(saves, "PSP/SAVEDATA").apply { mkdirs() }.path
            // The SD card's title folder: Azahar's libretro build keeps its folders in Azahar/.
            "3ds" -> File(saves, "Azahar/sdmc/Nintendo 3DS/$SD_ID/$SD_ID/title").apply { mkdirs() }.path
            // Vita3K's own memory card, in the core's data folder.
            "psvita" -> File(console(context, key, "data"), "fs/ux0/user/00/savedata").apply { mkdirs() }.path
            else -> saves
        }
    }

    fun corePath(context: Context, file: String): String =
        File(context.applicationInfo.nativeLibraryDir, file).path

    /** What the last look through a console's games found (ScanService). */
    fun gamesFile(context: Context, key: String) = File(context.filesDir, "games-$key.json")

    /** What a console's core said of itself at the last look (ScanService). */
    fun coreFile(context: Context, key: String) = File(context.filesDir, "core-$key.json")

    /** The settings for playing, in layers ([PlaySettings]). */
    fun playSettings(context: Context) = PlaySettings(File(context.filesDir, "settings.json"))

    /** What a console's core understands, from the last look. */
    fun coreSettings(context: Context, key: String): List<PlaySetting> = try {
        PlaySetting.list(key, JSONObject(coreFile(context, key).readText()))
    } catch (_: Exception) {
        emptyList()
    }

    private fun foldersFile(context: Context) = File(context.filesDir, "folders.json")

    /** The folders the user added on the page. */
    fun folders(context: Context): List<String> = try {
        val list = JSONArray(foldersFile(context).readText())
        List(list.length()) { list.getString(it) }
    } catch (_: Exception) {
        emptyList()
    }

    fun setFolders(context: Context, folders: List<String>) {
        foldersFile(context).writeText(JSONArray(folders).toString())
    }

    /**
     * Where games are looked for: the user's folders, and the folders the reader apps have
     * always used, on the device and on any card ("Games/<System>", "Reader Games").
     */
    fun gameFolders(context: Context): List<File> {
        val roots = ArrayList<File>()
        roots.add(Environment.getExternalStorageDirectory())
        File("/storage").listFiles()?.filter { it.name != "emulated" && it.name != "self" }?.let { roots.addAll(it) }
        val usual = roots.flatMap { listOf(File(it, "Games"), File(it, "Reader Games")) }
        return (folders(context).map { File(it) } + usual).filter { it.isDirectory }.distinctBy { it.path }
    }

    /**
     * Where a core looks for the console's own files. A PlayStation's BIOS is the user's, and
     * the reader apps keep such files in Games/BIOS/<System>; found by itself like the games.
     */
    fun system(context: Context, key: String): String {
        val own = if (key == "ps1") bios(context, key) else null
        return (own ?: File(console(context, key, "system"))).path
    }

    /** The user's folder with a BIOS for [key], or null. */
    fun bios(context: Context, key: String): File? {
        val name = BIOS_FOLDERS[key] ?: return null
        return gameFolders(context).map { File(it, "BIOS/$name") }.firstOrNull { it.list()?.isNotEmpty() == true }
    }

    /** Where the reader apps keep dictionaries to import: Games/Dictionaries, found by itself. */
    fun dictionaryFolder(context: Context): File? =
        gameFolders(context).map { File(it, "Dictionaries") }.firstOrNull { it.isDirectory }

    /** The path of a folder the file picker chose ("primary:Games", "1234-5678:Games"). */
    fun pathOfTree(documentId: String): String? {
        val colon = documentId.indexOf(':')
        if (colon < 0) return null
        val volume = documentId.substring(0, colon)
        val below = documentId.substring(colon + 1)
        val base = if (volume == "primary") Environment.getExternalStorageDirectory().path else "/storage/$volume"
        return if (below.isEmpty()) base else "$base/$below"
    }
}
