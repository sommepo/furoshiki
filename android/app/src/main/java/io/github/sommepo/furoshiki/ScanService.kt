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
        save(key, found)
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
    }
}
