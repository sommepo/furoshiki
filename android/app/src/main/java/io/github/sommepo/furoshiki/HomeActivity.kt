// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import android.view.KeyEvent
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.webkit.JavascriptInterface
import android.webkit.WebChromeClient
import android.webkit.WebView
import androidx.appcompat.app.AppCompatActivity
import io.github.sommepo.furoshiki.reader.anki.AnkiDroidClient
import io.github.sommepo.furoshiki.reader.stats.ReadingRules
import io.github.sommepo.furoshiki.reader.stats.SessionSummary
import io.github.sommepo.furoshiki.reader.store.ReaderDatabase
import io.github.sommepo.furoshiki.reader.ui.AnkiSetupActivity
import io.github.sommepo.furoshiki.reader.ui.CrtSettingsActivity
import io.github.sommepo.furoshiki.reader.ui.DictionaryManagerActivity
import io.github.sommepo.furoshiki.reader.ui.ReaderGamesActivity
import io.github.sommepo.furoshiki.reader.ui.ReaderSettingsActivity
import io.github.sommepo.furoshiki.reader.ui.ReaderStartActivity
import io.github.sommepo.furoshiki.reader.ui.ReaderStatsActivity
import org.json.JSONArray
import org.json.JSONObject

/**
 * The home page: a web page (assets/front), since Furoshiki's look is written as CSS (the
 * desktop draws home/themes itself, a browser reads them as they are). This process runs the
 * page and nothing else: games and every screen of the reader run in the game's process.
 */
class HomeActivity : AppCompatActivity() {
    private lateinit var page: WebView
    private val database by lazy { ReaderDatabase(applicationContext) }

    /** The consoles still to be looked through, one at a time. */
    private val scanning = ArrayDeque<String>()

