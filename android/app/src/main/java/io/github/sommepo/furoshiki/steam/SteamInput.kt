// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.steam

import android.app.Activity
import android.os.SystemClock
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import io.github.sommepo.furoshiki.reader.cursor.HoverCursor

/**
 * The Steam Controller as any other controller to the screen in front: its buttons are Android's
 * gamepad keys and its sticks and triggers a joystick's axes, handed to the activity as if they
 * had come from the system, so that the controller map, the input wizard, the shortcuts and the
 * games take them as they take every controller's. The grips and the "···" button are buttons
 * 1 to 5 (shared/controllers/controllers.json's `android`), the reader's to give actions to.
 *
 * In a game ([inGame]) the right pad is the reader's pointer (HoverCursor, as the desktop's pad
 * and mouse are): touched it shows, moved it points, pressed it taps the word; the left pad
 * pressed opens the Reader menu ([onAction]). On the home screen the left stick goes along the
 * page as the d-pad does.
 */
class SteamInput(
    private val activity: Activity,
    private val inGame: Boolean,
    private val onAction: (String) -> Unit = {},
) : SteamPad.Sink {
    private val axes = FloatArray(SteamPad.Axis.entries.size)
    private var axesChanged = false
    private val stickKeys = BooleanArray(4) // left, right, up, down from the left stick, on the home screen
    private val downTimes = HashMap<Int, Long>()

    override fun button(button: SteamPad.Button, down: Boolean) {
        val code = KEYS[button] ?: return
        key(code, down)
    }

    override fun axis(axis: SteamPad.Axis, value: Int) {
        axes[axis.ordinal] = when (axis) {
            SteamPad.Axis.LEFT_TRIGGER, SteamPad.Axis.RIGHT_TRIGGER -> value / 32767f
            else -> (value / 32767f).coerceIn(-1f, 1f)
        }
        axesChanged = true
    }

    override fun reportDone() {
        if (!axesChanged) return
        axesChanged = false
        if (!inGame) stickAsDpad()
        val now = SystemClock.uptimeMillis()
        val properties = arrayOf(MotionEvent.PointerProperties().apply { id = 0; toolType = MotionEvent.TOOL_TYPE_UNKNOWN })
        val coords = arrayOf(MotionEvent.PointerCoords().apply {
            setAxisValue(MotionEvent.AXIS_X, axes[SteamPad.Axis.LEFT_X.ordinal])
            setAxisValue(MotionEvent.AXIS_Y, axes[SteamPad.Axis.LEFT_Y.ordinal])
            setAxisValue(MotionEvent.AXIS_Z, axes[SteamPad.Axis.RIGHT_X.ordinal])
            setAxisValue(MotionEvent.AXIS_RZ, axes[SteamPad.Axis.RIGHT_Y.ordinal])
            setAxisValue(MotionEvent.AXIS_LTRIGGER, axes[SteamPad.Axis.LEFT_TRIGGER.ordinal])
            setAxisValue(MotionEvent.AXIS_RTRIGGER, axes[SteamPad.Axis.RIGHT_TRIGGER.ordinal])
            setAxisValue(MotionEvent.AXIS_BRAKE, axes[SteamPad.Axis.LEFT_TRIGGER.ordinal])
            setAxisValue(MotionEvent.AXIS_GAS, axes[SteamPad.Axis.RIGHT_TRIGGER.ordinal])
        })
        val event = MotionEvent.obtain(
            now, now, MotionEvent.ACTION_MOVE, 1, properties, coords, 0, 0, 1f, 1f,
            DEVICE_ID, 0, InputDevice.SOURCE_JOYSTICK, 0,
        )
        try {
            activity.dispatchGenericMotionEvent(event)
        } finally {
            event.recycle()
        }
    }

    override fun pointerTouch(touching: Boolean) {
        if (inGame) HoverCursor.padTouch(touching)
    }

    override fun pointerMove(dx: Int, dy: Int) {
        if (inGame) HoverCursor.padMove(dx.toFloat(), dy.toFloat())
    }

    override fun pointerClick(left: Boolean, down: Boolean) {
        if (!down) return
        if (!inGame) {
            // The home screen: the right pad pressed takes as A does.
            if (left) {
                key(KeyEvent.KEYCODE_BUTTON_A, true)
                key(KeyEvent.KEYCODE_BUTTON_A, false)
            }
            return
        }
        if (left) HoverCursor.click() else onAction("reader_menu")
    }

    /** Lets go of every key held and puts the sticks at rest: the controller is gone, or the screen goes. */
    fun releaseAll() {
        for (code in downTimes.keys.toList()) key(code, false)
        if (axes.any { it != 0f }) {
            axes.fill(0f)
            axesChanged = true
            reportDone()
        }
        if (inGame) HoverCursor.padTouch(false)
    }

    private fun key(code: Int, down: Boolean) {
        val now = SystemClock.uptimeMillis()
        val downTime = if (down) now.also { downTimes[code] = it } else downTimes.remove(code) ?: return
        val event = KeyEvent(
            downTime, now, if (down) KeyEvent.ACTION_DOWN else KeyEvent.ACTION_UP, code, 0, 0,
            DEVICE_ID, 0, 0, InputDevice.SOURCE_GAMEPAD,
        )
        activity.dispatchKeyEvent(event)
    }

    /** The left stick pushed past half way on the home screen: the d-pad's keys. */
    private fun stickAsDpad() {
        val x = axes[SteamPad.Axis.LEFT_X.ordinal]
        val y = axes[SteamPad.Axis.LEFT_Y.ordinal]
        val now = booleanArrayOf(x < -0.5f, x > 0.5f, y < -0.5f, y > 0.5f)
        val codes = intArrayOf(KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN)
        for (at in 0..3) {
            if (now[at] != stickKeys[at]) {
                stickKeys[at] = now[at]
                key(codes[at], now[at])
            }
        }
    }

    companion object {
        /** The device events are said to come from: none of the system's. */
        const val DEVICE_ID = 0x5C00

        val KEYS: Map<SteamPad.Button, Int> = mapOf(
            SteamPad.Button.A to KeyEvent.KEYCODE_BUTTON_A,
            SteamPad.Button.B to KeyEvent.KEYCODE_BUTTON_B,
            SteamPad.Button.X to KeyEvent.KEYCODE_BUTTON_X,
            SteamPad.Button.Y to KeyEvent.KEYCODE_BUTTON_Y,
            SteamPad.Button.BACK to KeyEvent.KEYCODE_BUTTON_SELECT,
            SteamPad.Button.START to KeyEvent.KEYCODE_BUTTON_START,
            SteamPad.Button.GUIDE to KeyEvent.KEYCODE_BUTTON_MODE,
            SteamPad.Button.LEFT_STICK to KeyEvent.KEYCODE_BUTTON_THUMBL,
            SteamPad.Button.RIGHT_STICK to KeyEvent.KEYCODE_BUTTON_THUMBR,
            SteamPad.Button.LEFT_SHOULDER to KeyEvent.KEYCODE_BUTTON_L1,
            SteamPad.Button.RIGHT_SHOULDER to KeyEvent.KEYCODE_BUTTON_R1,
            SteamPad.Button.DPAD_UP to KeyEvent.KEYCODE_DPAD_UP,
            SteamPad.Button.DPAD_DOWN to KeyEvent.KEYCODE_DPAD_DOWN,
            SteamPad.Button.DPAD_LEFT to KeyEvent.KEYCODE_DPAD_LEFT,
            SteamPad.Button.DPAD_RIGHT to KeyEvent.KEYCODE_DPAD_RIGHT,
            SteamPad.Button.GRIP1 to KeyEvent.KEYCODE_BUTTON_1,
            SteamPad.Button.GRIP2 to KeyEvent.KEYCODE_BUTTON_2,
            SteamPad.Button.GRIP3 to KeyEvent.KEYCODE_BUTTON_3,
            SteamPad.Button.GRIP4 to KeyEvent.KEYCODE_BUTTON_4,
            SteamPad.Button.DOTS to KeyEvent.KEYCODE_BUTTON_5,
        )
    }
}
