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
        // One console a process: its core stays loaded for good. A look at another console sent
        // here before this process went (two pages looking at once, or a look asked for again
        // while the last went on) would have that core say what the other's files are, and the
        // other's list would be written from them (the Thor, 2026-11-15: Dreamcast discs on the
        // 3DS's list, PS2 discs on the PlayStation's, consoles' lists emptied). It is handed back.
        synchronized(ScanService) {
            if (claimed != null) {
                if (claimed != key) {
                    Log.w("Furoshiki", "$key: this look's process has $claimed's core, the look is asked again")
                    sendBroadcast(Intent(Places.SCANNED).setPackage(packageName).putExtra(EXTRA_CONSOLE, key).putExtra(EXTRA_AGAIN, true))
                }
                return START_NOT_STICKY
            }
            claimed = key
        }
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
        // The user, 2026-11-14: the Dreamcast and the PS2 gone from the shelf after a restart.
        // An empty look keeps the last list only while the games could not be looked at: a card
        // not mounted yet (the Thor's, just after it starts) or folders not readable yet. Folders
        // that could be read with nothing in them for this core: none (a list another core once
        // wrote here, the Thor's 3DS list of Dreamcast discs, does not stay because its files do).
        val kept = found.length() == 0 && (onMissingVolume(key) || !foldersReady())
        if (kept) {
            Log.w("Furoshiki", "$key: no game found while its games could not be looked at, its last list kept")
        } else {
            save(key, oneEntryAGame(JSONArray(found.toString()), records(this)))
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
        // As listed: a game of several discs one entry (it was saved disc by disc here before).
        if (fetched && !kept) save(key, oneEntryAGame(found, records(this)))
    }

    /** Whether every game folder can be read: just after the device starts the storage may not be. */
    private fun foldersReady(): Boolean = Places.gameFolders(this).all { it.isDirectory && it.listFiles() != null }

    /**
     * Whether the console's last list has a game on a card not mounted yet: the Thor's SD card
     * comes some time after the device starts, and its games looked gone until then (the user,
     * 2026-11-14).
     */
    private fun onMissingVolume(key: String): Boolean = runCatching {
        val last = JSONArray(Places.gamesFile(this, key).readText())
        (0 until last.length()).any { at -> volumeMissing(last.getJSONObject(at).optString("path")) }
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

        /** On [Places.SCANNED]: this console was not looked at (its process had another's core): again, later. */
        const val EXTRA_AGAIN = "again"

        /** The console this process looks at, the only one its core may be: set by the first look. */
        @Volatile
        private var claimed: String? = null
        private const val BROADCAST_MS = 300L

        /** Long enough after its last broadcast for a scan's process to have gone. */
        const val GONE_MS = 1000L
        private val DISC = Regex("\\(Disc (\\d+)\\)")

        /**
         * A game on several discs as one entry (the user, 2026-11-13): its first disc's, under its
         * title without "(Disc N)", with how many discs it has and their files in order (`paths`),
         * which the game is given as it starts (GameActivity). Discs named alike are one game only
         * when their IDs are one title's ([records]: game ID to its title record, titles.json):
         * Grisaia's two discs are one game, Da Capo I&II's two discs two games (2026-11-17).
         */
        fun oneEntryAGame(found: JSONArray, records: Map<String, String> = emptyMap()): JSONArray {
            val groups = LinkedHashMap<String, MutableList<JSONObject>>()
            for (i in 0 until found.length()) {
                val entry = found.getJSONObject(i)
                val path = entry.optString("path")
                val id = entry.optString("id")
                val key = if (Discs.number(File(path).name) != null) Discs.groupKey(path) + "\u0000" + (records[id] ?: id) else "one:$path"
                groups.getOrPut(key) { ArrayList() }.add(entry)
            }
            val out = JSONArray()
            for (entries in groups.values) {
                if (entries.size == 1) {
                    out.put(entries[0])
                    continue
                }
                val ordered = entries.sortedBy { Discs.number(File(it.optString("path")).name) ?: Int.MAX_VALUE }
                val first = ordered[0]
                first.put("title", Discs.withoutDisc(first.optString("title")))
                first.put("discs", entries.size)
                first.put("paths", JSONArray(ordered.map { it.optString("path") }))
                out.put(first)
            }
            return out
        }

        /** Each game ID's title record (the app's titles.json, tools/titles-index.py), or none. */
        fun records(context: android.content.Context): Map<String, String> = runCatching {
            val all = JSONObject(context.assets.open("front/titles.json").bufferedReader().use { it.readText() })
            all.keys().asSequence().associateWith { all.getJSONObject(it).optString("key", it) }
        }.getOrDefault(emptyMap())

        /**
         * The discs of the game listed at [path] in [listed] (a console's list), in order: its
         * `paths`, or only [path] for a game on one disc and for a disc that is a game of its own.
         */
        fun discsOf(listed: JSONArray, path: String): List<String> {
            for (i in 0 until listed.length()) {
                val entry = listed.optJSONObject(i) ?: continue
                val paths = entry.optJSONArray("paths") ?: continue
                val all = List(paths.length()) { paths.getString(it) }
                if (path in all) return all
            }
            return listOf(path)
        }
    }
}
