// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Process
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import io.github.sommepo.furoshiki.reader.ButtonMap
import io.github.sommepo.furoshiki.reader.Console
import io.github.sommepo.furoshiki.reader.NextChoice
import io.github.sommepo.furoshiki.reader.ReaderGlue
import io.github.sommepo.furoshiki.reader.Routes
import io.github.sommepo.furoshiki.reader.ReaderSession
import io.github.sommepo.furoshiki.reader.ReaderSettings
import java.io.File
import org.json.JSONArray
import kotlin.concurrent.thread
import kotlin.math.abs

/**
 * One game, played through one core, in this process alone: it ends with the game. The reader
 * lies over the picture as in the reader apps ([ReaderGlue]).
 */
class GameActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var surface: SurfaceView
    private var path = ""
    private var gameId = ""
    private var coreReady = false
    private var surfaceReady = false
    private var started = false
    private var resumedNow = false
    private var inMenu = false
    private var leaving = false

    // The controller as the game sees it.
    private var buttons = 0L
    private val sticks = IntArray(4)
    private val triggers = IntArray(2)

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        // On a device with two displays the game never turns with the sensor: the second display
        // cannot, and a game held like a book (Wish Room) turns its own picture (nds.rotation).
        if (getSystemService(DisplayManager::class.java)
                .getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).isNotEmpty()) {
            requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        }
        path = intent.getStringExtra(EXTRA_PATH).orEmpty()
        gameId = intent.getStringExtra(EXTRA_GAME).orEmpty()
        val key = intent.getStringExtra(EXTRA_CONSOLE).orEmpty()
        // A game on several discs (its list's entry, ScanService.oneEntryAGame): started on the disc
        // it was left on, else its first, the others given to it for the menu's "Change disc". A
        // PSP game always on its first: Grisaia starts from disc 1 only ("set disc 1" from disc 2),
        // and its core puts disc 2 in as the game asks for it (ppsspp/libretro/libretro.cpp).
        discPaths = runCatching { ScanService.discsOf(JSONArray(Places.gamesFile(this, key).readText()), path) }
            .getOrDefault(listOf(path)).filter { File(it).isFile }.ifEmpty { listOf(path) }
        if (discPaths.size > 1) {
            path = getSharedPreferences(DISC_PREFS, MODE_PRIVATE).getString(KEY_DISC + Discs.groupKey(path), null)
                ?.takeIf { it in discPaths && key != FIRST_DISC_ONLY } ?: discPaths[0]
            discPaths = listOf(path) + discPaths.filter { it != path }
        }
        HomeScreen.setPlaying(this, path, key, gameId)
        Console.use(key)
        // The controller's buttons as this console has them (the mapping screen's layer, ButtonMap).
        ReaderSettings.console = key
        migratePadMap(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        surface = SurfaceView(this)
        val frame = FrameLayout(this)
        frame.addView(surface, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        // The menu's button, faint in the top right corner: with the on-screen buttons, since a
        // device played by touch has no Select and Start to hold (the old apps' cog).
        val density = resources.displayMetrics.density
        val menuButton = android.widget.TextView(this).apply {
            text = "≡"
            textSize = 20f
            gravity = android.view.Gravity.CENTER
            setTextColor(android.graphics.Color.WHITE)
            alpha = .45f
            background = android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(0x66000000)
            }
            contentDescription = getString(R.string.game_menu_reader)
            visibility = android.view.View.GONE
            setOnClickListener { showMenu() }
        }
        val side = (38 * density).toInt()
        frame.addView(menuButton, FrameLayout.LayoutParams(side, side, android.view.Gravity.TOP or android.view.Gravity.END).apply {
            topMargin = (10 * density).toInt()
            marginEnd = (10 * density).toInt()
        })
        this.menuButton = menuButton
        // The guide's next step over the game (GuideCard), when it is asked for.
        guide = GuideCard(this, gameId).also { it.addTo(frame) }
        addDrawer(frame)
        setContentView(frame)
        hideBars()
        surface.holder.addCallback(this)
        actionKeys = ActionKeys.byKey(this)
        // The services this device's home server names (Explain's), where none is known yet.
        if (!Explain.offered(this)) thread(name = "Services") { runCatching { Services.ask(applicationContext) } }
        ReaderGlue.lowerKeys = { context -> lowerKeys(context) }
        ReaderGlue.lowerCorner = { context -> lowerCorner(context) }
        // A touch on the second display took the controller there (to Android's launcher under
        // the keys): the game's task to the front again, a moment after, so its display has them.
        ReaderGlue.refocus = {
            window.decorView.postDelayed({
                if (!leaving && !hasWindowFocus()) runCatching {
                    getSystemService(android.app.ActivityManager::class.java).moveTaskToFront(taskId, 0)
                }
            }, 250)
        }
        ReaderGlue.attach(this, surface)
        ReaderGlue.setAppControls { x, y ->
            (menuButton.visibility == android.view.View.VISIBLE &&
                x >= menuButton.left - 8 * density && x <= menuButton.right + 8 * density &&
                y >= menuButton.top - 8 * density && y <= menuButton.bottom + 8 * density) ||
                (guide?.shown == true && guide!!.view.let { x >= it.left && x <= it.right && y >= it.top && y <= it.bottom }) ||
                within(drawerHandle, x, y, 10 * density) || within(drawer, x, y, 0f)
        }
        ocr = io.github.sommepo.furoshiki.ocr.OcrSource(this, io.github.sommepo.furoshiki.ocr.SurfaceFrames(surface), gameId) { resumedNow && !inMenu && !leaving && started }
        ReaderGlue.stoppedByItself = { if (!leaving) fail(GameNative.lastError()?.removePrefix("core: ") ?: getString(R.string.game_ended)) }
        thread(name = "GameStart") {
            val error = prepare(key)
            runOnUiThread {
                if (error != null) {
                    fail(error)
                } else {
                    coreReady = true
                    startWhenReady()
                }
            }
        }
    }

    /** The core's files in place and the core started: off the main thread. */
    private fun prepare(key: String): String? {
        val file = Places.CORES[key] ?: return "No core for $key in this build"
        val system = Places.system(this, key)
        Places.BIOS_FOLDERS[key]?.let { folder ->
            if (Places.bios(this, key) == null) return getString(R.string.game_no_bios, folder)
        }
        // A core's own files. The Dreamcast has none: Flycast plays without a BIOS, on its stand-in.
        CoreAssets.forConsole(this, key)
        // This game's add-ons (Games/Addons): their scripts and settings, before the game's script is read.
        runCatching {
            val found = Addons.find(Places.gameFolders(this))
            if (found.isNotEmpty() && gameId.isNotEmpty()) {
                val scripts = File(getExternalFilesDir(null), io.github.sommepo.furoshiki.reader.ReaderNative.PROFILE_USER_DIR)
                val used = Addons(File(filesDir, "addons.json"), scripts, Places.playSettings(this)).apply(found, key, gameId)
                if (used.isNotEmpty()) Log.i("Furoshiki", "add-ons: " + used.joinToString())
            }
        }.onFailure { Log.w("Furoshiki", "add-ons: ${it.message}") }
        return GameNative.open(
            Places.corePath(this, file), Places.console(this, key, "data"), Places.console(this, key, "saves"),
            Places.console(this, key, "states"), system
        )
    }

    private fun startWhenReady() {
        if (started || !coreReady || !surfaceReady) return
        started = true
        sendSurface()
        val settings = Places.playSettings(this)
            .valuesFor(Console.KEY, gameId, Places.coreSettings(this, Console.KEY))
        for ((key, value) in settings) GameNative.applySetting(key, value)
        // What is text and so on no page: this game's codes and patches, the achievements' login.
        Places.playSettings(this).let { kept ->
            for (key in listOf("cheats.codes", "cheats.patches")) {
                kept.raw(key, Console.KEY, gameId).takeIf { it.isNotEmpty() }?.let { GameNative.applySetting(key, it) }
            }
            for (key in listOf("achievements.user", "achievements.token", "achievements.hardcore")) {
                kept.raw(key).takeIf { it.isNotEmpty() }?.let { GameNative.applySetting(key, it) }
            }
        }
        if (Console.TWO_SCREENS) chooseScreens(settings.map { it.first })
        GameNative.load(path)?.let { fail(it) } ?: run {
            // The game's other discs, for the menu's "Change disc".
            for (other in discPaths.drop(1)) GameNative.addDisc(other)
            ocr?.start()
        }
    }

    /** The game's discs: the one started first, then the others in their order (Discs). */
    private var discPaths: List<String> = emptyList()

    /** The disc in now, kept for the next time the game starts. */
    private fun keepDisc() {
        if (!started || discPaths.size < 2) return
        val now = GameNative.discCurrent()
        val disc = discPaths.getOrNull(now) ?: return
        // Written now, in a file of the game's process alone: the process is ended as the game is
        // quit (an apply() never reached the disk), and the home page's process keeps "home" in
        // memory and writes it back whole.
        getSharedPreferences(DISC_PREFS, MODE_PRIVATE).edit().putString(KEY_DISC + Discs.groupKey(disc), disc).commit()
    }

    /**
     * A 3DS: one over the other, which suits a display held upright and two displays (each then
     * shows its part of that picture), and a large upper screen on one wide display. The
     * picture is sharper than the console's own: these displays have some five times its pixels.
     */
    private fun chooseScreens(set: List<String>) {
        val twoDisplays = ReaderSettings(this).secondScreen && getSystemService(DisplayManager::class.java)
            .getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).isNotEmpty()
        val wide = surface.width > surface.height
        // Two displays need the screens one over the other; else the user's choice stands.
        if (twoDisplays || "3ds.layout" !in set) {
            GameNative.applySetting("3ds.layout", if (wide && !twoDisplays) "large_screen" else "top_bottom")
        }
        if ("picture.resolution" !in set) GameNative.applySetting("picture.resolution", "3")
    }

    private fun sendSurface() {
        val holder = surface.holder
        GameNative.setSurface(holder.surface, surface.width, surface.height, resources.displayMetrics.density)
    }

    private fun fail(message: String) {
        Log.e("Furoshiki", message)
        Toast.makeText(this, getString(R.string.game_failed, message), Toast.LENGTH_LONG).show()
        leave()
    }

    override fun surfaceCreated(holder: SurfaceHolder) = Unit

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        surfaceReady = true
        if (coreReady) sendSurface()
        startWhenReady()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        if (coreReady) GameNative.setSurface(null, 0, 0, 1f)
    }

    override fun onResume() {
        super.onResume()
        resumedNow = true
        AudioFocus.take(this, game = true)
        hideBars()
        ReaderGlue.onResume()
        // The buttons read again: the page may have changed them while the game stood behind it.
        nintendoFace = FaceButtons.nintendo(this)
        FaceButtons.forgetHandSwap(this)
        keySettings = ReaderSettings(this)
        loadPad()
        steam.start()
        if (started) ReaderSession.onSettingsChanged()
        ocr?.reload()
        // The menu's button with the on-screen buttons, as they are set now.
        menuButton?.visibility = if (ReaderGlue.wantsTouchButtons()) android.view.View.VISIBLE else android.view.View.GONE
        applyPause()
    }

    override fun onPause() {
        resumedNow = false
        AudioFocus.give(this)
        ReaderGlue.onPause()
        applyPause()
        steamInput.releaseAll()
        steam.stop()
        keepDisc()
        // Cards made in the game go to AnkiDroid, which syncs only when opened: asked to now, as
        // the game is left (never while it plays: AnkiDroid's request would pause it a moment).
        io.github.sommepo.furoshiki.reader.anki.AnkiDroidClient.requestSyncIfUnsynced(this)
        super.onPause()
    }

    /** The game stands still behind a menu, while the app is not in front, and while it is held for reading. */
    private fun applyPause() {
        if (coreReady) GameNative.pause(!resumedNow || inMenu || held)
        // Behind the game's menu the core no longer says it is in its game: the second display
        // stays all the same (the maintainer, 2026-11-17: the menu "should not disappear the bottom screen").
        ReaderGlue.held = held || inMenu
    }

    /** Held still by the second display's key, with the reader going on: for looking words up at leisure. */
    private var held = false

    /** The Steam Controller, read by Furoshiki itself (steam/): its right pad is the pointer here. */
    private val steamInput by lazy { io.github.sommepo.furoshiki.steam.SteamInput(this, inGame = true) { runAction(it) } }
    private val steam by lazy {
        io.github.sommepo.furoshiki.steam.SteamUsb(this, steamInput) {
            // One swipe across the pad crosses some two thirds of the game's screen.
            io.github.sommepo.furoshiki.steam.SteamPad.Tuning(
                swipePixels = maxOf(600.0, io.github.sommepo.furoshiki.reader.cursor.HoverCursor.width * 0.66)
            )
        }
    }

    /**
     * The keys along the foot of the second display (LowerKeys): what a computer's controller has
     * on its back buttons. Null when they are switched off (the game's menu).
     */
    private fun lowerKeys(context: android.content.Context): android.view.View? {
        if (!LowerKeys.wanted(this)) return null
        return LowerKeys.view(context, keyList())
    }

    /**
     * On a console of two screens (the DS, the 3DS) the second display is the game's lower screen,
     * so the keys can't take a strip of their own there: the reader's icon sits in its upper right
     * corner and brings them down over the screen's top; HIDE puts them away (the maintainer,
     * 2026-11-17: "i want the reader icon on the bottom screen bottom corner"). Taps elsewhere are
     * the game's.
     */
    private fun lowerCorner(context: android.content.Context): android.view.View {
        val density = context.resources.displayMetrics.density
        val layer = FrameLayout(context)
        val icon = android.widget.ImageView(context).apply {
            setImageResource(R.drawable.ic_reader_open_book)
            imageTintList = android.content.res.ColorStateList.valueOf(android.graphics.Color.WHITE)
            val pad = (9 * density).toInt()
            setPadding(pad, pad, pad, pad)
            alpha = .6f
            background = android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(0x88000000.toInt())
            }
            contentDescription = getString(R.string.drawer_show)
        }
        lateinit var keys: android.view.View
        var lineIconShown: android.view.View? = null
        // Shown and put away at once: on the second display a closing slide was left standing (seen).
        fun show(open: Boolean) {
            keys.visibility = if (open) android.view.View.VISIBLE else android.view.View.GONE
            icon.visibility = if (open) android.view.View.GONE else android.view.View.VISIBLE
            lineIconShown?.visibility = icon.visibility
            layer.invalidate()
        }
        keys = LowerKeys.view(context, keyList() + LowerKeys.Key(getString(R.string.drawer_hide)) { show(false) })
        keys.visibility = android.view.View.GONE
        icon.setOnClickListener { show(true) }
        // Along the top, where they leave the reader's line at the foot in sight (the maintainer: "the
        // line is hidden by the bar"); the icon in the top corner, in the band above the picture.
        layer.addView(keys, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, android.view.Gravity.TOP))
        val side = (36 * density).toInt()
        layer.addView(icon, FrameLayout.LayoutParams(side, side, android.view.Gravity.TOP or android.view.Gravity.END).apply {
            val margin = (4 * density).toInt()
            setMargins(margin, margin, margin, margin)
        })
        // The line's own icon, floating in the other top corner, apart from the keys' (the maintainer,
        // 2026-11-17: on a game of two screens "a separate icon in the upper corner ... which when
        // tapped reveals the LINE"): the reader's line shown over the lower screen, or put away.
        val settings = ReaderSettings(this)
        val lineIcon = android.widget.TextView(context).apply {
            text = "文"
            textSize = 15f
            gravity = android.view.Gravity.CENTER
            setTextColor(android.graphics.Color.WHITE)
            contentDescription = getString(R.string.lower_key_line)
        }
        fun paintLine() {
            val on = !settings.panelAutoHide
            lineIcon.alpha = if (on) .95f else .6f
            lineIcon.background = android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(if (on) 0xFFD9472B.toInt() else 0x88000000.toInt())
            }
        }
        paintLine()
        lineIcon.setOnClickListener {
            settings.preferences.edit().putBoolean(ReaderSettings.KEY_PANEL_AUTO_HIDE, !settings.panelAutoHide).apply()
            paintLine()
        }
        lineIconShown = lineIcon
        layer.addView(lineIcon, FrameLayout.LayoutParams(side, side, android.view.Gravity.TOP or android.view.Gravity.START).apply {
            val margin = (4 * density).toInt()
            setMargins(margin, margin, margin, margin)
        })
        return layer
    }

    /** The keys, as the second display has them along its foot and one display in its drawer. */
    private fun keyList(): List<LowerKeys.Key> {
        val settings = ReaderSettings(this)
        return (
            listOfNotNull(
                LowerKeys.Key(getString(R.string.lower_key_pause), { held }) { act("pause") },
                LowerKeys.Key(getString(R.string.lower_key_stats), { settings.statsPaused }) { act("stats") },
                LowerKeys.Key(getString(R.string.lower_key_log)) { act("log") },
                // The line shown always (on), or only where the game's own text can't be pointed at:
                // the second display then the trackpad alone.
                LowerKeys.Key(getString(R.string.lower_key_line), { !settings.panelAutoHide }) {
                    settings.preferences.edit().putBoolean(ReaderSettings.KEY_PANEL_AUTO_HIDE, !settings.panelAutoHide).apply()
                },
                LowerKeys.Key(getString(R.string.lower_key_explain)) { act("explain") }.takeIf { Explain.offered(this) },
                LowerKeys.Key(getString(R.string.lower_key_read)) { act("ocr_now") }.takeIf { ocr?.active == true },
                LowerKeys.Key(getString(R.string.lower_key_guide), { guide?.shown == true }) { act("guide") }.takeIf { guide?.offered() == true },
                LowerKeys.Key(getString(R.string.lower_key_menu)) { act("menu") },
            )
        )
    }

    // ---- One display (a Fold, a tablet): the same keys in a drawer along the foot of the game ----

    private var drawerHandle: android.view.View? = null
    private var drawer: android.view.View? = null

    /**
     * Where there is no second display the keys lie over the game's foot, out of sight until the
     * small handle at the middle of the foot is tapped; HIDE puts them away again. Not built
     * where the keys are switched off (the game's menu).
     */
    private fun addDrawer(frame: FrameLayout) {
        val second = getSystemService(DisplayManager::class.java).getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).isNotEmpty()
        if (second || !LowerKeys.wanted(this)) return
        val density = resources.displayMetrics.density
        val handle = android.widget.TextView(this).apply {
            text = "⋯"
            textSize = 18f
            gravity = android.view.Gravity.CENTER
            setTextColor(android.graphics.Color.WHITE)
            alpha = .4f
            contentDescription = getString(R.string.drawer_show)
            background = android.graphics.drawable.GradientDrawable().apply {
                cornerRadius = 12 * density
                setColor(0x66000000)
            }
        }
        val keys = LowerKeys.view(this, keyList() + LowerKeys.Key(getString(R.string.drawer_hide)) { showDrawer(false) })
        keys.visibility = android.view.View.GONE
        handle.setOnClickListener { showDrawer(true) }
        frame.addView(keys, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, android.view.Gravity.BOTTOM))
        frame.addView(handle, FrameLayout.LayoutParams((64 * density).toInt(), (26 * density).toInt(), android.view.Gravity.BOTTOM or android.view.Gravity.CENTER_HORIZONTAL).apply {
            bottomMargin = (6 * density).toInt()
        })
        drawer = keys
        drawerHandle = handle
    }

    private fun showDrawer(open: Boolean) {
        val keys = drawer ?: return
        val handle = drawerHandle ?: return
        val away = keys.height.takeIf { it > 0 }?.toFloat() ?: (80 * resources.displayMetrics.density)
        if (open) {
            keys.visibility = android.view.View.VISIBLE
            keys.translationY = away
            keys.animate().translationY(0f).setDuration(180).start()
            handle.visibility = android.view.View.GONE
        } else {
            keys.animate().translationY(away).setDuration(160).withEndAction { keys.visibility = android.view.View.GONE }.start()
            handle.visibility = android.view.View.VISIBLE
        }
    }

    private fun within(view: android.view.View?, x: Float, y: Float, slop: Float): Boolean =
        view != null && view.visibility == android.view.View.VISIBLE &&
            x >= view.left - slop && x <= view.right + slop && y >= view.top - slop && y <= view.bottom + slop

    /** The controller's buttons given to an action ([ActionKeys]): each key and what it does. */
    private var actionKeys: Map<Int, String> = emptyMap()

    /**
     * One of the things done over a game without its menu ([ActionKeys.ALL]): from a key on the
     * second display, or from the button of the controller given to it.
     */
    private fun act(action: String) {
        if (leaving) return
        when (action) {
            "pause" -> {
                held = !held
                applyPause()
            }
            "stats" -> ReaderSession.toggleStatsPaused()
            "card" -> ReaderSession.dispatch(ReaderSession.Command.SENTENCE_CARD)
            "log" -> ReaderSession.dispatch(ReaderSession.Command.BACKLOG)
            "panel" -> ReaderSession.dispatch(ReaderSession.Command.TOGGLE_PANEL)
            "explain" -> explain()
            "ocr_now" -> ocr?.readNow()
            "next_choice" -> if (Console.HAS_NEXT_CHOICE) NextChoice.toggle()
            "fast" -> setFast(!fast)
            "guide" -> if (guide?.offered() == true) guide?.toggle()
            "guide_done" -> guide?.done()
            "quick_menu" -> showReaderMenu()
            "menu" -> showMenu()
        }
    }

    private fun setFast(on: Boolean) {
        fast = on
        GameNative.applySetting("emulation.speed", if (fast) "0" else "100")
    }

    /** Explain, of the line being read: the game stands still while its answer is read. */
    private fun explain() {
        if (inMenu) return
        val line = ReaderSession.current?.line?.text?.trim().orEmpty()
        if (line.isEmpty()) {
            Toast.makeText(this, R.string.explain_no_line, Toast.LENGTH_SHORT).show()
            return
        }
        if (!Explain.offered(this)) {
            Toast.makeText(this, R.string.explain_no_service, Toast.LENGTH_LONG).show()
            return
        }
        val before = runCatching {
            ReaderSession.database(this).recentLines(gameId, 4).map { it.line.text }.filter { it.trim() != line }.reversed()
        }.getOrDefault(emptyList())
        inMenu = true
        applyPause()
        Explain.show(this, line, before, ReaderSession.gameTitle, gameId) {
            inMenu = false
            applyPause()
        }
    }

    private fun hideBars() {
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideBars()
            AudioFocus.take(this, game = true, again = true)
        }
    }

    // ---- The menu over the game: back, or the controller's Select and Start held together ----

    private var menuButton: android.view.View? = null
    private var guide: GuideCard? = null

    /**
     * *More settings* (the reader's own menu), with the game held still while it is open as the
     * game's menu holds it (the maintainer, 2026-11-17: the game's menu paused the game, and this,
     * opened from it, let it run on).
     */
    private fun showReaderMenu() {
        inMenu = true
        applyPause()
        ReaderGlue.showMenu {
            inMenu = false
            applyPause()
        }
    }

    private fun showMenu() {
        if (inMenu || leaving) return
        inMenu = true
        applyPause()
        // Eden has no savestates.
        val states = Console.KEY != "switch"
        // Not here by the maintainer's word (2026-11-17, from the Thor): cheat codes, changing the
        // disc, the OCR text area and hiding the second display's keys.
        val items = listOfNotNull(
            getString(R.string.game_menu_resume) to {},
            getString(R.string.game_menu_reader) to { showReaderMenu() },
            (getString(R.string.game_menu_explain) to { explain() }).takeIf { Explain.offered(this) },
            (getString(R.string.game_menu_ocr_now) to { ocr?.readNow() }).takeIf { ocr?.active == true },
            (getString(R.string.game_menu_save) to { pickState(save = true) }).takeIf { states },
            (getString(R.string.game_menu_load) to { pickState(save = false) }).takeIf { states },
            getString(if (fast) R.string.game_menu_fast_off else R.string.game_menu_fast_on) to { setFast(!fast) },
            (getString(if (NextChoice.going) R.string.game_menu_next_choice_stop else R.string.game_menu_next_choice) to {
                NextChoice.toggle()
            }).takeIf { Console.HAS_NEXT_CHOICE },
            (getString(R.string.game_menu_routes) to { Routes.show(this) }).takeIf { Routes.offer() != null },
            (getString(R.string.game_menu_patches) to { pickPatches() }).takeIf { GameNative.features() and 32 != 0 },
            (getString(if (guide?.shown == true) R.string.game_menu_guide_hide else R.string.game_menu_guide) to {
                guide?.toggle()
            }).takeIf { guide?.offered() == true },
            getString(R.string.action_keys_title) to {
                inMenu = true
                ActionKeys.pick(this) {
                    actionKeys = ActionKeys.byKey(this)
                    inMenu = false
                    applyPause()
                }
            },
            getString(R.string.game_menu_home) to { leave() },
        )
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setItems(items.map { it.first }.toTypedArray()) { _, which -> chosen = { items[which].second() } }
            .setOnDismissListener {
                inMenu = false
                // What was chosen, once the menu is gone: a box it opens holds the game still itself.
                val then = chosen
                chosen = null
                then?.invoke()
                applyPause()
            }
            .show()
    }

    /** The game read by OCR, where no script reads it or the user said so (ocr/OcrSource). */
    private var ocr: io.github.sommepo.furoshiki.ocr.OcrSource? = null

    /**
     * The game's text areas set up over its picture, held still (ocr/OcrSetup): from the menu,
     * so a controller reaches it as a finger does.
     */
    private fun setUpOcr() {
        val source = ocr ?: return
        if (!started || leaving) return
        inMenu = true
        applyPause()
        // The picture as it stands, whole, and where the game's own picture is in it.
        val rect = runCatching {
            if (Console.TWO_SCREENS) {
                GameNative.screenRects()?.let { android.graphics.Rect(it[0], it[1], it[0] + it[2], it[1] + it[3]) }
            } else {
                io.github.sommepo.furoshiki.reader.ReaderNative.pictureRect(surface.width, surface.height)
                    ?.takeIf { it.size >= 6 && it[4] > it[2] && it[5] > it[3] }
                    ?.let { android.graphics.Rect(it[2], it[3], it[4], it[5]) }
            }
        }.getOrNull() ?: android.graphics.Rect(0, 0, surface.width, surface.height)
        io.github.sommepo.furoshiki.reader.ui.ReaderHost.copyFrame(surface, surface.width) { bitmap ->
            if (bitmap == null || leaving) {
                inMenu = false
                applyPause()
                return@copyFrame
            }
            io.github.sommepo.furoshiki.ocr.OcrSetup(
                this, bitmap, rect, source.game,
                read = { areas -> source.read(bitmap, rect, areas) },
                onSave = { source.setGame(it) },
                onClosed = {
                    inMenu = false
                    applyPause()
                    hideBars()
                },
            ).show()
        }
    }

    /** The menu's row that was tapped, done as the menu shuts. */
    private var chosen: (() -> Unit)? = null

    /** As fast as it goes, until switched back (the menu's row): through what was read before. */
    private var fast = false

    /** A savestate's slot chosen: each with when it was written, where the core tells. */
    private fun pickState(save: Boolean) {
        val slots = GameNative.stateSlots().coerceIn(1, 10)
        if (slots == 1) {
            if (save) GameNative.saveState(1) else GameNative.loadState(1)
            return
        }
        val clock = java.text.SimpleDateFormat("MM/dd HH:mm", java.util.Locale.US)
        val labels = (1..slots).map { slot ->
            val saved = GameNative.stateSaved(slot)
            getString(R.string.game_state_slot, slot) + when {
                saved > 1 -> "  ·  " + clock.format(java.util.Date(saved))
                saved == 0L -> "  ·  " + getString(R.string.game_state_empty)
                else -> ""
            }
        }
        inMenu = true
        applyPause()
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setTitle(if (save) R.string.game_menu_save else R.string.game_menu_load)
            .setItems(labels.toTypedArray()) { _, which ->
                if (save) GameNative.saveState(which + 1) else GameNative.loadState(which + 1)
            }
            .setOnDismissListener {
                inMenu = false
                applyPause()
            }
            .show()
    }

    /** The core's patches for this game (widescreen, no interlacing), each switched; kept for the game. */
    private fun pickPatches() {
        val list = runCatching { org.json.JSONArray(String(GameNative.patches(gameId), Charsets.UTF_8)) }.getOrDefault(org.json.JSONArray())
        if (list.length() == 0) {
            android.widget.Toast.makeText(this, R.string.game_patches_none, android.widget.Toast.LENGTH_SHORT).show()
            return
        }
        val kept = Places.playSettings(this)
        val set = kept.raw("cheats.patches", Console.KEY, gameId)
        val names = List(list.length()) { list.getJSONObject(it).getString("name") }
        // Never set for this game: as the core has them by itself.
        val on = BooleanArray(names.size) { at ->
            if (set.isEmpty()) list.getJSONObject(at).optBoolean("on") else names[at] in set.lines()
        }
        inMenu = true
        applyPause()
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setTitle(R.string.game_menu_patches)
            .setMultiChoiceItems(names.toTypedArray(), on) { _, which, checked -> on[which] = checked }
            .setPositiveButton(android.R.string.ok) { _, _ ->
                // "none" keeps an empty choice apart from never having chosen.
                val chosen = names.filterIndexed { at, _ -> on[at] }.joinToString("\n").ifEmpty { "none" }
                kept.set("cheats.patches", chosen, Console.KEY, gameId)
                GameNative.applySetting("cheats.patches", chosen)
                android.widget.Toast.makeText(this, R.string.game_patches_next, android.widget.Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setOnDismissListener {
                inMenu = false
                applyPause()
            }
            .show()
    }

    /** This game's cheat codes, one to a line, as the console's players write them. */
    private fun typeCodes() {
        val kept = Places.playSettings(this)
        val field = android.widget.EditText(this).apply {
            setText(kept.raw("cheats.codes", Console.KEY, gameId))
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            minLines = 5
            typeface = android.graphics.Typeface.MONOSPACE
            hint = getString(R.string.game_codes_hint)
        }
        inMenu = true
        applyPause()
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setTitle(R.string.game_menu_codes)
            .setView(field)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val codes = field.text.toString().trim()
                kept.set("cheats.codes", codes, Console.KEY, gameId)
                GameNative.applySetting("cheats.codes", codes)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setOnDismissListener {
                inMenu = false
                applyPause()
            }
            .show()
    }

    /** Another disc of the game put in, by its label. */
    private fun pickDisc(count: Int) {
        val now = GameNative.discCurrent()
        val labels = (0 until count).map { index ->
            val label = String(GameNative.discLabel(index), Charsets.UTF_8).ifEmpty { getString(R.string.game_disc, index + 1) }
            if (index == now) "● $label" else label
        }
        inMenu = true
        applyPause()
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setTitle(R.string.game_menu_disc)
            .setItems(labels.toTypedArray()) { _, which -> if (which != now) GameNative.insertDisc(which) }
            .setOnDismissListener {
                inMenu = false
                applyPause()
            }
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() = showMenu()

    // A tap of two fingers anywhere over the game: the menu (the user, 2026-11-14: on the Y700 in
    // its controller the edge's back swipe is hard to reach). As the second finger comes, what
    // the first began is cancelled, and nothing of the gesture reaches the game.
    private var twoFingers = false
    private var twoFingersAt = 0L
    private var twoFingersMoved = false
    private val twoFingersStart = HashMap<Int, android.graphics.PointF>()

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                twoFingers = false
                twoFingersStart.clear()
                twoFingersStart[event.getPointerId(0)] = android.graphics.PointF(event.x, event.y)
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                val index = event.actionIndex
                twoFingersStart[event.getPointerId(index)] = android.graphics.PointF(event.getX(index), event.getY(index))
                if (!twoFingers && event.pointerCount == 2) {
                    twoFingers = true
                    twoFingersMoved = false
                    twoFingersAt = event.eventTime
                    val cancel = MotionEvent.obtain(event).apply { action = MotionEvent.ACTION_CANCEL }
                    super.dispatchTouchEvent(cancel)
                    cancel.recycle()
                    return true
                }
                if (event.pointerCount > 2) twoFingersMoved = true
            }
        }
        if (!twoFingers) return super.dispatchTouchEvent(event)
        val slop = 24 * resources.displayMetrics.density
        for (i in 0 until event.pointerCount) {
            val start = twoFingersStart[event.getPointerId(i)] ?: continue
            if (abs(event.getX(i) - start.x) > slop || abs(event.getY(i) - start.y) > slop) twoFingersMoved = true
        }
        if (event.actionMasked == MotionEvent.ACTION_UP) {
            twoFingers = false
            if (!twoFingersMoved && event.eventTime - twoFingersAt < 500) showMenu()
        } else if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
            twoFingers = false
        }
        return true
    }

    /**
     * Another game chosen on the shelf while this one is in play (Furoshiki as the home screen,
     * where the page is a task of its own): this one ends, and the page starts the other in a
     * new process.
     */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        val other = intent.getStringExtra(EXTRA_PATH).orEmpty()
        if (other.isEmpty() || other == path) return
        HomeScreen.setNext(this, other, intent.getStringExtra(EXTRA_CONSOLE).orEmpty(), intent.getStringExtra(EXTRA_GAME).orEmpty())
        leave()
    }

    /** The session saved, then this process gone: a core is never unloaded. */
    private fun leave() {
        if (leaving) return
        leaving = true
        keepDisc() // while the core still says which disc is in: it is stopped below, before onPause
        guide?.hide()
        ocr?.stop()
        var gone = false
        // The game ends here whatever its core does: one that never comes back from stopping (a
        // hung emulator thread) left the game "in play" and its process running behind the shelf.
        val go = Runnable {
            if (gone) return@Runnable
            gone = true
            HomeScreen.setPlaying(this, null)
            if (HomeScreen.chosen(this)) {
                // The page is the home screen, in a task of its own: this one goes whole.
                startActivity(HomeScreen.home())
                finishAndRemoveTask()
            } else {
                startActivity(Intent(this, HomeActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
                finish()
            }
            Process.killProcess(Process.myPid())
        }
        window.decorView.postDelayed(go, LEAVE_AT_MOST_MS)
        thread(name = "GameEnd") {
            if (started) GameNative.stop()
            runOnUiThread { ReaderSession.stop() }
            ReaderSession.awaitStopped(3000)
            runOnUiThread(go)
        }
    }

    /** The task closed from the system's list: the game goes with it, not left running unseen. */
    override fun onDestroy() {
        super.onDestroy()
        if (!leaving && isFinishing) {
            HomeScreen.setPlaying(this, null)
            if (started) GameNative.stop()
            ReaderSession.stop()
            ReaderSession.awaitStopped(1000)
            Process.killProcess(Process.myPid())
        }
    }

    // ---- The controller ----

    // The game's own buttons (the controller mapping screen's "<Console>'s buttons", ButtonMap's
    // pad_button_<id>): which button of the controller presses each, for this console.

    /** A key of the controller and the places (libretro's, [ButtonMap.PAD_PLACES]) it presses. */
    private var padPlaces: Map<Int, List<Int>> = emptyMap()

    /** The keys that press the console's L2 and R2, for the triggers as the core hears how far they are pulled. */
    private val triggerKeys = intArrayOf(KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2)

    /** The controller's buttons held now: by their key events, and by its axes (the d-pad as a hat, the triggers). */
    private val heldKeys = HashSet<Int>()
    private val heldAxes = HashSet<Int>()
    private val pulled = IntArray(2)

    /** Read as the game comes to the front: the page may have changed them behind it. */
    private fun loadPad() {
        val values = ReaderSettings.fresh(this).all
        padPlaces = ButtonMap.padKeys(values, Console.KEY).mapValues { (_, ids) -> ids.map { ButtonMap.PAD_PLACES.indexOf(it) } }
        triggerKeys[0] = ButtonMap.PAD_INPUTS[ButtonMap.padSource(values, "l2", Console.KEY)] ?: -1
        triggerKeys[1] = ButtonMap.PAD_INPUTS[ButtonMap.padSource(values, "r2", Console.KEY)] ?: -1
        pressPad()
    }

    /** The console's buttons as the held buttons of the controller press them. */
    private fun pressPad() {
        var bits = 0L
        for (held in arrayOf(heldKeys, heldAxes)) {
            for (key in held) padPlaces[key]?.forEach { bits = bits or (1L shl Console.padBit(it)) }
        }
        buttons = bits
        // How far L2 and R2 are pulled: the trigger's own pull where a trigger presses it, else all or nothing.
        for (side in 0..1) {
            triggers[side] = when (val key = triggerKeys[side]) {
                KeyEvent.KEYCODE_BUTTON_L2 -> pulled[0]
                KeyEvent.KEYCODE_BUTTON_R2 -> pulled[1]
                else -> if (key in heldKeys || key in heldAxes) 255 else 0
            }
        }
    }

    /** A button the reader or a shortcut has: an axis of it is not the game's either. */
    private fun isShortcut(code: Int): Boolean {
        val settings = keySettings ?: ReaderSettings(this).also { keySettings = it }
        return code == settings.cursorButton || code == settings.statsButton || code == settings.nextChoiceButton ||
            code == settings.menuButton || settings.buttonAction(code) != null
    }

    // ---- What a button was given on the controller mapping screen (ButtonMap) ----

    /** The settings the buttons are read from: made again as the game comes back to the front. */
    private var keySettings: ReaderSettings? = null

    /** Buttons whose press ran an action: their repeats and release are kept from the game too. */
    private val mappedKeys = HashSet<Int>()

    /**
     * A button with an action of its own (the console's layer first): the action runs as it is
     * pressed, and the game hears nothing of it. The pointer's, the statistics' and the next
     * choice's buttons are the reader's (ReaderInput), before this.
     */
    private fun buttonAction(event: KeyEvent): Boolean {
        val code = event.keyCode
        if (event.action == KeyEvent.ACTION_UP) return mappedKeys.remove(code)
        if (event.action != KeyEvent.ACTION_DOWN) return false
        if (code in mappedKeys) return true
        val settings = keySettings ?: ReaderSettings(this).also { keySettings = it }
        val action = if (settings.menuButton == code) "reader_menu" else settings.buttonAction(code) ?: return false
        if (!runAction(action)) return false
        mappedKeys.add(code)
        return true
    }

    /** What each action of shared/controllers/controllers.json does here; false for one this app has not. */
    private fun runAction(action: String): Boolean {
        when (action) {
            "reader_menu" -> showReaderMenu()
            "reader_pause_game" -> {
                held = !held
                applyPause()
            }
            "reader_pause_stats" -> ReaderSession.toggleStatsPaused()
            "reader_sentence_card" -> ReaderSession.dispatch(ReaderSession.Command.SENTENCE_CARD)
            "reader_word_card" -> ReaderSession.dispatch(ReaderSession.Command.WORD_CARD)
            "reader_backlog" -> ReaderSession.dispatch(ReaderSession.Command.BACKLOG)
            "reader_close_popup" -> ReaderSession.dispatch(ReaderSession.Command.CLOSE_POPUP)
            "reader_next_choice" -> NextChoice.toggle()
            "furoshiki_game_menu" -> showMenu()
            "furoshiki_guide" -> if (guide?.offered() == true) guide?.toggle()
            "furoshiki_guide_done" -> if (guide?.offered() == true) guide?.done()
            else -> return false
        }
        return true
    }

    private fun sendPad() {
        GameNative.setPad(buttons, sticks[0], sticks[1], sticks[2], sticks[3], triggers[0], triggers[1])
    }

    /** Whether the device's own buttons come by their Nintendo letters (FaceButtons), read as the game comes to the front. */
    private var nintendoFace = false

    override fun dispatchKeyEvent(raw: KeyEvent): Boolean {
        // The face buttons by place, whatever the device's letters (FaceButtons).
        val event = FaceButtons.byPlace(raw, nintendoFace)
        // An open popup is modal: its buttons (and Back, which closes it) before anything else.
        if (io.github.sommepo.furoshiki.reader.ui.PopupKeys.isOpen && ReaderGlue.onKey(event)) return true
        if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            event.keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
        ) {
            return super.dispatchKeyEvent(event)
        }
        // A button given to an action is the action's, not the game's.
        actionKeys[event.keyCode]?.let { action ->
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) act(action)
            return true
        }
        if (ReaderGlue.onKey(event)) return true
        // A button given a shortcut does the shortcut, and the game hears nothing of it.
        if (buttonAction(event)) return true
        val code = event.keyCode
        if (code !in ButtonMap.PAD_INPUTS.values) return super.dispatchKeyEvent(event)
        val down = event.action == KeyEvent.ACTION_DOWN
        if (down) heldKeys.add(code) else if (event.action == KeyEvent.ACTION_UP) heldKeys.remove(code)
        // These two are of the controller in the hand, whatever its buttons press in the game.
        // The guide's next step: Select and L1 show or hide it, Select and R1 tick it.
        val shoulder = when (code) {
            KeyEvent.KEYCODE_BUTTON_L1 -> 1
            KeyEvent.KEYCODE_BUTTON_R1 -> 2
            else -> 0
        }
        if (shoulder != 0 && down && KeyEvent.KEYCODE_BUTTON_SELECT in heldKeys && guide?.offered() == true) {
            heldKeys.remove(KeyEvent.KEYCODE_BUTTON_SELECT)
            heldKeys.remove(code)
            pressPad()
            sendPad()
            if (shoulder == 1) guide?.toggle() else guide?.done()
            return true
        }
        // Select and Start held together: the menu over the game.
        if (KeyEvent.KEYCODE_BUTTON_SELECT in heldKeys && KeyEvent.KEYCODE_BUTTON_START in heldKeys) {
            heldKeys.clear()
            pressPad()
            sendPad()
            showMenu()
            return true
        }
        pressPad()
        sendPad()
        return true
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0) return super.dispatchGenericMotionEvent(event)
        if (ReaderGlue.onMotion(event)) return true
        fun axis(id: Int): Int {
            val value = event.getAxisValue(id)
            return if (abs(value) < DEAD_ZONE) 0 else (value * 32767).toInt()
        }
        sticks[0] = axis(MotionEvent.AXIS_X)
        sticks[1] = axis(MotionEvent.AXIS_Y)
        sticks[2] = axis(MotionEvent.AXIS_Z)
        sticks[3] = axis(MotionEvent.AXIS_RZ)
        pulled[0] = (maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)) * 255).toInt()
        pulled[1] = (maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)) * 255).toInt()
        // The d-pad as a hat and the triggers as axes are buttons of the controller like the
        // rest: they press what the map gives them. A trigger moved elsewhere counts as held
        // from half way (one on its own place is the core's by its pull alone, as it was).
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        heldAxes.clear()
        if (hatX < -0.5f) heldAxes.add(KeyEvent.KEYCODE_DPAD_LEFT)
        if (hatX > 0.5f) heldAxes.add(KeyEvent.KEYCODE_DPAD_RIGHT)
        if (hatY < -0.5f) heldAxes.add(KeyEvent.KEYCODE_DPAD_UP)
        if (hatY > 0.5f) heldAxes.add(KeyEvent.KEYCODE_DPAD_DOWN)
        val sides = intArrayOf(KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2)
        for (side in 0..1) {
            val code = sides[side]
            val own = padPlaces[code]?.singleOrNull() == ButtonMap.PAD_PLACES.indexOf(if (side == 0) "l2" else "r2")
            if (pulled[side] > 127 && !own && !isShortcut(code)) heldAxes.add(code)
        }
        pressPad()
        sendPad()
        return true
    }

    companion object {
        /** How long the game may take to stop before it is ended anyway. */
        private const val LEAVE_AT_MOST_MS = 4000L

        const val EXTRA_PATH = "path"
        const val EXTRA_CONSOLE = "console"
        const val EXTRA_GAME = "game"
        private const val DEAD_ZONE = 0.12f

        private const val KEY_PAD_MAP = "pad_map"
        private const val KEY_DISC = "disc:"
        private const val DISC_PREFS = "discs"
        /** The console whose games always start on their first disc (above). */
        private const val FIRST_DISC_ONLY = "psp"

        /**
         * The old "Buttons" dialog of the game's menu kept one map for every console ("home",
         * `pad_map`: a place to the key given to it). The controller mapping screen has the
         * game's buttons now: what the dialog was given becomes all consoles' buttons there,
         * once, and the old map is gone. Called by the page too, before it shows the buttons.
         */
        fun migratePadMap(context: android.content.Context) {
            val kept = context.getSharedPreferences("home", android.content.Context.MODE_PRIVATE)
            val text = kept.getString(KEY_PAD_MAP, null) ?: return
            runCatching {
                val given = org.json.JSONObject(text)
                val places = HashMap<Int, Int>()
                for (place in given.keys()) place.toIntOrNull()?.let { places[it] = given.getInt(place) }
                ButtonMap.migratePlaces(ReaderSettings.fresh(context), places)
            }
            kept.edit().remove(KEY_PAD_MAP).commit()
        }
    }
}
