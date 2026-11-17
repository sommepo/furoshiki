// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import java.lang.ref.WeakReference
import kotlin.math.abs

/**
 * The controller while a popup is open (the maintainer, 2026-11-17): the popup is modal, so the
 * game gets no buttons meanwhile; the left stick scrolls it; north (△) makes a card of the word
 * without audio, east (○) one with the line's audio; L1 and R1 go to the tab before and the
 * next (words, grammar, kanji, names); south (✕) and Back close it. The cursor's own button
 * stays the cursor's ([ReaderInput] asks before it gets here).
 *
 * Whether a controller is the input ([padInUse]: a controller's button or stick was the last
 * thing used, not a finger) decides whether the popup marks those buttons on itself.
 */
object PopupKeys {
    private val layers = ArrayList<WeakReference<LookupLayer>>()
    private val main = Handler(Looper.getMainLooper())

    /** Every popup there is (the game's display and the second one): the open one answers. */
    fun add(layer: LookupLayer) {
        layers.removeAll { it.get() == null || it.get() === layer }
        layers.add(WeakReference(layer))
    }

    private fun open(): LookupLayer? = layers.mapNotNull { it.get() }.firstOrNull { it.isOpen }

    val isOpen: Boolean get() = open() != null

    /** A controller's button or stick was the last input, not a finger on the screen. */
    @Volatile
    var padInUse = false
        private set

    /** The input was a controller ([pad]) or a finger: every popup's marks follow. */
    fun inputFrom(pad: Boolean) {
        if (padInUse == pad) return
        padInUse = pad
        layers.mapNotNull { it.get() }.forEach { it.padChanged(pad) }
    }

    /** A button pressed or let go while a popup is open: true when it was the popup's (all are). */
    fun onKey(code: Int, down: Boolean): Boolean {
        val layer = open() ?: return false
        if (!down) return true
        when (code) {
            KeyEvent.KEYCODE_BUTTON_Y -> layer.mineWord(audio = false)
            KeyEvent.KEYCODE_BUTTON_B -> layer.mineWord(audio = true)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BACK -> layer.close()
            KeyEvent.KEYCODE_BUTTON_L1 -> layer.tab(-1)
            KeyEvent.KEYCODE_BUTTON_R1 -> layer.tab(1)
            KeyEvent.KEYCODE_DPAD_DOWN -> layer.scrollBy((STEP * 4).toInt())
            KeyEvent.KEYCODE_DPAD_UP -> layer.scrollBy((-STEP * 4).toInt())
        }
        return true
    }

    private var stick = 0f

    private val tick = object : Runnable {
        override fun run() {
            val layer = open()
            if (layer == null || abs(stick) < DEAD) {
                stick = 0f
                return
            }
            // Further over, faster: the square of how far the stick is pushed.
            layer.scrollBy((stick * abs(stick) * STEP).toInt())
            main.postDelayed(this, FRAME_MS)
        }
    }

    /** The left stick's up and down while a popup is open: true when it was the popup's. */
    fun onStick(y: Float): Boolean {
        if (open() == null) return false
        val was = abs(stick) >= DEAD
        stick = y
        if (!was && abs(y) >= DEAD) main.post(tick)
        return true
    }

    private const val DEAD = 0.25f
    private const val STEP = 26f
    private const val FRAME_MS = 16L
}
