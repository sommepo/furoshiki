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
        playPinned(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        playPinned(intent)
    }

    /** RetroAchievements: the user and the password, sent once to retroachievements.org for a token. */
    private fun askAchievements() {
        val kept = Places.playSettings(this)
        val themed = android.view.ContextThemeWrapper(this, R.style.Theme_Reader)
        val user = android.widget.EditText(themed).apply {
            hint = "User"
            setText(kept.raw("achievements.user"))
            isSingleLine = true
        }
        val password = android.widget.EditText(themed).apply {
            hint = "Password"
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            isSingleLine = true
        }
        val box = android.widget.LinearLayout(themed).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad / 2, pad, 0)
            addView(user)
            addView(password)
        }
        androidx.appcompat.app.AlertDialog.Builder(themed)
            .setTitle("RetroAchievements")
            .setMessage("For the cores that have achievements. The password is sent to retroachievements.org once; only the token it gives is kept.")
            .setView(box)
            .setPositiveButton("Log in") { _, _ ->
                val name = user.text.toString().trim()
                val secret = password.text.toString()
                if (name.isEmpty() || secret.isEmpty()) return@setPositiveButton
                Thread {
                    val said = runCatching {
                        val form = "r=login2&u=" + java.net.URLEncoder.encode(name, "UTF-8") + "&p=" + java.net.URLEncoder.encode(secret, "UTF-8")
                        val link = java.net.URL("https://retroachievements.org/dorequest.php").openConnection() as java.net.HttpURLConnection
                        link.requestMethod = "POST"
                        link.doOutput = true
                        link.connectTimeout = 10000
                        link.readTimeout = 15000
                        link.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                        link.setRequestProperty("User-Agent", "Furoshiki/1")
                        link.outputStream.use { it.write(form.toByteArray()) }
                        val answer = JSONObject((if (link.responseCode < 400) link.inputStream else link.errorStream).bufferedReader().use { it.readText() })
                        val token = answer.optString("Token")
                        if (answer.optBoolean("Success") && token.isNotEmpty()) {
                            kept.set("achievements.user", name)
                            kept.set("achievements.token", token)
                            "Logged in as $name"
                        } else {
                            "Not logged in: " + answer.optString("Error", "no answer")
                        }
                    }.getOrElse { "Not logged in: ${it.message}" }
                    callPage("say(${JSONObject.quote(said)}); Front.reload()")
                }.start()
            }
            .setNeutralButton("Log out") { _, _ ->
                kept.set("achievements.user", "")
                kept.set("achievements.token", "")
                callPage("Front.reload()")
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    /** A game's shortcut on the home screen was tapped: the game, at once. */
    private fun playPinned(intent: Intent?) {
        if (intent?.action != Places.ACTION_PLAY) return
        val path = intent.getStringExtra(GameActivity.EXTRA_PATH) ?: return
        intent.action = null // once: not again when the page comes back
        if (!java.io.File(path).exists()) return
        startActivity(
            Intent(this, GameActivity::class.java).putExtra(GameActivity.EXTRA_PATH, path)
                .putExtra(GameActivity.EXTRA_CONSOLE, intent.getStringExtra(GameActivity.EXTRA_CONSOLE))
                .putExtra(GameActivity.EXTRA_GAME, intent.getStringExtra(GameActivity.EXTRA_GAME))
        )
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
        showLower()
        callPage("if (window.Front) Front.reload()")
        offerGettingStarted()
        // Back from the scripts: the games they read are listed.
        if (scriptsOpened) {
            scriptsOpened = false
            scan()
        }
    }

    override fun onPause() {
        // The second display is a game's from here (its trackpad, a 3DS's lower screen).
        hideLower()
        super.onPause()
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

    // ---- A second display (the Thor's lower screen): the title in hand and the keys ----

    /** The page of the second display, while this page is in front and there is one. */
    private var lower: LowerPage? = null

    /** What the first page last said the second shows: a page shown later starts with it. */
    private var lowerShown = ""

    private fun secondDisplay() = getSystemService(android.hardware.display.DisplayManager::class.java)
        .getDisplays(android.hardware.display.DisplayManager.DISPLAY_CATEGORY_PRESENTATION).firstOrNull()

    @SuppressLint("SetJavaScriptEnabled")
    private inner class LowerPage(display: android.view.Display) : android.app.Presentation(this@HomeActivity, display) {
        val view = WebView(context)

        override fun onCreate(state: Bundle?) {
            super.onCreate(state)
            view.setBackgroundColor(android.graphics.Color.BLACK)
            view.settings.javaScriptEnabled = true
            view.settings.allowFileAccess = true
            view.isVerticalScrollBarEnabled = false
            view.addJavascriptInterface(LowerBridge(), "FuroshikiHost")
            view.webViewClient = object : android.webkit.WebViewClient() {
                override fun onPageFinished(page: WebView, url: String) = tell(lowerShown)
            }
            setContentView(view)
            view.loadUrl("file:///android_asset/front/index.html#lower")
        }

        fun tell(json: String) {
            if (json.isNotEmpty()) view.evaluateJavascript("if (window.Lower) Lower.show(${org.json.JSONObject.quote(json)})", null)
        }
    }

    /** What the second display's page may ask: only to tell the first what a finger did. */
    private inner class LowerBridge {
        @JavascriptInterface
        fun remote(what: String) = callPage("Front.remote(${org.json.JSONObject.quote(what)})")
    }

    private fun showLower() {
        if (lower != null) return
        val display = secondDisplay() ?: return
        lower = runCatching { LowerPage(display).also { it.show() } }.getOrNull()
        callPage("if (window.Front) { fit(); Front.reload(); }")
    }

    private fun hideLower() {
        lower?.dismiss()
        lower = null
    }

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

        /** Whether a second display shows the title in hand and the keys (the page is then all shelf). */
        @JavascriptInterface
        fun twoScreens(): Boolean = lower != null

        @JavascriptInterface
        fun lower(json: String) {
            lowerShown = json
            runOnUiThread { lower?.tell(json) }
        }

        @JavascriptInterface
        fun themes(): String = JSONArray(assets.list("front/themes").orEmpty().toList()).toString()

        @JavascriptInterface
        fun icon(): String = AppIcon.label(this@HomeActivity)

        @JavascriptInterface
        fun pickIcon() = runOnUiThread { AppIcon.pick(this@HomeActivity) { callPage("Front.reload()") } }

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

        /** Whether games without a script are listed too, and that changed (the folders are looked through again). */
        @JavascriptInterface
        fun allGames(): Boolean = getSharedPreferences("home", MODE_PRIVATE).getBoolean(Places.KEY_ALL_GAMES, false)

        @JavascriptInterface
        fun setAllGames(all: Boolean) {
            getSharedPreferences("home", MODE_PRIVATE).edit().putBoolean(Places.KEY_ALL_GAMES, all).apply()
            runOnUiThread { scan() }
        }

        /** A game on the launcher's home screen: tapped, it starts at once. */
        @JavascriptInterface
        fun pin(path: String, console: String, game: String, title: String) = runOnUiThread {
            val shortcuts = getSystemService(android.content.pm.ShortcutManager::class.java)
            if (shortcuts == null || !shortcuts.isRequestPinShortcutSupported) {
                callPage("say('This launcher takes no shortcuts')")
                return@runOnUiThread
            }
            val start = Intent(this@HomeActivity, HomeActivity::class.java).setAction(Places.ACTION_PLAY)
                .putExtra(GameActivity.EXTRA_PATH, path).putExtra(GameActivity.EXTRA_CONSOLE, console)
                .putExtra(GameActivity.EXTRA_GAME, game)
            val shortcut = android.content.pm.ShortcutInfo.Builder(this@HomeActivity, "game:$console:$game")
                .setShortLabel(title.take(24)).setLongLabel(title)
                .setIcon(android.graphics.drawable.Icon.createWithResource(this@HomeActivity, applicationInfo.icon))
                .setIntent(start).build()
            shortcuts.requestPinShortcut(shortcut, null)
        }

        /** RetroAchievements' user where one is logged in, else empty. */
        @JavascriptInterface
        fun achievementsUser(): String = Places.playSettings(this@HomeActivity).raw("achievements.user")

        /** Asks for the login and keeps the token it gives (the password is sent once and not kept); or logs out. */
        @JavascriptInterface
        fun achievementsLogin() = runOnUiThread { askAchievements() }


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
            // One page of the reader's settings: "settings:READING", by its section's name.
            if (screen.startsWith("settings:")) {
                val section = ReaderSettingsActivity.Section.values().firstOrNull { it.name == screen.substringAfter(':') } ?: return
                startActivity(ReaderSettingsActivity.intent(this@HomeActivity, section))
                return
            }
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