    private val scanned = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            callPage("Front.reload()")
            // The next console's core is for a new process: a service started now would go to
            // the one that is about to end.
            if (!intent.getBooleanExtra(ScanService.EXTRA_LISTED, false)) page.postDelayed({ scanNext() }, ScanService.GONE_MS)
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        page = WebView(this)
        page.setBackgroundColor(android.graphics.Color.BLACK)
        page.settings.javaScriptEnabled = true
        page.settings.allowFileAccess = true
        page.settings.domStorageEnabled = true
        page.webChromeClient = WebChromeClient()
        page.isVerticalScrollBarEnabled = false
        page.addJavascriptInterface(Bridge(), "FuroshikiHost")
        setContentView(page)
        registerReceiver(scanned, IntentFilter(Places.SCANNED), Context.RECEIVER_NOT_EXPORTED)
        page.loadUrl("file:///android_asset/front/index.html")
        scan()
    }

    override fun onDestroy() {
        unregisterReceiver(scanned)
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        callPage("if (window.Front) Front.reload()")
        offerGettingStarted()
        // Back from the scripts: the games they read are listed.
        if (scriptsOpened) {
            scriptsOpened = false
            scan()
        }
    }

    private var scriptsOpened = false

    /** The reader's first questions, once, when there are games to read: never over a game. */
    private fun offerGettingStarted() {
        val kept = getSharedPreferences("home", MODE_PRIVATE)
        if (!hasAccess() || kept.getBoolean(KEY_OFFERED, false)) return
        kept.edit().putBoolean(KEY_OFFERED, true).apply()
        // A build with its settings in it (AppSetup) has only AnkiDroid's leave to ask.
        if (assets.list("").orEmpty().contains("setup.json")) {
            if (packageManager.getLaunchIntentForPackage(AnkiDroidClient.ANKIDROID_PACKAGE) != null) {
                requestPermissions(arrayOf(AnkiDroidClient.PERMISSION), 1)
            }
            return
        }
        startActivity(Intent(this, ReaderStartActivity::class.java))
    }

    private fun callPage(script: String) = runOnUiThread { page.evaluateJavascript(script, null) }

    private fun scan() {
        if (!hasAccess() || scanning.isNotEmpty()) return
        startService(Intent(this, AppSetupService::class.java))
        scanning.addAll(Places.CORES.keys)
        scanNext()
    }

    private fun scanNext() {
        val key = scanning.removeFirstOrNull() ?: return
        startService(Intent(this, ScanService::class.java).putExtra(ScanService.EXTRA_CONSOLE, key))
    }

    private fun hasAccess() = Environment.isExternalStorageManager()

    // A controller works the page as the desktop's: the page takes keys, the rest are told here.
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val name = when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_START -> "Enter"
            KeyEvent.KEYCODE_BUTTON_B -> "Escape"
            KeyEvent.KEYCODE_BUTTON_L1 -> "PageUp"
            KeyEvent.KEYCODE_BUTTON_R1 -> "PageDown"
            KeyEvent.KEYCODE_DPAD_UP -> "ArrowUp"
            KeyEvent.KEYCODE_DPAD_DOWN -> "ArrowDown"
            KeyEvent.KEYCODE_DPAD_LEFT -> "ArrowLeft"
            KeyEvent.KEYCODE_DPAD_RIGHT -> "ArrowRight"
            else -> return super.dispatchKeyEvent(event)
        }
        if (event.action == KeyEvent.ACTION_DOWN) callPage("Front.key('$name')")
        return true
    }

    private val pickFolder = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocumentTree()
    ) { tree: Uri? ->
        val path = tree?.let { Places.pathOfTree(DocumentsContract.getTreeDocumentId(it)) } ?: return@registerForActivityResult
        Places.setFolders(this, (Places.folders(this) + path).distinct())
        scan()
        callPage("Front.reload()")
    }

    private companion object {
        const val KEY_OFFERED = "getting_started_offered"
    }

    /** What the page may ask. Called on a thread of the page's. */
    private inner class Bridge {
        @JavascriptInterface
        fun asset(path: String): String = assets.open("front/$path").bufferedReader().use { it.readText() }

        @JavascriptInterface
        fun themes(): String = JSONArray(assets.list("front/themes").orEmpty().toList()).toString()

        @JavascriptInterface
        fun hasAccess(): Boolean = this@HomeActivity.hasAccess()

        @JavascriptInterface
        fun askAccess() {
            startActivity(
                Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))
            )
        }

        /** The games the last look found, each with what was read of it. */
        @JavascriptInterface
        fun games(): String {
            val list = JSONArray()
            for (key in Places.CORES.keys) {
                try {
                    val found = JSONArray(Places.gamesFile(this@HomeActivity, key).readText())
                    for (i in 0 until found.length()) list.put(found.get(i))
                } catch (_: Exception) {
                    // Not looked through yet.
                }
            }
            val read = database.gameTotals().associateBy { it.game }
            for (i in 0 until list.length()) {
                val game = list.getJSONObject(i)
                val totals = read[game.getString("id")]?.counters ?: continue
                game.put("chars", totals.characters).put("activeMs", totals.activeMs)
                database.dailyHistory(game.getString("id"), 1).firstOrNull()?.let { game.put("last", it.first) }
            }
            return list.toString()
        }

        /** Today, the whole, and the last thirty days. */
        @JavascriptInterface
        fun stats(): String {
            val today = ReadingRules.readingDay(System.currentTimeMillis())
            val counters = { c: io.github.sommepo.furoshiki.reader.stats.ReadingCounters ->
                JSONObject().put("chars", c.characters).put("activeMs", c.activeMs)
                    .put("text", SessionSummary.format(c))
            }
            val days = JSONArray()
            for ((day, c) in database.dailyHistory(null, 30)) {
                days.put(JSONObject().put("day", day).put("chars", c.characters))
            }
            val games = JSONArray()
            for (game in database.gameTotals()) {
                games.put(counters(game.counters).put("title", game.title))
            }
            return JSONObject().put("today", counters(database.dayTotals(today)))
                .put("total", counters(database.totals())).put("days", days).put("games", games).toString()
        }

        @JavascriptInterface
        fun folders(): String = JSONArray(Places.folders(this@HomeActivity)).toString()

        @JavascriptInterface
        fun addFolder() = runOnUiThread { pickFolder.launch(null) }

        @JavascriptInterface
        fun removeFolder(path: String) {
            Places.setFolders(this@HomeActivity, Places.folders(this@HomeActivity) - path)
            scan()
        }

        @JavascriptInterface
        fun scan() = this@HomeActivity.scan()

        @JavascriptInterface
        fun play(path: String, console: String, game: String) {
            startActivity(
                Intent(this@HomeActivity, GameActivity::class.java)
                    .putExtra(GameActivity.EXTRA_PATH, path).putExtra(GameActivity.EXTRA_CONSOLE, console)
                    .putExtra(GameActivity.EXTRA_GAME, game)
            )
        }

        /** The consoles whose cores have said what they understand, in the order they are played. */
        @JavascriptInterface
        fun consoles(): String {
            val list = JSONArray()
            for (key in Places.CORES.keys) {
                val file = Places.coreFile(this@HomeActivity, key)
                if (!file.isFile) continue
                val about = JSONObject(file.readText())
                list.put(
                    JSONObject().put("key", key).put("short", about.optString("short", key))
                        .put("system", about.optString("system"))
                )
            }
            return list.toString()
        }

        /**
         * A layer of the settings: a console's what its core understands, all consoles' what
         * they have in common, each once.
         */
        @JavascriptInterface
        fun layer(console: String, game: String): String {
            val settings = if (console.isNotEmpty()) {
                Places.coreSettings(this@HomeActivity, console)
            } else {
                Places.CORES.keys.flatMap { Places.coreSettings(this@HomeActivity, it) }
                    .filter { it.common }.distinctBy { it.key }
            }
            return Places.playSettings(this@HomeActivity).page(settings.filter { !it.hidden }, console, game).toString()
        }

        /** Sets a setting in a layer; an empty value takes it out. */
        @JavascriptInterface
        fun setSetting(key: String, value: String, console: String, game: String) =
            Places.playSettings(this@HomeActivity).set(key, value, console, game)

        /** The Game scripts screen with one console's published list. */
        @JavascriptInterface
        fun openScripts(console: String) {
            scriptsOpened = true
            startActivity(Intent(this@HomeActivity, ScriptsActivity::class.java).putExtra(ScriptsActivity.EXTRA_CONSOLE, console))
        }

        @JavascriptInterface
        fun open(screen: String) {
            val activity = when (screen) {
                "settings" -> ReaderSettingsActivity::class.java
                "dictionaries" -> DictionaryManagerActivity::class.java
                "anki" -> AnkiSetupActivity::class.java
                "crt" -> CrtSettingsActivity::class.java
                "scripts" -> ReaderGamesActivity::class.java
                "statistics" -> ReaderStatsActivity::class.java
                else -> return
            }
            startActivity(Intent(this@HomeActivity, activity))
        }
    }
}
