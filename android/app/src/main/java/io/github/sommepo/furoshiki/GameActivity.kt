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
import io.github.sommepo.furoshiki.reader.Console
import io.github.sommepo.furoshiki.reader.NextChoice
import io.github.sommepo.furoshiki.reader.ReaderGlue
import io.github.sommepo.furoshiki.reader.Routes
import io.github.sommepo.furoshiki.reader.ReaderSession
import io.github.sommepo.furoshiki.reader.ReaderSettings
import java.io.File
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
    private var hat = 0L
    private val sticks = IntArray(4)
    private val triggers = IntArray(2)

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        path = intent.getStringExtra(EXTRA_PATH).orEmpty()
        gameId = intent.getStringExtra(EXTRA_GAME).orEmpty()
        val key = intent.getStringExtra(EXTRA_CONSOLE).orEmpty()
        Console.use(key)
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
        // The left stick for a thumb, with the on-screen buttons: a ring at the lower left,
        // beside the d-pad's column; where the thumb is from its middle is how far it is pushed.
        val stick = object : android.view.View(this) {
            private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
            private var dx = 0f
            private var dy = 0f

            override fun onDraw(canvas: android.graphics.Canvas) {
                val radius = width / 2f
                paint.style = android.graphics.Paint.Style.FILL
                paint.color = 0x33000000
                canvas.drawCircle(radius, radius, radius - 2 * density, paint)
                paint.style = android.graphics.Paint.Style.STROKE
                paint.strokeWidth = 1.5f * density
                paint.color = 0x99FFFFFF.toInt()
                canvas.drawCircle(radius, radius, radius - 2 * density, paint)
                paint.style = android.graphics.Paint.Style.FILL
                paint.color = 0x88FFFFFF.toInt()
                canvas.drawCircle(radius + dx * radius * .55f, radius + dy * radius * .55f, radius * .36f, paint)
            }

            @android.annotation.SuppressLint("ClickableViewAccessibility")
            override fun onTouchEvent(event: MotionEvent): Boolean {
                val radius = width / 2f
                if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
                    dx = 0f
                    dy = 0f
                } else {
                    dx = ((event.x - radius) / (radius * .7f)).coerceIn(-1f, 1f)
                    dy = ((event.y - radius) / (radius * .7f)).coerceIn(-1f, 1f)
                }
                sticks[0] = (dx * 32767).toInt()
                sticks[1] = (dy * 32767).toInt()
                sendPad()
                invalidate()
                return true
            }
        }
        stick.alpha = .55f
        stick.visibility = android.view.View.GONE
        val across = (104 * density).toInt()
        frame.addView(stick, FrameLayout.LayoutParams(across, across, android.view.Gravity.BOTTOM or android.view.Gravity.START).apply {
            leftMargin = (150 * density).toInt()
            bottomMargin = (18 * density).toInt()
        })
        this.stickView = stick
        setContentView(frame)
        hideBars()
        surface.holder.addCallback(this)
        ReaderGlue.attach(this, surface)
        ReaderGlue.setAppControls { x, y ->
            (menuButton.visibility == android.view.View.VISIBLE &&
                x >= menuButton.left - 8 * density && x <= menuButton.right + 8 * density &&
                y >= menuButton.top - 8 * density && y <= menuButton.bottom + 8 * density) ||
                (stick.visibility == android.view.View.VISIBLE &&
                    x >= stick.left && x <= stick.right && y >= stick.top && y <= stick.bottom)
        }
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
        GameNative.load(path)?.let { fail(it) }
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
        hideBars()
        ReaderGlue.onResume()
        // The menu's button with the on-screen buttons, as they are set now.
        menuButton?.visibility = if (ReaderGlue.wantsTouchButtons()) android.view.View.VISIBLE else android.view.View.GONE
        // The stick where the console has one (a PlayStation's first pad and a Dreamcast VN need none, and have it all the same).
        stickView?.visibility = if (ReaderGlue.wantsTouchButtons()) android.view.View.VISIBLE else android.view.View.GONE
        applyPause()
    }

    override fun onPause() {
        resumedNow = false
        ReaderGlue.onPause()
        applyPause()
        super.onPause()
    }

    /** The game stands still behind a menu and while the app is not in front. */
    private fun applyPause() {
        if (coreReady) GameNative.pause(!resumedNow || inMenu)
    }

    private fun hideBars() {
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideBars()
    }

    // ---- The menu over the game: back, or the controller's Select and Start held together ----

    private var menuButton: android.view.View? = null
    private var stickView: android.view.View? = null

    private fun showMenu() {
        if (inMenu || leaving) return
        inMenu = true
        applyPause()
        // Eden has no savestates.
        val states = Console.KEY != "switch"
        val discs = if (GameNative.features() and 2 != 0) GameNative.discCount() else 0
        val items = listOfNotNull(
            getString(R.string.game_menu_resume) to {},
            getString(R.string.game_menu_reader) to { ReaderGlue.showMenu() },
            (getString(R.string.game_menu_save) to { pickState(save = true) }).takeIf { states },
            (getString(R.string.game_menu_load) to { pickState(save = false) }).takeIf { states },
            (getString(R.string.game_menu_disc) to { pickDisc(discs) }).takeIf { discs > 1 },
            getString(if (fast) R.string.game_menu_fast_off else R.string.game_menu_fast_on) to {
                fast = !fast
                GameNative.applySetting("emulation.speed", if (fast) "0" else "100")
            },
            (getString(if (NextChoice.going) R.string.game_menu_next_choice_stop else R.string.game_menu_next_choice) to {
                NextChoice.toggle()
            }).takeIf { Console.HAS_NEXT_CHOICE },
            (getString(R.string.game_menu_routes) to { Routes.show(this) }).takeIf { Routes.offer() != null },
            (getString(R.string.game_menu_patches) to { pickPatches() }).takeIf { GameNative.features() and 32 != 0 },
            (getString(R.string.game_menu_codes) to { typeCodes() }).takeIf { GameNative.features() and 64 != 0 },
            getString(R.string.game_menu_buttons) to { pickButtons() },
            getString(R.string.game_menu_home) to { leave() },
        )
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setItems(items.map { it.first }.toTypedArray()) { _, which -> items[which].second() }
            .setOnDismissListener {
                inMenu = false
                applyPause()
            }
            .show()
    }

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

    /** The session saved, then this process gone: a core is never unloaded. */
    private fun leave() {
        if (leaving) return
        leaving = true
        thread(name = "GameEnd") {
            if (started) GameNative.stop()
            runOnUiThread { ReaderSession.stop() }
            ReaderSession.awaitStopped(3000)
            runOnUiThread {
                startActivity(Intent(this, HomeActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
                finish()
                Process.killProcess(Process.myPid())
            }
        }
    }

    /** The task closed from the system's list: the game goes with it, not left running unseen. */
    override fun onDestroy() {
        super.onDestroy()
        if (!leaving && isFinishing) {
            if (started) GameNative.stop()
            ReaderSession.stop()
            ReaderSession.awaitStopped(1000)
            Process.killProcess(Process.myPid())
        }
    }

    // ---- The controller ----

    /** The controller's keys and the place each presses, as it comes or as the user gave them. */
    private val padKeys: MutableMap<Int, Int> by lazy { loadPadKeys() }

    /** While a place waits for its button: what the next key pressed is handed to. */
    private var remapping: ((Int) -> Unit)? = null

    private fun loadPadKeys(): MutableMap<Int, Int> {
        val keys = HashMap(PAD_KEYS)
        runCatching {
            val given = org.json.JSONObject(getSharedPreferences("home", MODE_PRIVATE).getString(KEY_PAD_MAP, "{}") ?: "{}")
            for (place in given.keys()) {
                val key = given.getInt(place)
                // A key given a place leaves the place it had; the place leaves the key it had.
                keys.remove(key)
                keys.entries.removeAll { it.value == place.toInt() }
                keys[key] = place.toInt()
            }
        }
        return keys
    }

    /** The buttons, each with the key that presses it: a tap, then the key to give it. */
    private fun pickButtons() {
        val places = PLACES.indices.toList()
        val labels = places.map { place ->
            val key = padKeys.entries.firstOrNull { it.value == place }?.key
            PLACES[place] + "  ·  " + (key?.let { KeyEvent.keyCodeToString(it).removePrefix("KEYCODE_").removePrefix("BUTTON_") } ?: "—")
        } + getString(R.string.game_buttons_reset)
        inMenu = true
        applyPause()
        AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
            .setTitle(R.string.game_menu_buttons)
            .setItems(labels.toTypedArray()) { _, which ->
                val kept = getSharedPreferences("home", MODE_PRIVATE)
                if (which >= places.size) {
                    kept.edit().remove(KEY_PAD_MAP).apply()
                    padKeys.clear()
                    padKeys.putAll(PAD_KEYS)
                    return@setItems
                }
                val waiting = AlertDialog.Builder(android.view.ContextThemeWrapper(this, R.style.Theme_Reader))
                    .setTitle(PLACES[which])
                    .setMessage(R.string.game_buttons_press)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setOnDismissListener {
                        remapping = null
                        inMenu = false
                        applyPause()
                    }
                    .create()
                inMenu = true
                remapping = { key ->
                    val given = runCatching { org.json.JSONObject(kept.getString(KEY_PAD_MAP, "{}") ?: "{}") }.getOrDefault(org.json.JSONObject())
                    // One place a key: what this key had before is no longer its.
                    for (place in given.keys().asSequence().toList()) if (given.optInt(place, -1) == key) given.remove(place)
                    given.put(which.toString(), key)
                    kept.edit().putString(KEY_PAD_MAP, given.toString()).apply()
                    padKeys.clear()
                    padKeys.putAll(loadPadKeys())
                    waiting.dismiss()
                }
                // The dialog's own window takes the keys: they are handed on from there.
                waiting.setOnKeyListener { _, code, event ->
                    if (code == KeyEvent.KEYCODE_BACK) return@setOnKeyListener false
                    if (event.action == KeyEvent.ACTION_DOWN) remapping?.invoke(code)
                    true
                }
                waiting.show()
            }
            .setOnDismissListener {
                if (remapping == null) {
                    inMenu = false
                    applyPause()
                }
            }
            .show()
    }

    private fun sendPad() {
        GameNative.setPad(buttons or hat, sticks[0], sticks[1], sticks[2], sticks[3], triggers[0], triggers[1])
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            event.keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
        ) {
            return super.dispatchKeyEvent(event)
        }
        // A button being given another place (the menu's Buttons): this press is its answer.
        remapping?.let { asked ->
            if (event.action == KeyEvent.ACTION_DOWN) asked(event.keyCode)
            return true
        }
        if (ReaderGlue.onKey(event)) return true
        val bit = padKeys[event.keyCode] ?: return super.dispatchKeyEvent(event)
        val mask = 1L shl Console.padBit(bit)
        buttons = if (event.action == KeyEvent.ACTION_DOWN) buttons or mask else buttons and mask.inv()
        val together = (1L shl Console.padBit(RETRO_SELECT)) or (1L shl Console.padBit(RETRO_START))
        if (buttons and together == together) {
            buttons = 0
            sendPad()
            showMenu()
            return true
        }
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
        triggers[0] = (maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)) * 255).toInt()
        triggers[1] = (maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)) * 255).toInt()
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        hat = (if (hatX < -0.5f) 1L shl Console.padBit(RETRO_LEFT) else 0L) or
            (if (hatX > 0.5f) 1L shl Console.padBit(RETRO_RIGHT) else 0L) or
            (if (hatY < -0.5f) 1L shl Console.padBit(RETRO_UP) else 0L) or
            (if (hatY > 0.5f) 1L shl Console.padBit(RETRO_DOWN) else 0L)
        sendPad()
        return true
    }

    companion object {
        const val EXTRA_PATH = "path"
        const val EXTRA_CONSOLE = "console"
        const val EXTRA_GAME = "game"
        private const val DEAD_ZONE = 0.12f

        // A core's buttons are libretro's joypad bits, by where they lie on a controller.
        private const val RETRO_SELECT = 2
        private const val RETRO_START = 3
        private const val RETRO_UP = 4
        private const val RETRO_DOWN = 5
        private const val RETRO_LEFT = 6
        private const val RETRO_RIGHT = 7
        private const val KEY_PAD_MAP = "pad_map"

        /** The places of a controller's buttons, as libretro numbers them. */
        private val PLACES = listOf(
            "South (×, B)", "West (□, Y)", "Select", "Start", "Up", "Down", "Left", "Right",
            "East (○, A)", "North (△, X)", "L1", "R1", "L2", "R2", "L3", "R3",
        )
        private val PAD_KEYS = mapOf(
            KeyEvent.KEYCODE_BUTTON_A to 0, KeyEvent.KEYCODE_BUTTON_X to 1,
            KeyEvent.KEYCODE_BUTTON_SELECT to RETRO_SELECT, KeyEvent.KEYCODE_BUTTON_START to RETRO_START,
            KeyEvent.KEYCODE_DPAD_UP to RETRO_UP, KeyEvent.KEYCODE_DPAD_DOWN to RETRO_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT to RETRO_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT to RETRO_RIGHT,
            KeyEvent.KEYCODE_BUTTON_B to 8, KeyEvent.KEYCODE_BUTTON_Y to 9,
            KeyEvent.KEYCODE_BUTTON_L1 to 10, KeyEvent.KEYCODE_BUTTON_R1 to 11,
            KeyEvent.KEYCODE_BUTTON_L2 to 12, KeyEvent.KEYCODE_BUTTON_R2 to 13,
            KeyEvent.KEYCODE_BUTTON_THUMBL to 14, KeyEvent.KEYCODE_BUTTON_THUMBR to 15,
        )
    }
}
