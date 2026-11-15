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
import io.github.sommepo.furoshiki.reader.ButtonMap
import io.github.sommepo.furoshiki.reader.ReaderSettings
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
import kotlin.concurrent.thread

/**
 * The home page: a web page (assets/front), since Furoshiki's look is written as CSS (the
 * desktop draws home/themes itself, a browser reads them as they are). This process runs the
 * page and nothing else: games and every screen of the reader run in the game's process.
 */
class HomeActivity : AppCompatActivity() {
    private lateinit var page: WebView

    /** Started on the second display and gone to the first instead (onCreate): nothing of it was set up. */
    private var moved = false
    private val database by lazy { ReaderDatabase(applicationContext) }
    private val labelsFile by lazy { java.io.File(filesDir, "labels.json") }

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
        // The page belongs on the first display: the second display's is LowerActivity. Started on
        // the second (a launcher icon tapped down there), it opens on the first instead and leaves
        // nothing behind. A stray copy of it there, hidden, silenced every sound on the AYN Thor
        // (2026-11-15): that device's game assistant counts each of an app's tasks on the second
        // display, and holding audio focus takes only one count away (AudioFocus).
        val shownOn = display?.displayId ?: android.view.Display.DEFAULT_DISPLAY
        if (shownOn != android.view.Display.DEFAULT_DISPLAY) {
            val again = Intent(intent).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            runCatching {
                startActivity(again, android.app.ActivityOptions.makeBasic().setLaunchDisplayId(android.view.Display.DEFAULT_DISPLAY).toBundle())
            }
            moved = true
            finishAndRemoveTask()
            page = WebView(this)
            return
        }
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
        homeAsked(intent)
        playPinned(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        homeAsked(intent)
        playPinned(intent)
        // The Home key: the shelf, at the game in play, from wherever it was pressed.
        if (intent.hasCategory(Intent.CATEGORY_HOME)) callPage("if (window.Front) Front.home()")
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

    /** The OCR engine: as the build has it by default, the service, or this device. */
    private fun pickOcr() {
        val themed = android.view.ContextThemeWrapper(this, R.style.Theme_Reader)
        val settings = io.github.sommepo.furoshiki.ocr.OcrSettings.read(this)
        val device = io.github.sommepo.furoshiki.ocr.OcrSource.deviceAvailable(this)
        val choices = listOfNotNull(
            "" to (if (io.github.sommepo.furoshiki.ocr.OcrSettings.dev) "Default: the OCR service where one is known, else this device" else "Default: this device"),
            "service" to "The OCR service (your home server, or the OCR helper on a computer)",
            ("device" to "This device (nothing is sent anywhere)").takeIf { device },
        )
        androidx.appcompat.app.AlertDialog.Builder(themed)
            .setTitle("OCR engine")
            .setSingleChoiceItems(choices.map { it.second }.toTypedArray(), choices.indexOfFirst { it.first == settings.engine }.coerceAtLeast(0)) { dialog, which ->
                io.github.sommepo.furoshiki.ocr.OcrSettings.write(this, settings.copy(engine = choices[which].first))
                dialog.dismiss()
                callPage("Front.reload()")
            }
            .show()
    }

    /** The OCR service's address and token, typed; left empty, the one the paired home server names. */
    private fun typeOcr() {
        val themed = android.view.ContextThemeWrapper(this, R.style.Theme_Reader)
        val settings = io.github.sommepo.furoshiki.ocr.OcrSettings.read(this)
        val named = Services.of(this, "furochibi")
        val address = android.widget.EditText(themed).apply {
            hint = named.ifEmpty { "http://computer:8794" }
            setText(settings.address)
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI
            isSingleLine = true
        }
        val token = android.widget.EditText(themed).apply {
            hint = "Token (the OCR helper's)"
            setText(settings.token)
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            isSingleLine = true
        }
        val box = android.widget.LinearLayout(themed).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad / 2, pad, 0)
            addView(address)
            addView(token)
        }
        androidx.appcompat.app.AlertDialog.Builder(themed)
            .setTitle("OCR service")
            .setMessage("Where a game's text area is sent to be read. Left empty: " + named.ifEmpty { "none is known" } + ".")
            .setView(box)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                io.github.sommepo.furoshiki.ocr.OcrSettings.write(
                    this,
                    settings.copy(address = io.github.sommepo.furoshiki.ocr.OcrSettings.clean(address.text.toString()), token = token.text.toString().trim())
                )
                callPage("Front.reload()")
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }


    /**
     * The Settings row's switch from outside (tools/thor.sh home on: adb may not enable an app's
     * components itself). It only offers Furoshiki as a home app or takes the offer back; which
     * home app is used stays Android's choice.
     */
    private fun homeAsked(intent: Intent?) {
        if (intent?.action != HomeScreen.ACTION_OFFER) return
        intent.action = null
        HomeScreen.offer(this, intent.getBooleanExtra("on", true))
    }

    /** A game's shortcut on the home screen was tapped: the game, at once. */
    private fun playPinned(intent: Intent?) {
        if (intent?.action != Places.ACTION_PLAY) return
        val path = intent.getStringExtra(GameActivity.EXTRA_PATH) ?: return
        intent.action = null // once: not again when the page comes back
        if (!java.io.File(path).exists()) return
        startGame(path, intent.getStringExtra(GameActivity.EXTRA_CONSOLE).orEmpty(), intent.getStringExtra(GameActivity.EXTRA_GAME).orEmpty())
    }

    private fun startGame(path: String, console: String, game: String) {
        startActivity(
            HomeScreen.apart(
                this,
                Intent(this, GameActivity::class.java).putExtra(GameActivity.EXTRA_PATH, path)
                    .putExtra(GameActivity.EXTRA_CONSOLE, console).putExtra(GameActivity.EXTRA_GAME, game)
            )
        )
    }

    /**
     * The game chosen while another was in play, once that one's process is gone: a core is
     * never unloaded, so the next game is for a new process.
     */
    private fun startNext() {
        val next = HomeScreen.takeNext(this) ?: return
        var waited = 0
        val look = object : Runnable {
            override fun run() {
                if (HomeScreen.gameProcess(this@HomeActivity) && waited < 40) {
                    waited++
                    page.postDelayed(this, 100)
                } else {
                    startGame(next.optString("path"), next.optString("console"), next.optString("game"))
                }
            }
        }
        page.postDelayed(look, 150)
    }

    // Behind a game or another app the page draws nothing and its idle motion stands still.
    override fun onStart() {
        super.onStart()
        page.onResume()
    }

    override fun onStop() {
        page.onPause()
        super.onStop()
    }

    override fun onDestroy() {
        if (!moved) unregisterReceiver(scanned)
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // Asked again with this window in focus: filed under the first display (AudioFocus.take).
        if (hasFocus) AudioFocus.take(this, game = false, again = true)
    }

    override fun onResume() {
        super.onResume()
        AudioFocus.take(this, game = false)
        steam.start()
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        front = this
        isHome = HomeScreen.chosen(this)
        // A handheld with two displays (the Thor) stays as it is held, on its side: its second display
        // cannot turn, so the first never does either (the user, 2026-11-15), whatever the sensor says.
        requestedOrientation = if (wideDevice()) android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        else android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        showLower()
        callPage("if (window.Front) Front.reload()")
        startNext()
        offerGettingStarted()
        // Back from the scripts: the games they read are listed.
        if (scriptsOpened) {
            scriptsOpened = false
            scan()
        }
    }

    /** The Steam Controller, read by Furoshiki itself (steam/): the page's controller like any other. */
    private val steamInput by lazy { io.github.sommepo.furoshiki.steam.SteamInput(this, inGame = false) }
    private val steam by lazy { io.github.sommepo.furoshiki.steam.SteamUsb(this, steamInput) }

    override fun onPause() {
        AudioFocus.give(this)
        steamInput.releaseAll()
        steam.stop()
        // The second display is a game's from here (its trackpad, a 3DS's lower screen).
        if (front === this) front = null
        hideLower()
        LowerActivity.live?.tell()
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
        startActivity(HomeScreen.apart(this, Intent(this, ReaderStartActivity::class.java)))
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

    /** A handheld of two displays, one over the other: held on its side, always. */
    private fun wideDevice(): Boolean = secondDisplay() != null

    /** Furoshiki is the device's home screen: looked up as the page comes to the front. */
    private var isHome = false

    private fun showLower() {
        if (isHome) {
            // The home screen: the second display has its own page for good (LowerActivity),
            // Android's home of that display. Started here where Android did not start it.
            hideLower()
            val there = LowerActivity.live
            if (there != null) {
                there.tell()
            } else {
                val display = secondDisplay() ?: return
                runCatching {
                    startActivity(
                        Intent(this, LowerActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                        android.app.ActivityOptions.makeBasic().setLaunchDisplayId(display.displayId).toBundle()
                    )
                }
            }
            return
        }
        if (lower != null) return
        val display = secondDisplay() ?: return
        lower = runCatching { LowerPage(display).also { it.show() } }.getOrNull()
        callPage("if (window.Front) { fit(); Front.reload(); }")
    }

    /** The second display's own page has come: this one is all shelf from here. */
    fun lowerCame() = callPage("if (window.Front) { fit(); Front.reload(); }")

    /** What a finger did on the second display's page. */
    fun remote(what: String) = callPage("Front.remote(${org.json.JSONObject.quote(what)})")

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

    /**
     * While the controller mapping screen is open (map.js) the page has the controller's keys as
     * they come, by their codes: a button is learnt, lit on the drawing, and the screen goes about
     * by them itself (B may be what is learnt, so it is not turned into back here).
     */
    @Volatile
    private var rawKeys = false

    /** The triggers as buttons, for a controller that has them as axes only. */
    private val triggerDown = booleanArrayOf(false, false)
    private val hatDown = booleanArrayOf(false, false, false, false)

    private fun rawKey(event: KeyEvent): Boolean {
        val code = event.keyCode
        val ours = KeyEvent.isGamepadButton(code) || code == KeyEvent.KEYCODE_BACK ||
            code in KeyEvent.KEYCODE_DPAD_UP..KeyEvent.KEYCODE_DPAD_CENTER
        if (!ours) return false
        if (event.repeatCount == 0 && (event.action == KeyEvent.ACTION_DOWN || event.action == KeyEvent.ACTION_UP)) {
            callPage("Front.pad($code, ${event.action == KeyEvent.ACTION_DOWN})")
        }
        return true
    }

    override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean {
        if (rawKeys && event.source and android.view.InputDevice.SOURCE_CLASS_JOYSTICK != 0) {
            val sides = listOf(
                KeyEvent.KEYCODE_BUTTON_L2 to maxOf(event.getAxisValue(android.view.MotionEvent.AXIS_LTRIGGER), event.getAxisValue(android.view.MotionEvent.AXIS_BRAKE)),
                KeyEvent.KEYCODE_BUTTON_R2 to maxOf(event.getAxisValue(android.view.MotionEvent.AXIS_RTRIGGER), event.getAxisValue(android.view.MotionEvent.AXIS_GAS)),
            )
            sides.forEachIndexed { at, (code, value) ->
                val down = value > 0.5f
                if (down != triggerDown[at]) {
                    triggerDown[at] = down
                    callPage("Front.pad($code, $down)")
                }
            }
            // A d-pad that is a hat: its four directions as buttons, here, so that nothing
            // else takes it for a move about the page (the system's own keys for it never come).
            val hat = listOf(
                KeyEvent.KEYCODE_DPAD_LEFT to (event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X) < -0.5f),
                KeyEvent.KEYCODE_DPAD_RIGHT to (event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X) > 0.5f),
                KeyEvent.KEYCODE_DPAD_UP to (event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y) < -0.5f),
                KeyEvent.KEYCODE_DPAD_DOWN to (event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y) > 0.5f),
            )
            hat.forEachIndexed { at, (code, down) ->
                if (down != hatDown[at]) {
                    hatDown[at] = down
                    callPage("Front.pad($code, $down)")
                }
            }
            // The sticks too stay here: the screen is the controller's alone while it is open.
            return true
        }
        return super.dispatchGenericMotionEvent(event)
    }

    // A controller works the page as the desktop's: the page takes keys, the rest are told here.
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (rawKeys && rawKey(event)) return true
        // The home screen has nowhere to go back to: back is the page's own, as B is.
        if (event.keyCode == KeyEvent.KEYCODE_BACK && isHome) {
            if (event.action == KeyEvent.ACTION_UP) callPage("Front.key('Escape')")
            return true
        }
        val name = when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> "Enter"
            // Start plays the title in hand at once; A opens its page first, as on a computer.
            KeyEvent.KEYCODE_BUTTON_START -> "Play"
            KeyEvent.KEYCODE_BUTTON_B -> "Escape"
            KeyEvent.KEYCODE_BUTTON_Y -> "Other"
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

    /** The GPU's name, asked once (an OpenGL context made for it). */
    @Volatile
    private var gpuName: String? = null

    companion object {
        private const val KEY_OFFERED = "getting_started_offered"

        /** The page while it is in front: what the second display's own page talks to. */
        @Volatile
        var front: HomeActivity? = null
    }

    /** What the page may ask. Called on a thread of the page's. */
    private inner class Bridge {
        @JavascriptInterface
        fun asset(path: String): String = assets.open("front/$path").bufferedReader().use { it.readText() }

        /** Whether a second display shows the title in hand and the keys (the page is then all shelf). */
        @JavascriptInterface
        fun twoScreens(): Boolean = lower != null || LowerActivity.live != null

        @JavascriptInterface
        fun lower(json: String) {
            lowerShown = json
            LowerActivity.lastShown = json
            runOnUiThread {
                lower?.tell(json)
                LowerActivity.live?.tell()
            }
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
                    // One entry a game, its discs as one, also in a list made before that was so.
                    val found = ScanService.oneEntryAGame(JSONArray(Places.gamesFile(this@HomeActivity, key).readText()))
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
                .put("total", counters(database.totals())).put("days", days).put("games", games)
                .put("daysRead", database.dailyHistory(null, 100_000).count { it.second.characters > 0 }).toString()
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
        fun play(path: String, console: String, game: String) = startGame(path, console, game)

        /** The game in play behind the page as `{path, console, game}`, or empty. */
        @JavascriptInterface
        fun playing(): String = HomeScreen.playing(this@HomeActivity)?.toString().orEmpty()

        // ---- The home screen of the device (HomeScreen, Device) ----

        /** "off", "on", or "waiting" while Android still has another home app chosen. */
        @JavascriptInterface
        fun homeScreen(): String = when {
            HomeScreen.chosen(this@HomeActivity) -> "on"
            HomeScreen.offered(this@HomeActivity) -> "waiting"
            else -> "off"
        }

        /** Offers Furoshiki as the home app, or takes the offer back; Android's own page chooses. */
        @JavascriptInterface
        fun setHomeScreen(on: Boolean) = runOnUiThread {
            HomeScreen.offer(this@HomeActivity, on)
            if (on && !HomeScreen.chosen(this@HomeActivity)) Device.open(this@HomeActivity, "home")
            callPage("Front.reload()")
        }

        @JavascriptInterface
        fun status(): String = Device.status(this@HomeActivity).put("dim", Device.mayDim(this@HomeActivity))
            .put("storage", Device.storage(this@HomeActivity)).toString()

        @JavascriptInterface
        fun volume(direction: Int): Int = Device.stepVolume(this@HomeActivity, direction)

        @JavascriptInterface
        fun brightness(direction: Int): Int = Device.stepBrightness(this@HomeActivity, direction)

        @JavascriptInterface
        fun apps(): String = Device.apps(this@HomeActivity).toString()

        @JavascriptInterface
        fun openApp(name: String, activity: String) = runOnUiThread { Device.openApp(this@HomeActivity, name, activity) }

        /** One of Android's own screens, by a name of ours (Device.open). */
        @JavascriptInterface
        fun system(what: String) = runOnUiThread { Device.open(this@HomeActivity, what) }

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

        // ---- What a title is: its record, the study tracker's figures, what was read of it here ----

        /** The title records by game ID (tools/titles-index.py: English names, people, box pictures), or `{}`. */
        @JavascriptInterface
        fun titles(): String = runCatching { asset("titles.json") }.getOrDefault("{}")

        /** What was read of a game on this device: its last thirty days, the newest first, and how many lines are kept. */
        @JavascriptInterface
        fun reading(game: String): String {
            val days = JSONArray()
            for ((day, c) in database.dailyHistory(game, 30)) days.put(JSONObject().put("day", day).put("chars", c.characters))
            return JSONObject().put("days", days).put("lines", database.lineCount(game)).toString()
        }

        /** The Log: the lines read of a game, as the reader kept them. */
        @JavascriptInterface
        fun openLog(game: String) = startActivity(
            HomeScreen.apart(
                this@HomeActivity,
                Intent(this@HomeActivity, io.github.sommepo.furoshiki.reader.ui.ReaderBacklogActivity::class.java)
                    .putExtra(io.github.sommepo.furoshiki.reader.ui.ReaderBacklogActivity.EXTRA_GAME, game)
            )
        )


        // ---- Buttons for the keys, widescreen, the services, what a row is called ----

        /** The actions done over a game, each with the button given to it (ActionKeys). */
        @JavascriptInterface
        fun actionKeys(): String {
            val assigned = ActionKeys.assigned(this@HomeActivity)
            val list = JSONArray()
            for (action in ActionKeys.ALL) {
                list.put(JSONObject().put("id", action.id).put("label", getString(action.label)).put("key", assigned[action.id]?.let(ActionKeys::name).orEmpty()))
            }
            return list.toString()
        }

        @JavascriptInterface
        fun pickActionKeys() = runOnUiThread { ActionKeys.pick(this@HomeActivity) { callPage("Front.reload()") } }

        private fun wideSettings() = Wide.settings(Places.CORES.keys.associateWith { Places.coreSettings(this@HomeActivity, it) })

        /** Widescreen as one row: `{value, choices, consoles}`; no choices where no core has a wide picture. */
        @JavascriptInterface
        fun wide(): String {
            val wide = wideSettings()
            val names = wide.keys.map { key ->
                runCatching { JSONObject(Places.coreFile(this@HomeActivity, key).readText()).optString("short", key) }.getOrDefault(key)
            }
            return JSONObject().put("value", Wide.value(wide, Places.playSettings(this@HomeActivity)))
                .put("choices", JSONArray(Wide.choices(wide))).put("consoles", JSONArray(names)).toString()
        }

        @JavascriptInterface
        fun setWide(shape: String) = Wide.set(wideSettings(), Places.playSettings(this@HomeActivity), shape)

        // ---- OCR: the engine a game without a script is read with (ocr/OcrSettings) ----

        /** `{chosen, using, address, named, token, device}`: the choice, what it comes to now, the service's address typed and named. */
        @JavascriptInterface
        fun ocr(): String {
            val settings = io.github.sommepo.furoshiki.ocr.OcrSettings.read(this@HomeActivity)
            val named = Services.of(this@HomeActivity, "furochibi")
            val device = io.github.sommepo.furoshiki.ocr.OcrSource.deviceAvailable(this@HomeActivity)
            val using = io.github.sommepo.furoshiki.ocr.OcrSettings.choose(
                settings.engine, io.github.sommepo.furoshiki.ocr.OcrSettings.dev, settings.address.isNotEmpty() || named.isNotEmpty(), device
            )
            return JSONObject().put("chosen", settings.engine).put("using", using?.id.orEmpty()).put("address", settings.address)
                .put("named", named).put("token", settings.token.isNotEmpty()).put("device", device).toString()
        }

        @JavascriptInterface
        fun pickOcrEngine() = runOnUiThread { pickOcr() }

        @JavascriptInterface
        fun typeOcrService() = runOnUiThread { typeOcr() }


        /** What the user called the rows, as `{"Page/Name": "label"}` (the desktop's program/labels.json). */
        @JavascriptInterface
        fun labels(): String = runCatching { labelsFile.readText() }.getOrDefault("{}")

        /** A label set; an empty one puts the row back; an empty key puts every row back. */
        @JavascriptInterface
        fun setLabel(key: String, label: String) {
            val kept = runCatching { JSONObject(labelsFile.readText()) }.getOrDefault(JSONObject())
            when {
                key.isEmpty() -> labelsFile.delete()
                label.isBlank() -> labelsFile.writeText(kept.apply { remove(key) }.toString(1))
                else -> labelsFile.writeText(kept.put(key, label.trim()).toString(1))
            }
        }

        @JavascriptInterface
        fun askLabel(key: String, now: String) = runOnUiThread {
            val themed = android.view.ContextThemeWrapper(this@HomeActivity, R.style.Theme_Reader)
            val field = android.widget.EditText(themed).apply {
                setText(now)
                isSingleLine = true
                selectAll()
            }
            val box = android.widget.FrameLayout(themed).apply {
                val pad = (20 * resources.displayMetrics.density).toInt()
                setPadding(pad, pad / 2, pad, 0)
                addView(field)
            }
            androidx.appcompat.app.AlertDialog.Builder(themed)
                .setTitle(key.substringAfter('/'))
                .setMessage("What it is to be called. Left empty: as it was built.")
                .setView(box)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    setLabel(key, field.text.toString())
                    callPage("Front.reload()")
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        @JavascriptInterface
        fun copyLabels() = runOnUiThread {
            getSystemService(android.content.ClipboardManager::class.java)
                .setPrimaryClip(android.content.ClipData.newPlainText("Furoshiki labels", labels()))
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
            startActivity(HomeScreen.apart(this@HomeActivity, Intent(this@HomeActivity, ScriptsActivity::class.java).putExtra(ScriptsActivity.EXTRA_CONSOLE, console)))
        }

        // ---- The controller mapping screen (map.js; the model is ButtonMap, in the reader's settings) ----

        /** The controller's keys handed to the page as they come, or as the page's names again. */
        @JavascriptInterface
        fun listenRaw(on: Boolean) {
            rawKeys = on
            triggerDown.fill(false)
        }

        /** What each of [ids] (a JSON list of action ids) is on in [console]'s layer ("" for all consoles). */
        @JavascriptInterface
        fun buttonMap(console: String, ids: String): String {
            GameActivity.migratePadMap(this@HomeActivity)
            val values = ReaderSettings.fresh(this@HomeActivity).all
            val layer = console.ifEmpty { null }
            // The game's own buttons: the controller's button (an input's id) that presses each.
            val pad = JSONObject()
            for (button in ButtonMap.PAD_BUTTONS) {
                val binding = ButtonMap.padBinding(values, button, layer)
                pad.put(button, JSONObject().put("source", binding.source).put("own", binding.own).put("changed", binding.changed))
            }
            val actions = JSONObject()
            val list = JSONArray(ids)
            for (i in 0 until list.length()) {
                val id = list.getString(i)
                val binding = ButtonMap.binding(values, id, layer)
                actions.put(id, JSONObject().put("button", binding.button).put("own", binding.own))
            }
            return JSONObject().put("actions", actions).put("pad", pad).put("own", layer != null && ButtonMap.hasOwn(values, console)).toString()
        }

        /** Puts an action on a button; false for a button it cannot be on (the pointer's). */
        @JavascriptInterface
        fun assignButton(action: String, button: Int, console: String): Boolean =
            ButtonMap.assign(ReaderSettings.fresh(this@HomeActivity), action, button, console.ifEmpty { null })

        @JavascriptInterface
        fun clearButton(action: String, console: String) =
            ButtonMap.clear(ReaderSettings.fresh(this@HomeActivity), action, console.ifEmpty { null })

        /** The console's [button] pressed by the controller's key [code]; what was on it takes the other's. */
        @JavascriptInterface
        fun assignPad(button: String, code: Int, console: String): Boolean {
            val input = ButtonMap.padInput(code) ?: return false
            return ButtonMap.assignPad(ReaderSettings.fresh(this@HomeActivity), button, input, console.ifEmpty { null })
        }

        /** The input wizard's answers ({console button: key code}), written at once. */
        @JavascriptInterface
        fun setPads(json: String, console: String) {
            val asked = org.json.JSONObject(json)
            val given = asked.keys().asSequence().mapNotNull { button ->
                ButtonMap.padInput(asked.optInt(button, -1))?.let { button to it }
            }.toMap()
            ButtonMap.setPads(ReaderSettings.fresh(this@HomeActivity), given, console.ifEmpty { null })
        }

        @JavascriptInterface
        fun clearPad(button: String, console: String) =
            ButtonMap.clearPad(ReaderSettings.fresh(this@HomeActivity), button, console.ifEmpty { null })

        @JavascriptInterface
        fun sameAsAll(console: String) {
            if (console.isNotEmpty()) ButtonMap.sameAsAll(ReaderSettings.fresh(this@HomeActivity), console)
        }

        @JavascriptInterface
        fun open(screen: String) {
            // One page of the reader's settings: "settings:READING", by its section's name.
            if (screen.startsWith("settings:")) {
                val section = ReaderSettingsActivity.Section.values().firstOrNull { it.name == screen.substringAfter(':') } ?: return
                startActivity(HomeScreen.apart(this@HomeActivity, ReaderSettingsActivity.intent(this@HomeActivity, section)))
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
            startActivity(HomeScreen.apart(this@HomeActivity, Intent(this@HomeActivity, activity)))
        }
    }
}
