// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import org.readers.app.reader.cursor.HoverCursor
import org.readers.app.reader.ui.PopupKeys
import kotlin.math.abs

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
                // A controller is the input now: the popup marks its buttons.
                if (KeyEvent.isGamepadButton(code) || event.source and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD) {
                    PopupKeys.inputFrom(pad = true)
                }
                if (!ReaderSession.isActive || !inGame()) return false
                if (code == HoverCursor.button) {
                    if (HoverCursor.onButton(code, true, HoverCursor.SOURCE_KEY)) {
                        taken.add(code)
                        return true
                    }
                    return false
                }
                // An open popup is modal: the controller's buttons are its own (PopupKeys).
                if (PopupKeys.isOpen && (KeyEvent.isGamepadButton(code) || code == KeyEvent.KEYCODE_BACK ||
                        code == KeyEvent.KEYCODE_DPAD_UP || code == KeyEvent.KEYCODE_DPAD_DOWN ||
                        code == KeyEvent.KEYCODE_DPAD_LEFT || code == KeyEvent.KEYCODE_DPAD_RIGHT)) {
                    PopupKeys.onKey(code, true)
                    taken.add(code)
                    return true
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
        if (maxOf(abs(event.getAxisValue(MotionEvent.AXIS_X)), abs(event.getAxisValue(MotionEvent.AXIS_Y)),
                abs(event.getAxisValue(MotionEvent.AXIS_Z)), abs(event.getAxisValue(MotionEvent.AXIS_RZ))) > 0.4f) {
            PopupKeys.inputFrom(pad = true) // a stick pushed: a controller is the input
        }
        // A trigger that reports itself as an axis only.
        trigger(0, KeyEvent.KEYCODE_BUTTON_L2, maxOf(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)))
        trigger(1, KeyEvent.KEYCODE_BUTTON_R2, maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)))
        // An open popup takes the left stick to scroll, and the game gets none of it meanwhile.
        if (PopupKeys.onStick(event.getAxisValue(MotionEvent.AXIS_Y))) return true
        // Only while the sticks steer the cursor: with a second display as the trackpad they,
        // and the d-pad that comes in the same reports, are the game's.
        if (!HoverCursor.takesSticks) return false
        HoverCursor.onStick(0, event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y))
        HoverCursor.onStick(1, event.getAxisValue(MotionEvent.AXIS_Z), event.getAxisValue(MotionEvent.AXIS_RZ))
        return true
    }

    private fun trigger(index: Int, code: Int, value: Float) {
        // Down past half-way, up only under a quarter: a long trigger held near the middle
        // wavers there, and each crossing was a press.
        val down = if (triggerDown[index]) value > 0.25f else value > 0.5f
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
