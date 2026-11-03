// SPDX-FileCopyrightText: Station Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader

import android.app.Activity
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import com.github.stenzek.duckstation.reader.cursor.HoverCursor
import com.github.stenzek.duckstation.reader.ui.ReaderHost
import com.github.stenzek.duckstation.reader.ui.ReaderOverlay
import com.github.stenzek.duckstation.reader.ui.AnkiSetupActivity
import com.github.stenzek.duckstation.reader.ui.DictionaryManagerActivity
import com.github.stenzek.duckstation.reader.ui.ReaderGamesActivity
import com.github.stenzek.duckstation.reader.ui.ReaderSettingsActivity
import com.github.stenzek.duckstation.reader.ui.ReaderStatsActivity
import com.github.stenzek.duckstation.reader.ui.SecondScreen

/**
 * What ties the reading layer to the emulator's activity of a running game (EmulationActivity,
 * Java), which calls in here at the few places marked "Reader fork"; everything else of the
 * reader is its own. The app's game list (MainActivity) opens the reader's pages through
 * [openPage].
 *
 * The reader's layer lies over the game's surface (and the touchscreen controller): it is
 * shown, and takes touches, only while the game runs, not while the emulator's pause menu has
 * stopped it.
 */
object ReaderGlue : ReaderNative.Frontend {
    private val main = Handler(Looper.getMainLooper())

    private var activity: Activity? = null
    private var surface: SurfaceView? = null
    private var host: ReaderHost? = null
    private var overlay: ReaderOverlay? = null
    private var second: SecondScreen? = null
    private var resumed = false
    private var inFront = false

    /** How often the emulator is asked whether the game is in front. */
    private const val WATCH_MS = 200L

    private val watch = object : Runnable {
        override fun run() {
            update()
            main.postDelayed(this, WATCH_MS)
        }
    }

    /** Told whether the game is being played by touch (the reader's buttons stand in for the pad). */
    fun interface TouchPlaying {
        fun changed(playing: Boolean)
    }

    private var touchPlayingListener: TouchPlaying? = null

    /** Whether the running game is being played by touch now. */
    @JvmStatic
    var touchPlaying = false
        private set

    @JvmStatic
    fun setTouchPlayingListener(listener: TouchPlaying?) {
        touchPlayingListener = listener
    }

    /**
     * DuckStation's touchscreen controller at first ("digital", or "none"): on for a phone or
     * tablet, off on a handheld with controls of its own (an AYN Thor), as the reader's Play by
     * touch is (the other reader apps' on-screen pads are the same).
     */
    @JvmStatic
    fun touchControllerAtFirst(): String = if (ReaderSettings.touchPlayAtFirst()) "digital" else "none"

    /**
     * Keeps the first values of what this app sets by the device (the touchscreen controller,
     * Play by touch) as the user's own, so that the settings pages show what is in force.
     */
    @JvmStatic
    fun keepFirstSettings(context: android.content.Context) {
        val preferences = androidx.preference.PreferenceManager.getDefaultSharedPreferences(context)
        if (!preferences.contains(TOUCH_CONTROLLER_KEY)) {
            preferences.edit().putString(TOUCH_CONTROLLER_KEY, touchControllerAtFirst()).apply()
        }
        ReaderSettings(context).keepTouchPlayDefault()
    }

    private const val TOUCH_CONTROLLER_KEY = "Controller1/TouchscreenControllerView"

    /** The activity has made its surface. */
    @JvmStatic
    fun attach(activity: Activity, surface: View?) {
        detach()
        this.activity = activity
        this.surface = surface as? SurfaceView
        ReaderNative.appContext = activity.applicationContext
        ReaderNative.frontend = this
        ReaderNative.attachOnce()
        HoverCursor.holdSticks = { held ->
            try {
                ReaderNative.holdSticks(held)
            } catch (_: UnsatisfiedLinkError) {
            }
        }

        val secondScreen = SecondScreen(activity) { overlay?.refresh() }
        val readerHost = ReaderHost(activity, secondaryActive = { secondScreen.active })
        secondScreen.host = readerHost
        val layer = ReaderOverlay(activity, readerHost, secondary = false)
        layer.gameSurface = this.surface
        layer.visibility = View.GONE
        // While a game is played by touch the reader's small buttons stand in for DuckStation's
        // touchscreen controller (EmulationActivity hides its pad).
        layer.onTouchPlaying = { playing ->
            touchPlaying = playing
            touchPlayingListener?.changed(playing)
        }
        // Just above the game's surface, under DuckStation's touchscreen controller (which the
        // activity adds after this): the tube's frame does not hide its buttons, and a touch
        // beside them comes on to the reader's layer (TouchscreenControllerView passes it on).
        val params = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        val parent = surface?.parent as? ViewGroup
        if (parent != null) {
            parent.addView(layer, parent.indexOfChild(surface) + 1, params)
        } else {
            activity.addContentView(layer, params)
        }
        host = readerHost
        second = secondScreen
        overlay = layer
        provideFrames()
        main.removeCallbacks(watch)
        main.post(watch)
    }

