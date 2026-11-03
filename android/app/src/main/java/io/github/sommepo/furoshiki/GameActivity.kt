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
import io.github.sommepo.furoshiki.reader.ReaderGlue
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
        setContentView(frame)
        hideBars()
        surface.holder.addCallback(this)
        ReaderGlue.attach(this, surface)
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

    private fun showMenu() {
        if (inMenu || leaving) return
        inMenu = true
        applyPause()
        // Eden has no savestates.
        val states = Console.KEY != "switch"
        val items = listOfNotNull(
            getString(R.string.game_menu_resume) to {},
            getString(R.string.game_menu_reader) to { ReaderGlue.showMenu() },
            (getString(R.string.game_menu_save) to { GameNative.saveState(1); Unit }).takeIf { states },
            (getString(R.string.game_menu_load) to { GameNative.loadState(1); Unit }).takeIf { states },
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

    private fun sendPad() {
        GameNative.setPad(buttons or hat, sticks[0], sticks[1], sticks[2], sticks[3], triggers[0], triggers[1])
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            event.keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
        ) {
            return super.dispatchKeyEvent(event)
        }
        if (ReaderGlue.onKey(event)) return true
        val bit = PAD_KEYS[event.keyCode] ?: return super.dispatchKeyEvent(event)
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
