// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.os.Bundle
import android.widget.LinearLayout
import org.readers.app.R
import org.readers.app.reader.ReaderSession

/** The backlog as a screen of its own ([BacklogView]). */
class ReaderBacklogActivity : ReaderScreen() {
    override val titleRes = R.string.reader_backlog

    private lateinit var backlog: BacklogView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val game = intent.getStringExtra(EXTRA_GAME) ?: ReaderSession.gameKey
        backlog = BacklogView(this, game, textScale = 1.25f) {
            startActivity(android.content.Intent(this, DictionaryManagerActivity::class.java))
        }
        content.addView(backlog, LinearLayout.LayoutParams(MATCH, 0, 1f))
        // Whose lines these are, under the title.
        runCatching { ReaderSession.database(this).gameTitle(game) }.getOrNull()?.takeIf { it.isNotEmpty() }?.let { toolbar.subtitle = it }
        current = this
    }

    override fun onDestroy() {
        if (current === this) current = null
        main.removeCallbacks(edge)
        backlog.release()
        super.onDestroy()
    }

    // ---- Without a finger on it: the d-pad and the stick scroll, and so does the pointer at an edge ----

    private val main = android.os.Handler(android.os.Looper.getMainLooper())
    private var edgeStep = 0
    private val edge = object : Runnable {
        override fun run() {
            if (edgeStep == 0) return
            backlog.scrollLines(edgeStep, smooth = false)
            main.postDelayed(this, 16)
        }
    }

    override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean {
        val step = when (event.keyCode) {
            android.view.KeyEvent.KEYCODE_DPAD_DOWN -> 1
            android.view.KeyEvent.KEYCODE_DPAD_UP -> -1
            android.view.KeyEvent.KEYCODE_BUTTON_R1, android.view.KeyEvent.KEYCODE_PAGE_DOWN -> 3
            android.view.KeyEvent.KEYCODE_BUTTON_L1, android.view.KeyEvent.KEYCODE_PAGE_UP -> -3
            else -> 0
        }
        // A popup open over the lines has the d-pad (PopupKeys); the search field its own keys.
        if (step != 0 && !backlog.lookups.isOpen && currentFocus !is android.widget.EditText) {
            if (event.action == android.view.KeyEvent.ACTION_DOWN) backlog.scrollLines(step * backlog.height * 3 / 10)
            return true
        }
        if (event.keyCode == android.view.KeyEvent.KEYCODE_BUTTON_B && !backlog.lookups.isOpen) {
            if (event.action == android.view.KeyEvent.ACTION_UP) finish()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean {
        if (event.isFromSource(android.view.InputDevice.SOURCE_JOYSTICK) && event.action == android.view.MotionEvent.ACTION_MOVE) {
            // The left stick scrolls, faster the further it is pushed.
            val y = event.getAxisValue(android.view.MotionEvent.AXIS_Y)
            edgeStep = if (kotlin.math.abs(y) > 0.25f && !backlog.lookups.isOpen) (y * kotlin.math.abs(y) * 34).toInt() else 0
            main.removeCallbacks(edge)
            if (edgeStep != 0) main.post(edge)
            return true
        }
        if (event.isFromSource(android.view.InputDevice.SOURCE_CLASS_POINTER) &&
            (event.action == android.view.MotionEvent.ACTION_HOVER_MOVE || event.action == android.view.MotionEvent.ACTION_HOVER_EXIT)) {
            // The pointer held near the top or the foot of the lines scrolls them that way.
            val place = IntArray(2).also { backlog.getLocationOnScreen(it) }
            val y = event.rawY - place[1]
            val band = backlog.height * 0.1f
            edgeStep = when {
                event.action == android.view.MotionEvent.ACTION_HOVER_EXIT || backlog.lookups.isOpen -> 0
                y < band -> -((band - y) / band * 26).toInt() - 2
                y > backlog.height - band -> ((y - (backlog.height - band)) / band * 26).toInt() + 2
                else -> 0
            }
            main.removeCallbacks(edge)
            if (edgeStep != 0) main.post(edge)
        }
        return super.dispatchGenericMotionEvent(event)
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (backlog.lookups.isOpen) backlog.lookups.close() else super.onBackPressed()
    }

    companion object {
        const val EXTRA_GAME = "game"

        /** The backlog screen that is open, for the key that opens it, moves it and closes it. */
        var current: ReaderBacklogActivity? = null
            private set
    }
}
