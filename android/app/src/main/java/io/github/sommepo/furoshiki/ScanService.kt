// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.os.Process
import android.util.Log
import io.github.sommepo.furoshiki.reader.Console
import io.github.sommepo.furoshiki.reader.ReaderCatalog
import io.github.sommepo.furoshiki.reader.ReaderNative
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * Looks through the game folders for one console, in a process of its own where its core may be
 * loaded, and writes what it finds to [Places.gamesFile]; HomeActivity starts it for each console
 * in turn. As on the desktop only the games with a text profile are listed, under the profile's
 * title: the core says what a file is (`identify`), never the app.
 */
class ScanService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val key = intent?.getStringExtra(EXTRA_CONSOLE).orEmpty()
        Thread({
            try {
                scan(key)
            } catch (e: Throwable) {
                Log.e("Furoshiki", "Looking through the games failed", e)
            }
            sendBroadcast(Intent(Places.SCANNED).setPackage(packageName).putExtra(EXTRA_CONSOLE, key))
            stopSelf()
            // A core stays loaded for good, and the next console's is another process's. A
            // broadcast from a process that has just gone is dropped: it is left time to go out.
            Thread.sleep(BROADCAST_MS)
            Process.killProcess(Process.myPid())
        }, "GameScan").start()
        return START_NOT_STICKY
    }

    private fun scan(key: String) {
        val file = Places.CORES[key] ?: return
        ReaderNative.appContext = applicationContext
        Console.use(key)
        val core = Places.corePath(this, file)
        if (!File(core).isFile) return
        CoreAssets.forConsole(this, key)
        val error = GameNative.open(
            core, Places.console(this, key, "data"), Places.console(this, key, "saves"),
            Places.console(this, key, "states"), Places.system(this, key)
        )
        if (error != null) {
            Log.e("Furoshiki", "$key: $error")
            return
        }
        val about = GameNative.info() ?: return
        Places.coreFile(this, key).writeText(about)
        val info = JSONObject(about)
        val typesList = info.getJSONArray("types")
        val types = List(typesList.length()) { typesList.getString(it) }.toSet()
        val titles = ReaderCatalog.games(this).associate { it.titleId to it.title }
        val found = JSONArray()
        val seen = HashSet<String>()
        val all = getSharedPreferences("home", MODE_PRIVATE).getBoolean(Places.KEY_ALL_GAMES, false)
        for (folder in Places.gameFolders(this)) {
            folder.walkTopDown().maxDepth(4)
                .filter { it.isFile && it.extension.lowercase() in types && seen.add(it.path) }
                // A disc image and its cue sheet are one game: the sheet is what is played.
                .filter {
                    "cue" !in types || it.extension.lowercase() != "bin" ||
                        !File(it.parentFile, it.nameWithoutExtension + ".cue").isFile
                }
                .forEach { game ->
                    val said = GameNative.identify(game.path) ?: return@forEach
                    // Games with a script, under its title; with "all games" (the Settings page's
                    // row) the others too, under what the core says they are called.
                    val scripted = titles[said[0]]
                    val title = scripted ?: said.getOrNull(1)?.takeIf { all && it.isNotBlank() } ?: (if (all) game.nameWithoutExtension else return@forEach)
                    // Two discs of one game have one ID and one script: the file tells them apart.
                    val disc = DISC.find(game.name)?.takeUnless { "ディスク" in title }
                        ?.let { " (Disc ${it.groupValues[1]})" }.orEmpty()
                    val entry = JSONObject().put("id", said[0]).put("title", title + disc).put("path", game.path)
                        .put("script", scripted != null)
                        .put("console", key).put("system", info.getString("system"))
                        .put("short", info.getString("short"))
                    Covers.of(this, key, said[0], game)?.let { entry.put("cover", it.path) }
                    found.put(entry)
                }
        }
        // Nothing at all found where the last look found games whose files are still there: the
        // storage was not ready yet (just after the device starts) or the core could not say what
        // its files are. The list stands as it was, rather than the console going from the shelf
        // until the next look (the user, 2026-11-14: the Dreamcast and the PS2 gone after a restart).
        if (found.length() == 0 && (stillThere(key) || !foldersReady())) {
            Log.w("Furoshiki", "$key: no game found, its last list kept")
        } else {
            save(key, oneEntryAGame(found))
        }
        // The list stands; what art it lacks comes after, as the network may be slow or absent.
        sendBroadcast(Intent(Places.SCANNED).setPackage(packageName).putExtra(EXTRA_LISTED, true))
        var fetched = false
        for (i in 0 until found.length()) {
            val game = found.getJSONObject(i)
            if (game.has("cover")) continue
            Covers.fetch(this, key, game.getString("id"), File(game.getString("path")))?.let {
                game.put("cover", it.path)
                fetched = true
            }
        }
        if (fetched) save(key, found)
    }

    /** Whether every game folder can be read: just after the device starts the storage may not be. */
    private fun foldersReady(): Boolean = Places.gameFolders(this).all { it.isDirectory && it.listFiles() != null }

    /**
     * Whether the console's last list has a game whose file is still there, or on a card not
     * mounted yet: the Thor's SD card comes some time after the device starts, and its games
     * looked gone until then (the user, 2026-11-14).
     */
    private fun stillThere(key: String): Boolean = runCatching {
        val last = JSONArray(Places.gamesFile(this, key).readText())
        (0 until last.length()).any { at ->
            val path = last.getJSONObject(at).optString("path")
            File(path).isFile || volumeMissing(path)
        }
    }.getOrDefault(false)

    /** A path on a storage volume (/storage/<id>/…) that is not there or not readable yet. */
    private fun volumeMissing(path: String): Boolean {
        val parts = path.split('/')
        if (parts.size < 4 || parts[1] != "storage" || parts[2] == "emulated" || parts[2] == "self") return false
        val volume = File("/storage/${parts[2]}")
        return !volume.isDirectory || volume.listFiles() == null
    }

    private fun save(key: String, found: JSONArray) {
        val out = Places.gamesFile(this, key)
        val partial = File(out.path + ".new")
        partial.writeText(found.toString())
        partial.renameTo(out)
    }

    companion object {
        const val EXTRA_CONSOLE = "console"

        /** On [Places.SCANNED]: the page may show the list, and the look at this console goes on. */
        const val EXTRA_LISTED = "listed"
        private const val BROADCAST_MS = 300L

        /** Long enough after its last broadcast for a scan's process to have gone. */
        const val GONE_MS = 1000L
        private val DISC = Regex("\\(Disc (\\d+)\\)")

        /**
         * A game on several discs as one entry (the user, 2026-11-13): its first disc's, under its
         * title without "(Disc N)", with how many discs it has. The game is given the rest as it
         * starts (GameActivity, [Discs]).
         */
        fun oneEntryAGame(found: JSONArray): JSONArray {
            val groups = LinkedHashMap<String, MutableList<JSONObject>>()
            for (i in 0 until found.length()) {
                val entry = found.getJSONObject(i)
                val path = entry.optString("path")
                val key = if (Discs.number(File(path).name) != null) Discs.groupKey(path) else "one:$path"
                groups.getOrPut(key) { ArrayList() }.add(entry)
            }
            val out = JSONArray()
            for (entries in groups.values) {
                if (entries.size == 1) {
                    out.put(entries[0])
                    continue
                }
                val first = entries.minBy { Discs.number(File(it.optString("path")).name) ?: Int.MAX_VALUE }
                first.put("title", Discs.withoutDisc(first.optString("title")))
                first.put("discs", entries.size)
                out.put(first)
            }
            return out
        }
    }
}
