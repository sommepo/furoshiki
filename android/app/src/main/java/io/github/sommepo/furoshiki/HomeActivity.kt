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

    private val scanned = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            // Add a game's name was asked on the second display (AddTitleActivity): its page is back.
            val added = intent.getBooleanExtra(AddTitleActivity.EXTRA_DONE, false)
            if (added) runCatching { lower?.show() }
            callPage(if (added) "Front.reload()" else "Front.listed()")
            if (intent.getBooleanExtra(ScanService.EXTRA_LISTED, false)) return
            val key = intent.getStringExtra(ScanService.EXTRA_CONSOLE).orEmpty()
            // A console handed back (the process it went to had another's core) is looked at again.
            if (intent.getBooleanExtra(ScanService.EXTRA_AGAIN, false)) {
                if (key.isNotEmpty() && key !in Scans.queue) Scans.queue.addFirst(key)
                // Ours: asked again once the process that refused it has gone.
                if (Scans.inFlight == key && Scans.owner === this@HomeActivity) {
                    Scans.inFlight = null
                    page.postDelayed({ scanNext() }, ScanService.GONE_MS * 3)
                }
                return
            }
            // Only the page that started this look goes on with the next: every page hears it.
            if (Scans.inFlight != key || Scans.owner !== this@HomeActivity) return
            // The next console's core is for a new process: a service started now would go to
            // the one that is about to end.
            Scans.inFlight = null
            page.postDelayed({ scanNext() }, ScanService.GONE_MS)
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
        // No grey veil over the whole page once a key is pressed: Android's default focus highlight
        // on a focused view without its own (the maintainer, 2026-11-17: "washed out" after a press,
        // and it stayed). The page draws its own cursor.
        page.defaultFocusHighlightEnabled = false
        page.settings.javaScriptEnabled = true
        page.settings.allowFileAccess = true
        page.settings.domStorageEnabled = true
        // The page may be pinched larger (its text is small on a phone); no buttons drawn for it.
        page.settings.setSupportZoom(true)
        page.settings.builtInZoomControls = true
        page.settings.displayZoomControls = false
        page.webChromeClient = WebChromeClient()
        page.isVerticalScrollBarEnabled = false
        page.addJavascriptInterface(Bridge(), "FuroshikiHost")
        setContentView(page)
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(scanned, IntentFilter(Places.SCANNED), Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(scanned, IntentFilter(Places.SCANNED))
        }
        page.loadUrl("file:///android_asset/front/index.html")
        // The CRT picture off once for everyone, before its page or a game reads it (CrtSettings).
        io.github.sommepo.furoshiki.reader.ui.CrtSettings.switchOffOnce(this)
        // Once the page is in front (onResume): a service may not be started before (the Thor with
        // its display off, 2026-11-24: the page started by an intent ended at once).
        scanWhenResumed = true
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
            hint = "Username"
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
            .setMessage("Your password is sent once to retroachievements.org and is not stored.")
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
                            "Couldn't log in: " + answer.optString("Error", "no answer") + ". Check the username and password."
                        }
                    }.getOrElse { "Couldn't log in: ${it.message ?: "no answer"}. Check the connection and try again." }
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
            // The default: on this device, but for the dev line where a home server's service is known.
            ("" to "Default (home server's OCR, else on this device)").takeIf { io.github.sommepo.furoshiki.ocr.OcrSettings.dev },
            ("device" to (if (io.github.sommepo.furoshiki.ocr.OcrSettings.dev) "On this device" else "On this device (default)")).takeIf { device },
            "lens" to "Google Lens (sends text areas to Google)",
            "service" to "OCR helper on a computer",
        )
        androidx.appcompat.app.AlertDialog.Builder(themed)
            .setTitle("OCR engine")
            .setSingleChoiceItems(
                choices.map { it.second }.toTypedArray(),
                // A published build has no "Default" row: nothing chosen is "On this device (default)".
                choices.indexOfFirst { it.first == settings.engine }.takeIf { it >= 0 }
                    ?: choices.indexOfFirst { settings.engine.isEmpty() && it.first == "device" }
            ) { dialog, which ->
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
            hint = "Token"
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
            .setTitle("OCR helper")
            .setMessage(if (named.isEmpty()) "Address of the OCR helper" else "Address of the OCR helper. Empty: $named")
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
        // Another app's game in play (its companion in the game's process): the companion is told
        // first, and starts the next game itself once it has ended, as a game's process does.
        val playing = HomeScreen.playing(this)
        if (playing?.optString("console") == io.github.sommepo.furoshiki.companion.AppTitles.CONSOLE &&
            playing.optString("path") != path && HomeScreen.gameProcess(this)
        ) {
            startActivity(
                Intent(this, io.github.sommepo.furoshiki.companion.CompanionActivity::class.java)
                    .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_NEXT_PATH, path)
                    .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_NEXT_CONSOLE, console)
                    .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_NEXT_GAME, game)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            return
        }
        if (console == io.github.sommepo.furoshiki.companion.AppTitles.CONSOLE) {
            startCompanion(path, game)
            return
        }
        startActivity(
            HomeScreen.apart(
                this,
                Intent(this, GameActivity::class.java).putExtra(GameActivity.EXTRA_PATH, path)
                    .putExtra(GameActivity.EXTRA_CONSOLE, console).putExtra(GameActivity.EXTRA_GAME, game)
            )
        )
    }

    /**
     * A title another app plays: Furoshiki on the second display as its companion, which asks for
     * the screen and then starts the app on this one (companion/CompanionActivity); on a device
     * with one display, on this one, over the game.
     */
    private fun startCompanion(path: String, game: String) {
        // Only a second display one can see: switched on, not private, not one named like the
        // built-in screen (the reader's own choice, SecondScreen). Phones list virtual displays
        // too (casting, a desktop mode, a recorder), and the companion opened on one of those
        // showed nothing at all: no question about the screen, Start seemed to do nothing (a
        // user's report, 2026-11-19).
        val manager = getSystemService(android.hardware.display.DisplayManager::class.java)
        val main = manager.getDisplay(android.view.Display.DEFAULT_DISPLAY)
        val lower = LowerActivity.live?.display?.displayId
            ?: manager.getDisplays(android.hardware.display.DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
                .firstOrNull {
                    it.displayId != android.view.Display.DEFAULT_DISPLAY && it.isValid &&
                        it.state == android.view.Display.STATE_ON &&
                        it.flags and android.view.Display.FLAG_PRIVATE == 0 &&
                        it.flags and android.view.Display.FLAG_PRESENTATION != 0 &&
                        it.name != main?.name
                }?.displayId
        val list = io.github.sommepo.furoshiki.companion.AppTitles.list(this)
        val title = (0 until list.length()).map { list.getJSONObject(it) }.firstOrNull { it.optString("id") == game }?.optString("title").orEmpty()
        val intent = Intent(this, io.github.sommepo.furoshiki.companion.CompanionActivity::class.java)
            .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_PATH, path)
            .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_GAME, game)
            .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_TITLE, title)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        // A device with one display: the companion on it, over the game (CompanionActivity). A
        // display that will not take it: this one instead.
        android.util.Log.i("Furoshiki", "Companion: starting on display ${lower ?: "main"}")
        if (lower == null) startActivity(intent)
        else runCatching { startActivity(intent, android.app.ActivityOptions.makeBasic().setLaunchDisplayId(lower).toBundle()) }
            .onFailure {
                android.util.Log.w("Furoshiki", "Companion: display $lower refused it (${it.message}); on the main display")
                startActivity(intent)
            }
    }

    /** *Add a title*: its name, for a game [label] ([app], from its [activity]) plays. */
    private fun askTitle(app: String, activity: String, label: String) {
        // On the second display where there is one: the keyboard comes up where the box is, and a
        // handheld's lower display is the one typed on (a new user, 2026-11-19: "when doing add
        // game you should be able to type on the bottom screen"). A display that will not show
        // it: this one.
        val display = lower?.takeIf { it.isShowing }?.display
        val asked = display != null && runCatching {
            // The page there steps aside meanwhile: its window lies over any screen on that display.
            lower?.hide()
            startActivity(
                Intent(this, AddTitleActivity::class.java).putExtra(AddTitleActivity.EXTRA_APP, app)
                    .putExtra(AddTitleActivity.EXTRA_ACTIVITY, activity).putExtra(AddTitleActivity.EXTRA_LABEL, label)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                android.app.ActivityOptions.makeBasic().setLaunchDisplayId(display!!.displayId).toBundle()
            )
        }.isSuccess
        if (!asked) {
            lower?.show()
            askTitleOn(this, app, activity, label)
        }
    }

    private fun askTitleOn(base: Context, app: String, activity: String, label: String) {
        val themed = android.view.ContextThemeWrapper(base, R.style.Theme_Reader)
        val name = android.widget.EditText(themed).apply {
            isSingleLine = true
            inputType = android.text.InputType.TYPE_CLASS_TEXT
        }
        val box = android.widget.FrameLayout(themed).apply {
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad / 2, pad, 0)
            addView(name)
        }
        val dialog = androidx.appcompat.app.AlertDialog.Builder(themed)
            .setTitle(R.string.add_title_name)
            .setMessage(label)
            .setView(box)
            .setPositiveButton(R.string.add_title_add) { _, _ ->
                val title = name.text.toString().trim()
                if (title.isNotEmpty()) {
                    val entry = io.github.sommepo.furoshiki.companion.AppTitles.add(this, title, app, activity, label)
                    callPage("Front.added(" + org.json.JSONObject.quote(entry.optString("path")) + ")")
                }
            }
            .setNegativeButton(R.string.add_title_cancel, null)
            .create()
        dialog.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
        dialog.show()
        name.requestFocus()
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
        // This page's look goes on in the next page that asks for one (scan()).
        if (Scans.owner === this) {
            Scans.owner = null
            Scans.inFlight = null
        }
        if (!moved) unregisterReceiver(scanned)
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // Asked again with this window in focus: filed under the first display (AudioFocus.take).
        if (hasFocus) AudioFocus.take(this, game = false, again = true)
    }

    /** The dictionaries' move into the engine, said and shown on this screen (ConversionPrompt). */
    private val conversion by lazy { ConversionPrompt(this) }

    override fun onResume() {
        super.onResume()
        conversion.resume()
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
        if (scanWhenResumed) {
            scanWhenResumed = false
            scanIfDue()
        }
        startNext()
        offerGettingStarted()
        checkRelease()
        // Back from Android's "install unknown apps" switch, turned on: the update goes on.
        if (installWaiting && Updater.mayInstall(this)) {
            installWaiting = false
            installRelease()
        }
        // Back from the scripts: the games they read are listed, where a script came or went.
        if (scriptsOpened) {
            scriptsOpened = false
            if (scriptsStamp() != scriptsBefore) scan(quiet = true)
        }
    }

    /** The Steam Controller, read by Furoshiki itself (steam/): the page's controller like any other. */
    private val steamInput by lazy { io.github.sommepo.furoshiki.steam.SteamInput(this, inGame = false) }
    private val steam by lazy { io.github.sommepo.furoshiki.steam.SteamUsb(this, steamInput) }

    override fun onPause() {
        keepShelfTheme()
        AudioFocus.give(this)
        steamInput.releaseAll()
        steam.stop()
        // The second display is a game's from here (its trackpad, a 3DS's lower screen).
        if (front === this) front = null
        hideLower()
        LowerActivity.live?.tell()
        conversion.pause()
        super.onPause()
    }

    @Volatile private var scriptsOpened = false
    private var scanWhenResumed = false
    @Volatile private var scriptsBefore = 0L

    /**
     * The page's theme, kept for the reader: the popup takes its look unless the user chose one
     * (ReaderSettings.popupTheme). Read from the page as it leaves the front (a game is next).
     */
    private fun keepShelfTheme() {
        if (!::page.isInitialized) return
        page.evaluateJavascript(
            "(function(){try{return (typeof state!=='undefined'&&state.theme)||localStorage.getItem('look')}catch(e){return null}})()"
        ) { said ->
            val theme = runCatching { org.json.JSONTokener(said).nextValue() as? String }.getOrNull()
            if (!theme.isNullOrEmpty() && theme.matches(Regex("[a-z0-9-]{1,32}"))) {
                val settings = ReaderSettings(this)
                if (settings.shelfTheme != theme) {
                    settings.preferences.edit().putString(ReaderSettings.KEY_SHELF_THEME, theme).apply()
                }
            }
        }
    }

    /** Whether this build carries its own settings (AppSetup): its dictionaries and Anki are set by themselves. */
    private fun setUpByItself() = assets.list("").orEmpty().contains("setup.json")

    /**
     * The reader's first questions. They are steps of the page's one setup now (front.js,
     * runSetup: game folders, dictionaries, Anki, GameNative), no longer a screen of their own
     * beside it. Only a build with its settings in it (AppSetup) is asked here: AnkiDroid's leave.
     */
    private fun offerGettingStarted() {
        val kept = getSharedPreferences("home", MODE_PRIVATE)
        if (!hasAccess() || kept.getBoolean(KEY_OFFERED, false) || !setUpByItself()) return
        kept.edit().putBoolean(KEY_OFFERED, true).apply()
        if (packageManager.getLaunchIntentForPackage(AnkiDroidClient.ANKIDROID_PACKAGE) != null) {
            requestPermissions(arrayOf(AnkiDroidClient.PERMISSION), 1)
        }
    }

    /** Whether one of GameNative's apps is installed: its setup step and its folder's row are for those who have it. */
    private fun hasGameNative(): Boolean = listOf("app.gamenative", "app.gamenative.gold").any {
        runCatching { packageManager.getPackageInfo(it, 0) }.isSuccess
    }

    private fun callPage(script: String) = runOnUiThread { page.evaluateJavascript(script, null) }

    /** Set while Android's "install unknown apps" switch is open for an update. */
    private var installWaiting = false
    @Volatile private var updating = false

    /**
     * The in-app updater's daily look at GitHub (Updater.kt), in a published build only, never while
     * a game plays, nothing said when offline or unanswered. A newer release is said once on the shelf.
     * [asked]: the Settings page's *Check now*, which says what it found.
     */
    private fun checkRelease(asked: Boolean = false) {
        if (!Updater.enabled) return
        if (!asked && (!Updater.due(this) || HomeScreen.playing(this) != null)) return
        kotlin.concurrent.thread(name = "Updater") {
            val found = Updater.check(applicationContext)
            val newer = found.getOrNull()
            when {
                asked && found.isFailure -> callPage("say(${JSONObject.quote("Couldn't check for updates. Try again when online.")})")
                asked && newer == null -> callPage("say('Furoshiki is up to date')")
                newer != null && (asked || !Updater.told(this, newer)) -> {
                    Updater.markTold(this, newer)
                    callPage("say(${JSONObject.quote("Furoshiki ${newer.version} is available in Settings › About")})")
                }
            }
            callPage("if (window.Front) Front.reload()")
        }
    }

    /** The newer release, downloaded and handed to Android's installer, which asks the user. */
    private fun installRelease() {
        val release = Updater.available(this) ?: return callPage("say('Furoshiki is up to date')")
        if (HomeScreen.playing(this) != null) return callPage("say('Quit the game first')")
        if (!Updater.mayInstall(this)) {
            installWaiting = true
            callPage("say('Allow Furoshiki to install apps, then come back')")
            runCatching { Updater.askToInstall(this) }.onFailure {
                installWaiting = false
                callPage("say(${JSONObject.quote("Couldn't install the update: this device doesn't let Furoshiki install apps")})")
            }
            return
        }
        if (updating) return
        updating = true
        kotlin.concurrent.thread(name = "Updater") {
            runCatching {
                Updater.install(applicationContext, release) { percent -> callPage("say(${JSONObject.quote("Downloading update… $percent%")})") }
            }.onFailure {
                android.util.Log.w("Updater", "update failed", it)
                callPage("say(${JSONObject.quote("Couldn't download the update: ${it.message ?: "no answer"}. Try again from Settings › About.")})")
            }
            updating = false
        }
    }

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
            view.defaultFocusHighlightEnabled = false
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

        // One shelf over both displays (the maintainer, 2026-11-17: "i need the menu experience to
        // be one entire thing"): a touch here gives Android's keys to this display, so the
        // controller's buttons and sticks are handed to the shelf, and the shelf takes the keys
        // back a moment after the finger lifts.
        override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean = this@HomeActivity.dispatchKeyEvent(event)

        override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean =
            if (event.source and android.view.InputDevice.SOURCE_CLASS_JOYSTICK != 0) this@HomeActivity.dispatchGenericMotionEvent(event)
            else super.dispatchGenericMotionEvent(event)

        override fun dispatchTouchEvent(event: android.view.MotionEvent): Boolean {
            val taken = super.dispatchTouchEvent(event)
            if (event.actionMasked == android.view.MotionEvent.ACTION_UP) {
                view.postDelayed({
                    // Not once this touch has opened a page of settings on this display (this page
                    // is then hidden): the keys are that page's, so its Back and its rows work
                    // (seen on the Thor, 2026-11-19: Back did not close a settings page there).
                    if (isShowing && onLower == null && !this@HomeActivity.hasWindowFocus()) runCatching {
                        getSystemService(android.app.ActivityManager::class.java).moveTaskToFront(taskId, 0)
                    }
                }, 250)
            }
            return taken
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

    /**
     * The look through the consoles, one at a time, shared by every copy of the page in this
     * process (the home screen's and the app's may both be there): two looks at once sent two
     * consoles to one scan process. A look asked for while one goes on comes after it.
     */
    private object Scans {
        val queue = ArrayDeque<String>()
        var inFlight: String? = null
        var owner: HomeActivity? = null
        var again = false
        var againQuiet = true
        /** Started by the app itself, not asked for: nothing said on the page while it goes on. */
        var quiet = false
    }

    /**
     * A look by the app itself as the page is made, where the last whole look is older than
     * [AUTO_SCAN_MS] or a console has no list yet (the maintainer, 2026-11-24: "android version
     * seems to scan constantly after going back to the menu and is always loudly showing scanning
     * at the bottom, also seems to cause jitter on the main menu"). The page is made again each
     * time Android ended the app while a game was played, and each time every console was looked
     * at. It is quiet: the page says nothing of it.
     */
    private fun scanIfDue() {
        val last = getSharedPreferences("home", MODE_PRIVATE).getLong(KEY_LAST_SCAN, 0)
        val listed = Places.CORES.keys.all { Places.gamesFile(this, it).isFile }
        if (listed && System.currentTimeMillis() - last in 0 until AUTO_SCAN_MS) return
        scan(quiet = listed)
    }

    /** When the user's own scripts last changed: back from Game scripts, a look only where they did. */
    private fun scriptsStamp(): Long =
        java.io.File(getExternalFilesDir(null), io.github.sommepo.furoshiki.reader.ReaderNative.PROFILE_USER_DIR)
            .walkTopDown().maxDepth(2).maxOfOrNull { it.lastModified() } ?: 0L

    private fun scan(quiet: Boolean = false) {
        if (!hasAccess()) return
        importExports()
        // One the user asked for is said, even when it joins a quiet one under way.
        if (!quiet) Scans.quiet = false
        if (Scans.inFlight != null) {
            Scans.againQuiet = if (Scans.again) Scans.againQuiet && quiet else quiet
            Scans.again = true
            return
        }
        // A look left half done by a page that has gone: this one goes on with it.
        if (Scans.queue.isNotEmpty()) {
            Scans.againQuiet = if (Scans.again) Scans.againQuiet && quiet else quiet
            Scans.again = true
            scanNext()
            return
        }
        // Refused while the app is not in front: the next look does it.
        runCatching { startService(Intent(this, AppSetupService::class.java)) }
        Scans.quiet = quiet
        Scans.queue.addAll(Places.CORES.keys)
        scanNext()
    }

    /**
     * The games PC game apps exported for launchers (GameNative's *Frontend Sync*, Winlator's
     * shortcuts) in the game folders, as titles that start their game itself
     * (companion/FrontendExports); the shelf again where they changed.
     */
    private fun importExports() {
        val context = applicationContext
        Thread({
            val changed = runCatching {
                io.github.sommepo.furoshiki.companion.AppTitles.importExports(context, Places.gameFolders(context))
            }.onFailure { android.util.Log.w("Furoshiki", "Exported games: ${it.message}") }.getOrDefault(false)
            if (changed) runOnUiThread { callPage("Front.reload()") }
        }, "exported-games").start()
    }

    private fun scanNext() {
        val key = Scans.queue.removeFirstOrNull()
        // What the scan is at, on the page (a new user, 2026-11-19: "some popup on screen showing
        // the scan is in progress").
        val total = Places.CORES.size
        // By the console's name, never its key ("PS Vita", not "PSVITA").
        callPage("Front.scanning(" + org.json.JSONObject.quote(if (key == null || Scans.quiet) "" else "${Places.shortName(key)} · ${total - Scans.queue.size} of $total") + ")")
        if (key == null) {
            Scans.owner = null
            Scans.quiet = false
            getSharedPreferences("home", MODE_PRIVATE).edit().putLong(KEY_LAST_SCAN, System.currentTimeMillis()).apply()
            if (Scans.again) {
                val nextQuiet = Scans.againQuiet
                Scans.again = false
                Scans.againQuiet = true
                scan(quiet = nextQuiet)
            }
            return
        }
        Scans.inFlight = key
        Scans.owner = this
        // Refused while the app is not in front: the look ends here, and is asked again later.
        val asked = runCatching { startService(Intent(this, ScanService::class.java).putExtra(ScanService.EXTRA_CONSOLE, key)) }
        if (asked.isFailure) {
            android.util.Log.w("Furoshiki", "$key: its look could not start (${asked.exceptionOrNull()?.message})")
            Scans.queue.clear()
            Scans.inFlight = null
            Scans.owner = null
            Scans.quiet = false
            callPage("Front.scanning('')")
            return
        }
        // A look whose process never answers (it died) does not hold the others up.
        val started = key
        page.postDelayed({
            if (Scans.inFlight == started && Scans.owner === this) {
                android.util.Log.w("Furoshiki", "$started: no answer from its look, the next console")
                Scans.inFlight = null
                scanNext()
            }
        }, SCAN_AT_MOST_MS)
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
        // The mapping screen learns buttons by place (FaceButtons); the shelf's own keys stay the device's.
        if (rawKeys && rawKey(FaceButtons.byPlace(event, FaceButtons.nintendo(this)))) return true
        // Back is the page's first: a list or box open over it shuts, then the page goes up one
        // level (a game's page to its shelf, a group of settings to Settings, a page to the
        // library), and only from the top does Back leave the app (a user, 2026-11-19: Back with
        // "Choose app" open went straight to Android's home screen). A page of settings open on
        // the second display is shut before that. The home screen has nowhere to go back to.
        if (event.keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.action == KeyEvent.ACTION_UP && !event.isCanceled) {
                if (closeOnLower()) return true
                page.evaluateJavascript("(window.Front && Front.back) ? Front.back() : false") { took ->
                    if (took != "true" && !isHome) leaveByBack()
                }
            }
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
        // A held A or Start is one press: its repeats started a game again and again (a companion
        // that died at once was started some 150 times in a public user's log, 2026-11-19).
        val once = name == "Enter" || name == "Play" || name == "Other" || name == "Escape"
        if (event.action == KeyEvent.ACTION_DOWN && !(once && event.repeatCount > 0)) callPage("Front.key('$name')")
        return true
    }

    /** Back from the top level: what Android does by itself (the task goes behind, or the page ends). */
    private fun leaveByBack() {
        if (isFinishing || isDestroyed) return
        if (isTaskRoot) moveTaskToBack(true) else finish()
    }

    /**
     * The reader's page that was opened on the second display (settings, dictionaries, a game
     * scripts list), by its class; null when none was. Looked for among the app's tasks when Back
     * comes to this page while that one is still open.
     */
    @Volatile
    private var onLower: String? = null

    /** Shuts the page open on the second display, if one is. True when one was shut. */
    private fun closeOnLower(): Boolean {
        val target = onLower ?: return false
        val tasks = getSystemService(android.app.ActivityManager::class.java)
        val open = runCatching {
            tasks.appTasks.filter { it.taskInfo.baseActivity?.className == target || it.taskInfo.topActivity?.className == target }
        }.getOrDefault(emptyList())
        if (open.isEmpty()) {
            onLower = null
            return false
        }
        open.forEach { task -> runCatching { task.finishAndRemoveTask() } }
        onLower = null
        return true
    }

    private val pickFolder = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocumentTree()
    ) { tree: Uri? ->
        val path = tree?.let { Places.pathOfTree(DocumentsContract.getTreeDocumentId(it)) }
        if (path == null) {
            // Nothing chosen: the page goes on all the same (its setup's next step).
            callPage("Front.reload()")
            return@registerForActivityResult
        }
        Places.setFolders(this, (Places.folders(this) + path).distinct())
        scan()
        callPage("Front.reload()")
    }

    /** The GPU's name, asked once (an OpenGL context made for it). */
    @Volatile
    private var gpuName: String? = null

    companion object {
        private const val KEY_OFFERED = "getting_started_offered"

        /** How long one console's look may take before the next is looked at anyway. */
        private const val SCAN_AT_MOST_MS = 120_000L

        /** A look by the app itself at most this often (scanIfDue); one the user asks for at any time. */
        private const val AUTO_SCAN_MS = 30L * 60 * 1000
        private const val KEY_LAST_SCAN = "last_scan"

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

        /** Whether the device has a second display at all (no on-screen controls there, map.js). */
        @JavascriptInterface
        fun hasSecondDisplay(): Boolean = io.github.sommepo.furoshiki.reader.ReaderSettings.hasSecondDisplay(this@HomeActivity)

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
                    val found = ScanService.oneEntryAGame(JSONArray(Places.gamesFile(this@HomeActivity, key).readText()), ScanService.records(this@HomeActivity))
                    for (i in 0 until found.length()) list.put(found.get(i))
                } catch (_: Exception) {
                    // Not looked through yet.
                }
            }
            // The titles other apps play (Add a title), read as their companion.
            val apps = io.github.sommepo.furoshiki.companion.AppTitles.list(this@HomeActivity)
            for (i in 0 until apps.length()) list.put(apps.get(i))
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
                games.put(counters(game.counters).put("title", game.title).put("id", game.game))
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
        fun play(path: String, console: String, game: String) = runOnUiThread { startGame(path, console, game) }

        /** Another app's game in play: its companion ended, back to the shelf. */
        @JavascriptInterface
        fun quitPlaying() = runOnUiThread {
            if (!HomeScreen.gameProcess(this@HomeActivity)) {
                HomeScreen.setPlaying(this@HomeActivity, null)
                callPage("Front.reload()")
                return@runOnUiThread
            }
            startActivity(
                Intent(this@HomeActivity, io.github.sommepo.furoshiki.companion.CompanionActivity::class.java)
                    .putExtra(io.github.sommepo.furoshiki.companion.CompanionActivity.EXTRA_QUIT, true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }

        /** *Add a title*: a game [label] plays, its name asked for here. */
        @JavascriptInterface
        fun addTitle(app: String, activity: String, label: String) = runOnUiThread { askTitle(app, activity, label) }

        /** A title added for another app, taken off the shelf (what was read of it stays). */
        @JavascriptInterface
        fun forgetTitle(id: String) {
            io.github.sommepo.furoshiki.companion.AppTitles.remove(this@HomeActivity, id)
            callPage("Front.reload()")
        }

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
        fun allGames(): Boolean = getSharedPreferences("home", MODE_PRIVATE).getBoolean(Places.KEY_ALL_GAMES, true)

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
                callPage("say(${JSONObject.quote("Couldn't add to the Home screen: your launcher doesn't support shortcuts")})")
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

        // ---- Haptics, and every setting by its name (the page's "Search settings") ----

        /** Whether a card that lands and a step in the popup are felt (ReaderHaptics; the reader's own setting). */
        @JavascriptInterface
        fun haptics(): Boolean = ReaderSettings.fresh(this@HomeActivity).getBoolean(ReaderSettings.KEY_HAPTICS, true)

        @JavascriptInterface
        fun setHaptics(on: Boolean) {
            ReaderSettings.fresh(this@HomeActivity).edit().putBoolean(ReaderSettings.KEY_HAPTICS, on).commit()
        }

        /** Whether this device can tick at all: the row is for those that can. */
        @JavascriptInterface
        fun hasVibrator(): Boolean = getSystemService(android.os.Vibrator::class.java)?.hasVibrator() == true

        /** Every row of the reader's settings pages, as `[{section, page, key, title}]`, for the page's search. */
        @JavascriptInterface
        fun settingsIndex(): String {
            val list = JSONArray()
            for (found in runCatching { ReaderSettingsActivity.all(this@HomeActivity) }.getOrDefault(emptyList())) {
                list.put(
                    JSONObject().put("section", found.section.name).put("page", getString(found.section.title))
                        .put("key", found.key).put("title", found.title)
                )
            }
            return list.toString()
        }

        /** One of those rows: its page, opened at it. */
        @JavascriptInterface
        fun openSetting(section: String, key: String) {
            val found = ReaderSettingsActivity.Section.values().firstOrNull { it.name == section } ?: return
            startOnLower(HomeScreen.apart(this@HomeActivity, ReaderSettingsActivity.intent(this@HomeActivity, found, key)))
        }

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

        /** The IDs (upper case) of the games read by OCR with an area set: their spine carries glasses. */
        @JavascriptInterface
        fun ocrSet(): String {
            val areas = io.github.sommepo.furoshiki.ocr.OcrAreas(java.io.File(filesDir, "ocr-areas.json"))
            return JSONArray(areas.games().filter { id -> areas.of(id).let { it.on && it.areas.isNotEmpty() } }).toString()
        }

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


        /** A published build's update (Updater.kt): `{mine, version, newer, checked}`, or "" in any other build. */
        @JavascriptInterface
        fun release(): String = Updater.row(this@HomeActivity)

        @JavascriptInterface
        fun checkRelease() = runOnUiThread { this@HomeActivity.checkRelease(asked = true) }

        @JavascriptInterface
        fun installRelease() = runOnUiThread { this@HomeActivity.installRelease() }

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

        /**
         * The setup's steps for this device, in order (front.js, runSetup): the game folders
         * always; dictionaries and Anki unless the build sets them up itself or the older
         * "Getting started" screen already asked (its flag is only read now: the page keeps
         * which steps are done, so the list stays the same through a setup); GameNative's folder where its app is installed.
         */
        @JavascriptInterface
        fun setupSteps(): String {
            val asked = getSharedPreferences("home", MODE_PRIVATE).getBoolean(KEY_OFFERED, false) || setUpByItself()
            return JSONArray(listOfNotNull("folders", "dictionaries".takeIf { !asked }, "anki".takeIf { !asked }, "gamenative".takeIf { hasGameNative() })).toString()
        }

        @JavascriptInterface
        fun hasGameNative(): Boolean = this@HomeActivity.hasGameNative()

        /** Whether a controller is there (the device's own or one plugged in): the page shows its button hints then. */
        @JavascriptInterface
        fun hasController(): Boolean = android.view.InputDevice.getDeviceIds().any { id ->
            val device = android.view.InputDevice.getDevice(id) ?: return@any false
            !device.isVirtual && device.sources and android.view.InputDevice.SOURCE_GAMEPAD == android.view.InputDevice.SOURCE_GAMEPAD
        }

        /** This build's version: the release it is (a published build), else the app's own. */
        @JavascriptInterface
        fun version(): String = Updater.mine.ifEmpty { BuildConfig.VERSION_NAME }

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
            scriptsBefore = scriptsStamp()
            startActivity(HomeScreen.apart(this@HomeActivity, Intent(this@HomeActivity, ScriptsActivity::class.java).putExtra(ScriptsActivity.EXTRA_CONSOLE, console)))
        }

        // ---- The controller mapping screen (map.js; the model is ButtonMap, in the reader's settings) ----

        /** The controller's keys handed to the page as they come, or as the page's names again. */
        /** "nintendo" where the device's own buttons carry Nintendo letters at Android's places (FaceButtons), else "". */
        @JavascriptInterface
        fun faceLetters(): String = if (FaceButtons.nintendo(this@HomeActivity)) "nintendo" else ""

        @JavascriptInterface
        fun listenRaw(on: Boolean) {
            rawKeys = on
            triggerDown.fill(false)
        }

        /** What each of [ids] (a JSON list of action ids) is on in [console]'s layer ("" for all consoles). */
        @JavascriptInterface
        fun buttonMap(console: String, ids: String): String {
            GameActivity.migratePadMap(this@HomeActivity)
            // What "Shortcut buttons" had is this screen's now (one place for a shortcut).
            ActionKeys.migrate(this@HomeActivity)
            FaceButtons.forgetHandSwap(this@HomeActivity)
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
                // The popup's settings on the second display: the popup as they make it, on the first.
                if (section == ReaderSettingsActivity.Section.POPUP && secondDisplay() != null) runOnUiThread {
                    runCatching {
                        startActivity(
                            Intent(this@HomeActivity, PopupPreviewActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                            android.app.ActivityOptions.makeBasic().setLaunchDisplayId(display?.displayId ?: 0).toBundle()
                        )
                    }
                }
                startOnLower(HomeScreen.apart(this@HomeActivity, ReaderSettingsActivity.intent(this@HomeActivity, section)))
                return
            }
            // "Search settings": the reader's settings with the search asked at once.
            if (screen == "find") {
                startOnLower(HomeScreen.apart(this@HomeActivity, ReaderSettingsActivity.findIntent(this@HomeActivity)))
                return
            }
            val activity = when (screen) {
                "settings" -> ReaderSettingsActivity::class.java
                "dictionaries" -> DictionaryManagerActivity::class.java
                "anki" -> AnkiSetupActivity::class.java
                "crt" -> CrtSettingsActivity::class.java
                "scripts" -> GameScriptsActivity::class.java
                "statistics" -> ReaderStatsActivity::class.java
                "licences" -> LicencesActivity::class.java
                else -> return
            }
            startOnLower(HomeScreen.apart(this@HomeActivity, Intent(this@HomeActivity, activity)))
        }

        /**
         * A page of settings: on a device with two displays, on the second one, where settings are
         * changed (the maintainer's choice of 2026-11-17, 4A: "settings live on the lower screen
         * only"); the first keeps the shelf. On one display, as before.
         */
        private fun startOnLower(intent: Intent) = runOnUiThread { startOnLowerNow(intent) }

        private fun startOnLowerNow(intent: Intent) {
            val lower = LowerActivity.live?.display?.displayId ?: this@HomeActivity.lower?.display?.displayId
                ?: getSystemService(android.hardware.display.DisplayManager::class.java)
                    .getDisplays(android.hardware.display.DisplayManager.DISPLAY_CATEGORY_PRESENTATION).firstOrNull()?.displayId
            if (lower == null || lower == display?.displayId) {
                startActivity(intent)
                return
            }
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_MULTIPLE_TASK)
            // The second display's own page is a window over every activity there: it steps
            // aside while the page of settings is open, and comes back when that page is left.
            val target = intent.component?.className
            onLower = target
            this@HomeActivity.lower?.let { page ->
                page.hide()
                // The page of settings may run in another of the app's processes: its task is
                // watched (the app's own tasks), and once it is gone the shelf comes back.
                val tasks = getSystemService(android.app.ActivityManager::class.java)
                val main = android.os.Handler(android.os.Looper.getMainLooper())
                var seen = false
                var waited = 0
                val watch = object : Runnable {
                    override fun run() {
                        waited++
                        val open = runCatching {
                            tasks.appTasks.any { it.taskInfo.baseActivity?.className == target || it.taskInfo.topActivity?.className == target }
                        }.getOrDefault(false)
                        if (open) seen = true
                        if (open || (!seen && waited < 20)) {
                            main.postDelayed(this, 500)
                            return
                        }
                        // Back to the shelf: its second display's page again (made anew if the shelf
                        // let it go meanwhile), and the shelf in front, so the controller works it.
                        if (onLower == target) onLower = null
                        // The page reads again what that screen may have changed (and its setup goes on).
                        callPage("if (window.Front) Front.reload()")
                        if (this@HomeActivity.lower === page) runCatching { page.show() }
                        else if (this@HomeActivity.lower == null) showLower()
                        runCatching { tasks.moveTaskToFront(taskId, 0) }
                    }
                }
                main.postDelayed(watch, 500)
            }
            runCatching { startActivity(intent, android.app.ActivityOptions.makeBasic().setLaunchDisplayId(lower).toBundle()) }
                .onFailure { onLower = null; this@HomeActivity.lower?.show(); startActivity(intent) }
        }
    }
}
