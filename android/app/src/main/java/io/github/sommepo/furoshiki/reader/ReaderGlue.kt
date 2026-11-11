// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.reader

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import io.github.sommepo.furoshiki.GameNative
import io.github.sommepo.furoshiki.reader.cursor.HoverCursor
import io.github.sommepo.furoshiki.reader.screen.Area
import io.github.sommepo.furoshiki.reader.screen.ScreenGeometry
import io.github.sommepo.furoshiki.reader.ui.ReaderHost
import io.github.sommepo.furoshiki.reader.ui.ReaderOverlay
import io.github.sommepo.furoshiki.reader.ui.ScreenTextLayer
import io.github.sommepo.furoshiki.reader.ui.SecondScreen

/**
 * What ties the reading layer to GameActivity, as each reader app's file of this name ties it
 * to its emulator's activity. The surface shows the game alone, so the layer is up whenever
 * the game runs and is not paused.
 */
object ReaderGlue : ReaderNative.Frontend {
    private val main = Handler(Looper.getMainLooper())

    private var surface: SurfaceView? = null
    private var host: ReaderHost? = null
    private var overlay: ReaderOverlay? = null
    private var second: SecondScreen? = null
    private var activity: Activity? = null
    private var resumed = false
    private var inFront = false

    /** The game ended, or never started, by itself. GameActivity goes back to the page. */
    var stoppedByItself: (() -> Unit)? = null

    private const val WATCH_MS = 200L

    private val watch = object : Runnable {
        override fun run() {
            update()
            GameNative.sendCrt()
            main.postDelayed(this, WATCH_MS)
        }
    }

    fun attach(activity: Activity, surface: SurfaceView) {
        this.activity = activity
        this.surface = surface
        ReaderNative.appContext = activity.applicationContext
        ReaderNative.frontend = this
        GameNative // its library, before the first of ReaderNative's calls
        ReaderNative.attachOnce()
        HoverCursor.holdSticks = { held -> ReaderNative.holdSticks(held) }
        val secondScreen = SecondScreen(activity) { overlay?.refresh() }
        val readerHost = ReaderHost(activity, secondaryActive = { secondScreen.active })
        secondScreen.host = readerHost
        if (Console.TWO_SCREENS) {
            secondScreen.under = { context -> lowerScreen(context) }
            ScreenTextLayer.geometryOf = { secondary, width, height -> twoScreens(secondary, width, height) }
            surface.setOnTouchListener { _, event -> second?.active != true && fingerOnLower(event) }
        } else if (Console.KEY == "psvita" || Console.KEY == "switch") {
            surface.setOnTouchListener { view, event -> fingerOnPicture(view, event) }
        }
        val layer = ReaderOverlay(activity, readerHost, secondary = false)
        layer.gameSurface = surface
        layer.visibility = View.GONE
        activity.addContentView(
            layer,
            ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        )
        host = readerHost
        second = secondScreen
        overlay = layer
        main.post(watch)
    }

    /** Where the app's own button over the game is (the menu's): the reader's layer leaves taps there to it. */
    fun setAppControls(at: (Float, Float) -> Boolean) {
        overlay?.appControlsAt = at
    }

    /** Whether the on-screen buttons are wanted, as the reader's settings have it now. */
    fun wantsTouchButtons(): Boolean = activity?.let { ReaderSettings(it).touchButtons } ?: false

    fun onResume() {
        resumed = true
        update()
    }

    fun onPause() {
        resumed = false
        update()
    }

    fun onKey(event: KeyEvent): Boolean = ReaderInput.onKey(event)

    fun onMotion(event: MotionEvent): Boolean = ReaderInput.onMotion(event)

    fun showMenu() {
        overlay?.let { host?.showMenu(it) }
    }

    private fun update() {
        val front = resumed && ReaderSession.isActive && ReaderNative.inGame()
        if (front == inFront) return
        inFront = front
        overlay?.visibility = if (front) View.VISIBLE else View.GONE
        // Back from the settings: the on-screen buttons as they are set now.
        activity?.let { overlay?.touchButtonsAlways = ReaderSettings(it).touchButtons }
        ReaderSession.setReading(front)
        if (front) {
            second?.start()
        } else {
            HoverCursor.cancel()
            second?.stop()
        }
    }

    // ---- A console with two screens ----

    /** The lower screen on the second display, the game's touch screen as on one display. */
    private fun lowerScreen(context: Context): View {
        val view = SurfaceView(context)
        view.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) = Unit

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                GameNative.setSecondSurface(holder.surface, width, height)
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                GameNative.setSecondSurface(null, 0, 0)
            }
        })
        view.setOnTouchListener { _, event -> fingerOnLower(event) }
        return view
    }

    /** What a display shows: both screens on one, or the lower one on a display of its own. */
    private fun twoScreens(secondary: Boolean, width: Int, height: Int): ScreenGeometry? {
        val rects = try {
            GameNative.screenRects()
        } catch (_: UnsatisfiedLinkError) {
            null
        } ?: return null
        fun area(at: Int): List<Area> = listOfNotNull(
            Area(
                rects[at].toFloat(), rects[at + 1].toFloat(),
                (rects[at] + rects[at + 2]).toFloat(), (rects[at + 1] + rects[at + 3]).toFloat()
            ).takeUnless { it.isEmpty }
        )
        val upper = area(0)
        val lower = area(4)
        return when {
            secondary -> ScreenGeometry(width, height, game = emptyList(), pad = lower, lower = lower)
            second?.active == true -> ScreenGeometry(width, height, game = upper, pad = emptyList())
            else -> ScreenGeometry(width, height, game = upper, pad = lower, lower = lower)
        }
    }

    /** A finger on the lower screen is the game's: true when this one is, from its landing on. */
    private fun fingerOnLower(event: MotionEvent): Boolean {
        val rects = GameNative.screenRects() ?: return false
        return finger(event, 1, rects[4], rects[5], rects[6], rects[7])
    }

    /** A console with one screen that is its touch screen. */
    private fun fingerOnPicture(view: View, event: MotionEvent): Boolean {
        val rect = ReaderNative.pictureRect(view.width, view.height) ?: return false
        return finger(event, 0, rect[2], rect[3], rect[4] - rect[2], rect[5] - rect[3])
    }

    private fun finger(event: MotionEvent, screen: Int, left: Int, top: Int, width: Int, height: Int): Boolean {
        if (width <= 0 || height <= 0) return false
        val x = (event.x - left) / width
        val y = (event.y - top) / height
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (x !in 0f..1f || y !in 0f..1f) return false
                GameNative.pointer(screen, x, y, true)
            }
            MotionEvent.ACTION_MOVE -> GameNative.pointer(screen, x.coerceIn(0f, 1f), y.coerceIn(0f, 1f), true)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL ->
                GameNative.pointer(screen, x.coerceIn(0f, 1f), y.coerceIn(0f, 1f), false)
        }
        return true
    }

    // ---- ReaderNative.Frontend (main thread) ----

    override fun onGameStarted(gameId: String, title: String) {
        val context = activity ?: return
        ReaderSession.start(context, gameId, title)
        surface?.let { ReaderHost.provideFrames(it) }
        // A touch device has no other buttons: every game shows them, read or not.
        overlay?.touchButtonsAlways = ReaderSettings(context).touchButtons
        inFront = false
        update()
    }

    override fun onGameStopped() {
        ReaderSession.stop()
        update()
        stoppedByItself?.invoke()
    }

    override fun onMenu(what: String) = Unit
}
