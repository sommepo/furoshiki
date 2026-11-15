// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.steam

import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

/**
 * Valve's Steam Controller (2026) without Steam, as the desktop's bridge reads it
 * (shared/reader/desktop/steam_bridge.cpp, after SDL's SDL_hidapi_steam_triton.c): one state report
 * at a time goes in, what it amounts to goes out to a [Sink]. The same layout, the same pads: the
 * right one moves the pointer as a trackball does, pressed it is a click; the left one is a wheel,
 * pressed the other button. Nothing of Android here, so that it is tested on the computer.
 */
class SteamPad(var tuning: Tuning = Tuning()) {
    enum class Button {
        A, B, X, Y, BACK, GUIDE, START, LEFT_STICK, RIGHT_STICK, LEFT_SHOULDER, RIGHT_SHOULDER,
        DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
        /** The four grips and the "···" button: the reader's own, as on the desktop. */
        GRIP1, GRIP2, GRIP3, GRIP4, DOTS,
    }

    /** Sticks from -32768 to 32767 (right and down positive), triggers from 0 to 32767. */
    enum class Axis { LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y, LEFT_TRIGGER, RIGHT_TRIGGER }

    interface Sink {
        fun button(button: Button, down: Boolean)
        fun axis(axis: Axis, value: Int)
        /** The right pad touched or let go (the pointer's to show). */
        fun pointerTouch(touching: Boolean) {}
        /** The pointer moves by whole pixels of [Tuning.swipePixels]' scale (right and down positive). */
        fun pointerMove(dx: Int, dy: Int)
        /** A pad pressed down: the right one is the left button ([left] true). */
        fun pointerClick(left: Boolean, down: Boolean)
        /** The wheel: 120 a notch, up positive. */
        fun wheel(amount: Int) {}
        /** Everything one report said has been said (a moment to send the sticks at once). */
        fun reportDone() {}
    }

    /** How the pads feel; the defaults are the desktop's. */
    data class Tuning(
        val swipePixels: Double = 1100.0,
        val acceleration: Double = 1.0,
        val smoothing: Double = 0.6,
        val scrollNotches: Double = 14.0,
        val trackball: Boolean = true,
        val friction: Double = 0.970,
        val haptics: Boolean = true,
        val tickPixels: Double = 45.0,
        val tickMicros: Int = 900,
        val clickMicros: Int = 2600,
    )

    /** A tick for one pad's motor: [side] 0 the right pad, 1 the left. */
    data class Pulse(val side: Int, val micros: Int)

    private var buttons = 0L
    private val axes = IntArray(Axis.entries.size)
    private val axisSent = BooleanArray(Axis.entries.size)
    private var rightTouched = false
    private var rightX = 0.0
    private var rightY = 0.0
    private var velocityX = 0.0
    private var velocityY = 0.0
    private var restX = 0.0
    private var restY = 0.0
    private var rolling = false
    private var leftTouched = false
    private var leftY = 0.0
    private var wheel = 0.0
    private var wheelNotch = 0.0
    private var travel = 0.0
    private var sinceTick = 0
    private val pulses = ArrayDeque<Pulse>()

    /** The buttons of the last report, as the controller sent them. */
    val rawButtons: Long get() = buttons

    /** A tick the last reports asked for, or null. */
    fun takePulse(): Pulse? = pulses.removeLastOrNull()

