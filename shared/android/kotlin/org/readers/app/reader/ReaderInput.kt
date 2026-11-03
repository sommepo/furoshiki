// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import org.readers.app.reader.cursor.HoverCursor

/**
 * The controller, as far as it is the reader's: the button that holds the cursor, the sticks
 * while the cursor has them, the button that pauses the statistics. The activity asks here
 * about every key and every movement of a stick before the emulator hears of it.
 *
 * Buttons are Android's key codes (KeyEvent.KEYCODE_BUTTON_L2, ...): what the device's
 * controller sends, whatever the emulator maps it to. An app whose emulator asks the reader
 * about buttons in its native input (by the emulator's numbers: [Console.CURSOR_BUTTON_DEFAULT])
 * calls nothing here and learns the buttons from [buttonsChanged] and HoverCursor.watchButton.
 */
object ReaderInput {
    /** The button that pauses and resumes the statistics, or -1. */
    @Volatile
    var statsButton = -1
        set(value) {
            field = value
            buttonsChanged()
        }

    /** The button that goes on to the next choice ([NextChoice]), or -1. */
    @Volatile
    var nextChoiceButton = -1
        set(value) {
            field = value
            buttonsChanged()
        }

    /**
     * Told when [statsButton] or [nextChoiceButton] changed: for an app whose emulator asks the
     * reader about buttons in its native input instead of handing its key events here.
     */
    var buttonsChanged: () -> Unit = {}

    /** Keys whose press was taken: their release is the reader's too. */
    private val taken = HashSet<Int>()

    /** Whether each trigger is past its half-way point (L2, R2). */
    private val triggerDown = booleanArrayOf(false, false)

    /** True when the reader keeps the key and the emulator must not get it. */
    fun onKey(event: KeyEvent): Boolean {
        val code = event.keyCode
        when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (taken.contains(code)) return true // a repeat of a key that was taken
                if (!ReaderSession.isActive || !inGame()) return false
                if (code == HoverCursor.button) {
                    if (HoverCursor.onButton(code, true, HoverCursor.SOURCE_KEY)) {
                        taken.add(code)
                        return true
                    }
                    return false
                }
                if (code == statsButton && statsButton >= 0) {
                    ReaderSession.toggleStatsPaused()
                    taken.add(code)
                    return true
                }
                if (code == nextChoiceButton && nextChoiceButton >= 0) {
                    NextChoice.toggle()
                    taken.add(code)
                    return true
                }
                // While the cursor has the sticks, pressing one clicks where the cursor is.
                if (HoverCursor.takesSticks &&
                    (code == KeyEvent.KEYCODE_BUTTON_THUMBL || code == KeyEvent.KEYCODE_BUTTON_THUMBR)
                ) {
                    HoverCursor.click()
                    taken.add(code)
                    return true
                }
                return false
            }
            KeyEvent.ACTION_UP -> {
                if (!taken.remove(code)) return false
                if (code == HoverCursor.button) {
                    HoverCursor.onButton(code, false, HoverCursor.SOURCE_KEY)
                }
                return true
            }
        }
        return false
    }

    /** True when the reader keeps the movement (the sticks, while the cursor has them). */
    fun onMotion(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0 ||
            event.action != MotionEvent.ACTION_MOVE
        ) {
            return false
        }
        if (!ReaderSession.isActive) return false
        // A trigger that reports itself as an axis only.
        trigger(0, KeyEvent.KEYCODE_BUTTON_L2, maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)))
        trigger(1, KeyEvent.KEYCODE_BUTTON_R2, maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)))
        // Only while the sticks steer the cursor: with a second display as the trackpad they,
        // and the d-pad that comes in the same reports, are the game's.
        if (!HoverCursor.takesSticks) return false
        HoverCursor.onStick(0, event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y))
        HoverCursor.onStick(1, event.getAxisValue(MotionEvent.AXIS_Z), event.getAxisValue(MotionEvent.AXIS_RZ))
        return true
    }

    private fun trigger(index: Int, code: Int, value: Float) {
        val down = value > 0.5f
        if (down == triggerDown[index]) return
        triggerDown[index] = down
        if (code != HoverCursor.button) return
        if (down && !inGame()) return
        HoverCursor.onButton(code, down, HoverCursor.SOURCE_AXIS)
    }

    private fun inGame(): Boolean = try {
        ReaderNative.inGame()
    } catch (_: UnsatisfiedLinkError) {
        true // off the device (tests) there is only the game
    }
}