    /**
     * The reader's first questions, once: the app has no setup screen of its own. Asked from
     * the game list, never over a game that is starting: DuckStation's activity gives the game
     * up when another screen covers it before its surface is made.
     */
    @JvmStatic
    fun offerStart(activity: Activity) {
        com.github.stenzek.duckstation.reader.ui.ReaderStartActivity.offerOnce(activity)
    }

    @JvmStatic
    fun onResume() {
        resumed = true
        update()
    }

    @JvmStatic
    fun onPause() {
        resumed = false
        update()
    }

    @JvmStatic
    fun onDestroy(activity: Activity) {
        if (this.activity === activity) detach()
    }

    /** A key of a controller or keyboard. True when the emulator must not get it. */
    @JvmStatic
    fun onKey(event: KeyEvent): Boolean = ReaderInput.onKey(event)

    /** A movement of a stick or trigger. True when the emulator must not get it. */
    @JvmStatic
    fun onMotion(event: MotionEvent): Boolean = ReaderInput.onMotion(event)

    private fun detach() {
        main.removeCallbacks(watch)
        touchPlaying = false
        second?.stop()
        second = null
        ReaderSession.media?.frameSource = null
        (overlay?.parent as? ViewGroup)?.removeView(overlay)
        overlay = null
        host = null
        surface = null
        activity = null
        inFront = false
    }

    private fun provideFrames() {
        surface?.let { ReaderHost.provideFrames(it) }
    }

    /** Shows the reader's layer while the game is in front and hides it otherwise. */
    private fun update() {
        val front = resumed && ReaderSession.isActive && try {
            ReaderNative.inGame()
        } catch (_: UnsatisfiedLinkError) {
            false
        }
        if (front == inFront) return
        inFront = front
        overlay?.visibility = if (front) View.VISIBLE else View.GONE
        // Nobody is reading a game that is paused or behind a menu.
        ReaderSession.setReading(front)
        if (front) {
            second?.start()
        } else {
            HoverCursor.cancel()
            second?.stop()
        }
    }

    // ---- ReaderNative.Frontend (main thread) ----

    override fun onGameStarted(gameId: String, title: String) {
        val context = activity ?: return
        ReaderSession.start(context, gameId, title)
        provideFrames()
        inFront = false
        update()
    }

    override fun onGameStopped() {
        ReaderSession.stop()
        update()
    }

    /**
     * One of the emulator's own menus asks for the reader: "menu" (what the reader offers for
     * the running game: its menu over the game, or its settings when no game is read) or one
     * of its pages ("settings", "stats", "games", "dictionaries", "anki").
     */
    @JvmStatic
    fun openPage(context: Activity, what: String) {
        if (context !== activity) {
            pageIntent(context, what)?.let { context.startActivity(it) }
            return
        }
        // From the pause menu, which has just let the game go on: the reader's menu is anchored
        // to its layer, which is shown at the next look whether the game is in front.
        main.postDelayed({ update(); onMenu(what) }, WATCH_MS * 2)
    }

    private fun pageIntent(context: Activity, what: String): Intent? = when (what) {
        "stats" -> Intent(context, ReaderStatsActivity::class.java)
        "games" -> Intent(context, ReaderGamesActivity::class.java)
        "dictionaries" -> Intent(context, DictionaryManagerActivity::class.java)
        "anki" -> Intent(context, AnkiSetupActivity::class.java)
        "settings", "menu" -> Intent(context, ReaderSettingsActivity::class.java)
        else -> null
    }

    override fun onMenu(what: String) {
        val context = activity ?: return
        when (what) {
            "settings" -> context.startActivity(Intent(context, ReaderSettingsActivity::class.java))
            "stats" -> context.startActivity(Intent(context, ReaderStatsActivity::class.java))
            "games" -> context.startActivity(Intent(context, ReaderGamesActivity::class.java))
            "dictionaries" -> context.startActivity(Intent(context, DictionaryManagerActivity::class.java))
            "anki" -> context.startActivity(Intent(context, AnkiSetupActivity::class.java))
            else -> {
                val anchor = overlay ?: return
                if (ReaderSession.isActive) {
                    host?.showMenu(anchor)
                } else {
                    context.startActivity(Intent(context, ReaderSettingsActivity::class.java))
                }
            }
        }
    }
}