    /** One report: false for anything but a whole state report. */
    fun report(data: ByteArray, size: Int = data.size, sink: Sink): Boolean {
        if (size < STATE_LENGTH || (data[0].toInt() and 0xFF) != STATE_REPORT) return false
        val now = u8(data, BUTTONS_AT) or (u8(data, BUTTONS_AT + 1) shl 8) or
            (u8(data, BUTTONS_AT + 2) shl 16) or (u8(data, BUTTONS_AT + 3) shl 24)
        val changed = now xor buttons
        for ((bit, button) in BUTTON_BITS) {
            if (changed and bit != 0L) sink.button(button, now and bit != 0L)
        }
        // A resting stick wanders by a few units in every report: real movement only.
        fun axis(which: Axis, value: Int, slack: Int) {
            val at = which.ordinal
            if (!axisSent[at] || abs(value - axes[at]) > slack) {
                axes[at] = value
                axisSent[at] = true
                sink.axis(which, value)
            }
        }
        axis(Axis.LEFT_X, s16(data, LEFT_X_AT), 96)
        axis(Axis.LEFT_Y, (-s16(data, LEFT_Y_AT)).coerceIn(-32768, 32767), 96)
        axis(Axis.RIGHT_X, s16(data, RIGHT_X_AT), 96)
        axis(Axis.RIGHT_Y, (-s16(data, RIGHT_Y_AT)).coerceIn(-32768, 32767), 96)
        axis(Axis.LEFT_TRIGGER, s16(data, TRIGGER_LEFT_AT).coerceIn(0, 32767), 0)
        axis(Axis.RIGHT_TRIGGER, s16(data, TRIGGER_RIGHT_AT).coerceIn(0, 32767), 0)

        // The pads pressed down are the pointer's buttons; a pad has no switch to feel: its motor ticks.
        sinceTick++
        if (changed and RIGHT_PAD_CLICK != 0L) {
            val down = now and RIGHT_PAD_CLICK != 0L
            sink.pointerClick(left = true, down = down)
            tick(0, if (down) tuning.clickMicros else tuning.clickMicros / 2)
        }
        if (changed and LEFT_PAD_CLICK != 0L) {
            val down = now and LEFT_PAD_CLICK != 0L
            sink.pointerClick(left = false, down = down)
            tick(1, if (down) tuning.clickMicros else tuning.clickMicros / 2)
        }

        // The right pad moves the pointer, as a trackball does.
        val scale = tuning.swipePixels / 65536.0
        val alpha = tuning.smoothing.coerceIn(0.05, 1.0)
        val touching = now and RIGHT_PAD_TOUCH != 0L
        if (touching != rightTouched) sink.pointerTouch(touching)
        if (touching) {
            val x = s16(data, RIGHT_PAD_X_AT).toDouble()
            val y = s16(data, RIGHT_PAD_Y_AT).toDouble()
            if (!rightTouched) {
                rightTouched = true
                rightX = x
                rightY = y
                velocityX = 0.0
                velocityY = 0.0
            } else {
                val nx = rightX + (x - rightX) * alpha
                val ny = rightY + (y - rightY) * alpha
                val dx = nx - rightX
                val dy = ny - rightY
                rightX = nx
                rightY = ny
                val speed = hypot(dx, dy)
                val gain = scale * (1.0 + tuning.acceleration * min(speed / 600.0, 1.0))
                val px = dx * gain
                val py = -dy * gain
                velocityX += (px - velocityX) * 0.3
                velocityY += (py - velocityY) * 0.3
                point(px, py, sink)
            }
            rolling = false
        } else {
            if (rightTouched) rolling = tuning.trackball && hypot(velocityX, velocityY) > 1.5
            rightTouched = false
            if (rolling) {
                val friction = tuning.friction.coerceIn(0.5, 0.999)
                velocityX *= friction
                velocityY *= friction
                if (abs(velocityX) < 0.4 && abs(velocityY) < 0.4) rolling = false else point(velocityX, velocityY, sink)
            }
            if (!rolling) {
                velocityX = 0.0; velocityY = 0.0; restX = 0.0; restY = 0.0
            }
        }

        // The left pad is the wheel.
        if (now and LEFT_PAD_TOUCH != 0L) {
            val y = s16(data, LEFT_PAD_Y_AT).toDouble()
            if (leftTouched) {
                val turned = (y - leftY) * tuning.scrollNotches * 120.0 / 65536.0
                wheel += turned
                wheelNotch += turned
                val notches = (wheelNotch / 120.0).toInt()
                if (notches != 0) {
                    wheelNotch -= notches * 120.0
                    tick(1, tuning.tickMicros)
                }
                val whole = wheel.toInt()
                if (whole != 0) {
                    wheel -= whole
                    sink.wheel(whole)
                }
            }
            leftTouched = true
            leftY = y
        } else {
            leftTouched = false
            wheel = 0.0
            wheelNotch = 0.0
        }
        buttons = now
        sink.reportDone()
        return true
    }

    /** The controller is gone: everything is let go. */
    fun release(sink: Sink) {
        for ((bit, button) in BUTTON_BITS) if (buttons and bit != 0L) sink.button(button, false)
        if (buttons and RIGHT_PAD_CLICK != 0L) sink.pointerClick(left = true, down = false)
        if (buttons and LEFT_PAD_CLICK != 0L) sink.pointerClick(left = false, down = false)
        if (rightTouched) sink.pointerTouch(false)
        for (which in Axis.entries) {
            if (axisSent[which.ordinal] && axes[which.ordinal] != 0) sink.axis(which, 0)
            axes[which.ordinal] = 0
            axisSent[which.ordinal] = false
        }
        sink.reportDone()
        buttons = 0
        rightTouched = false; leftTouched = false; rolling = false
        velocityX = 0.0; velocityY = 0.0; restX = 0.0; restY = 0.0; wheel = 0.0; wheelNotch = 0.0; travel = 0.0
        pulses.clear()
    }

