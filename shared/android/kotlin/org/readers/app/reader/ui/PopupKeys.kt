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

    /**
     * Each key code of the controller and the console's button it presses in the game in play
     * (`ButtonMap`: south, east, west, north...), so that the popup's △ is the game's △. Empty:
     * Android's own (Y north, B east, A south).
     */
    @Volatile
    var buttons: Map<Int, String> = emptyMap()

    private fun buttonOf(code: Int): String? = buttons[code] ?: when (code) {
        KeyEvent.KEYCODE_BUTTON_Y -> "north"
        KeyEvent.KEYCODE_BUTTON_B -> "east"
        KeyEvent.KEYCODE_BUTTON_A -> "south"
        KeyEvent.KEYCODE_BUTTON_X -> "west"
        else -> null
    }.takeIf { buttons.isEmpty() }

    /** A button pressed or let go while a popup is open: true when it was the popup's (all are). */
    fun onKey(code: Int, down: Boolean): Boolean {
        val layer = open() ?: return false
        if (!down) return true
        if (code == KeyEvent.KEYCODE_BACK) {
            layer.close()
            return true
        }
        when (buttonOf(code)) {
            "north" -> layer.mineWord(audio = false)
            "east" -> layer.mineWord(audio = true)
            "south" -> layer.close()
        }
        when (code) {
            KeyEvent.KEYCODE_BUTTON_L1 -> layer.tab(-1)
            KeyEvent.KEYCODE_BUTTON_R1 -> layer.tab(1)
            KeyEvent.KEYCODE_DPAD_LEFT -> layer.kanjiStep(-1)
            KeyEvent.KEYCODE_DPAD_RIGHT -> layer.kanjiStep(1)
            KeyEvent.KEYCODE_DPAD_DOWN -> layer.entry(1, (STEP * 4).toInt())
            KeyEvent.KEYCODE_DPAD_UP -> layer.entry(-1, (-STEP * 4).toInt())
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