    private fun point(px: Double, py: Double, sink: Sink) {
        restX += px
        restY += py
        val ix = restX.toInt()
        val iy = restY.toInt()
        if (ix == 0 && iy == 0) return
        restX -= ix
        restY -= iy
        sink.pointerMove(ix, iy)
        // A tick in the pad now and then, as a trackball's bearings would give.
        travel += sqrt((ix * ix + iy * iy).toDouble())
        if (travel >= max(tuning.tickPixels, 5.0) && sinceTick >= 3) {
            travel = 0.0
            sinceTick = 0
            tick(0, tuning.tickMicros)
        }
    }

    private fun tick(side: Int, micros: Int) {
        if (tuning.haptics && pulses.size < 4) pulses.addLast(Pulse(side, micros))
    }

    companion object {
        const val VENDOR = 0x28DE
        /** Wired, the puck, the Steam Machine's receiver. */
        val PRODUCTS = setOf(0x1302, 0x1304, 0x1305)

        const val STATE_REPORT = 0x42
        const val STATE_LENGTH = 30
        /** A puck's report that a controller has gone (its second byte 1). */
        const val WIRELESS_REPORT = 0x79

        /** The feature report that switches the controller's own keyboard and mouse ("lizard mode")
         *  off: report 1, ID_SET_SETTINGS_VALUES (0x87), one setting, SETTING_LIZARD_MODE (9) = 0.
         *  The firmware switches it back on after a few seconds without it. */
        val LIZARD_OFF = byteArrayOf(1, 0x87.toByte(), 3, 9, 0, 0)
        const val FEATURE_LENGTH = 64

        /** A pad's tick: report 0x81, the side, then on time, off time and repeat count (16 bits each). */
        const val HAPTIC_REPORT = 0x81
        fun hapticMessage(pulse: Pulse, length: Int = 8): ByteArray {
            val on = pulse.micros.coerceIn(0, 0xFFFF)
            val message = ByteArray(maxOf(length, 8))
            message[0] = HAPTIC_REPORT.toByte()
            message[1] = pulse.side.toByte()
            message[2] = (on and 0xFF).toByte()
            message[3] = (on shr 8).toByte()
            message[6] = 1
            return message
        }

        // The report: ID, a counter, 32 bits of buttons, the triggers, the sticks, the left pad
        // (x, y, pressure), the right pad (x, y, pressure); little-endian, 16 bits each after the buttons.
        private const val BUTTONS_AT = 2
        private const val TRIGGER_LEFT_AT = 6
        private const val TRIGGER_RIGHT_AT = 8
        private const val LEFT_X_AT = 10
        private const val LEFT_Y_AT = 12
        private const val RIGHT_X_AT = 14
        private const val RIGHT_Y_AT = 16
        private const val LEFT_PAD_Y_AT = 20
        private const val RIGHT_PAD_X_AT = 24
        private const val RIGHT_PAD_Y_AT = 26

        const val RIGHT_PAD_TOUCH = 0x00200000L
        const val RIGHT_PAD_CLICK = 0x00400000L
        const val LEFT_PAD_TOUCH = 0x02000000L
        const val LEFT_PAD_CLICK = 0x04000000L

        // The menu button (☰) is 0x40 and View (⧉) 0x4000, as the desktop's bridge found in the hand.
        // The grips' and the sticks' touch sensors are no buttons: a hand on the controller sets them.
        val BUTTON_BITS: List<Pair<Long, Button>> = listOf(
            0x00000001L to Button.A, 0x00000002L to Button.B, 0x00000004L to Button.X, 0x00000008L to Button.Y,
            0x00080000L to Button.LEFT_SHOULDER, 0x00000200L to Button.RIGHT_SHOULDER,
            0x00004000L to Button.BACK, 0x00000040L to Button.START, 0x00010000L to Button.GUIDE,
            0x00008000L to Button.LEFT_STICK, 0x00000020L to Button.RIGHT_STICK,
            0x00002000L to Button.DPAD_UP, 0x00000400L to Button.DPAD_DOWN,
            0x00001000L to Button.DPAD_LEFT, 0x00000800L to Button.DPAD_RIGHT,
            0x00020000L to Button.GRIP1, 0x00040000L to Button.GRIP2,
            0x00000080L to Button.GRIP3, 0x00000100L to Button.GRIP4, 0x00000010L to Button.DOTS,
        )

        private fun u8(data: ByteArray, at: Int): Long = (data[at].toInt() and 0xFF).toLong()

        private fun s16(data: ByteArray, at: Int): Int =
            ((data[at].toInt() and 0xFF) or ((data[at + 1].toInt() and 0xFF) shl 8)).toShort().toInt()
    }
}
