// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.steam

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Made-up state reports of the Steam Controller (2026), read as the desktop's bridge reads them. */
class SteamPadTest {
    private class Heard : SteamPad.Sink {
        val said = ArrayList<String>()
        var moved = intArrayOf(0, 0)
        override fun button(button: SteamPad.Button, down: Boolean) { said += "$button ${if (down) "down" else "up"}" }
        override fun axis(axis: SteamPad.Axis, value: Int) { said += "$axis $value" }
        override fun pointerTouch(touching: Boolean) { said += "touch $touching" }
        override fun pointerMove(dx: Int, dy: Int) { moved = intArrayOf(moved[0] + dx, moved[1] + dy) }
        override fun pointerClick(left: Boolean, down: Boolean) { said += "click ${if (left) "left" else "right"} $down" }
        override fun wheel(amount: Int) { said += "wheel $amount" }
    }

    private fun report(
        buttons: Long = 0, leftX: Int = 0, leftY: Int = 0, triggerLeft: Int = 0,
        rightPadX: Int = 0, rightPadY: Int = 0, leftPadY: Int = 0,
    ): ByteArray {
        val data = ByteArray(SteamPad.STATE_LENGTH + 4)
        data[0] = SteamPad.STATE_REPORT.toByte()
        for (i in 0..3) data[2 + i] = ((buttons shr (8 * i)) and 0xFF).toByte()
        fun put(at: Int, value: Int) {
            data[at] = (value and 0xFF).toByte()
            data[at + 1] = ((value shr 8) and 0xFF).toByte()
        }
        put(6, triggerLeft)
        put(10, leftX)
        put(12, leftY)
        put(20, leftPadY)
        put(24, rightPadX)
        put(26, rightPadY)
        return data
    }

    @Test
    fun aReportThatIsNotStateIsPassedOver() {
        val heard = Heard()
        val pad = SteamPad()
        assertFalse(pad.report(byteArrayOf(0x79, 1), sink = heard))
        assertFalse(pad.report(ByteArray(10).also { it[0] = 0x42 }, sink = heard))
        assertTrue(heard.said.isEmpty())
    }

    @Test
    fun buttonsAreSaidAsTheyChangeTheGripsAndDotsAmongThem() {
        val heard = Heard()
        val pad = SteamPad()
        pad.report(report(), sink = heard)
        heard.said.clear()
        pad.report(report(buttons = 0x01L or 0x00020000L or 0x10L), sink = heard)
        assertEquals(listOf("A down", "GRIP1 down", "DOTS down"), heard.said)
        heard.said.clear()
        pad.report(report(buttons = 0x00020000L), sink = heard)
        assertEquals(listOf("A up", "DOTS up"), heard.said)
        // Menu (☰, 0x40) is Start and View (⧉, 0x4000) is Back, as in the hand.
        heard.said.clear()
        pad.report(report(buttons = 0x40L or 0x4000L), sink = heard)
        assertTrue("START down" in heard.said && "BACK down" in heard.said && "GRIP1 up" in heard.said)
    }

    @Test
    fun aStickUpIsNegativeAndARestingSticksWanderIsNotSaid() {
        val heard = Heard()
        val pad = SteamPad()
        pad.report(report(leftX = 0, leftY = 20000), sink = heard)
        assertTrue("LEFT_Y -20000" in heard.said)
        heard.said.clear()
        pad.report(report(leftX = 50, leftY = 20040), sink = heard)
        assertTrue(heard.said.none { it.startsWith("LEFT_") })
        pad.report(report(leftX = 0, leftY = 20000, triggerLeft = 32767), sink = heard)
        assertTrue("LEFT_TRIGGER 32767" in heard.said)
    }

    @Test
    fun theRightPadPointsAndItsPressIsTheLeftClick() {
        val heard = Heard()
        val pad = SteamPad(SteamPad.Tuning(trackball = false, haptics = false))
        val touch = SteamPad.RIGHT_PAD_TOUCH
        pad.report(report(buttons = touch, rightPadX = 0, rightPadY = 0), sink = heard)
        assertTrue("touch true" in heard.said)
        // Rightwards and upwards on the pad: the pointer goes right and up (y negative).
        for (step in 1..20) pad.report(report(buttons = touch, rightPadX = step * 1000, rightPadY = step * 600), sink = heard)
        assertTrue(heard.moved[0] > 0)
        assertTrue(heard.moved[1] < 0)
        pad.report(report(buttons = touch or SteamPad.RIGHT_PAD_CLICK, rightPadX = 20000, rightPadY = 12000), sink = heard)
        assertTrue("click left true" in heard.said)
        pad.report(report(), sink = heard)
        assertTrue("click left false" in heard.said)
        assertTrue("touch false" in heard.said)
    }

    @Test
    fun aFlickRollsOnAfterTheFingerLeaves() {
        val heard = Heard()
        val pad = SteamPad(SteamPad.Tuning(trackball = true, haptics = false))
        val touch = SteamPad.RIGHT_PAD_TOUCH
        pad.report(report(buttons = touch), sink = heard)
        for (step in 1..6) pad.report(report(buttons = touch, rightPadX = step * 5000), sink = heard)
        val atLift = heard.moved[0]
        repeat(20) { pad.report(report(), sink = heard) }
        assertTrue(heard.moved[0] > atLift)
    }

    @Test
    fun theLeftPadIsAWheelAndItsPressTheOtherButton() {
        val heard = Heard()
        val pad = SteamPad(SteamPad.Tuning(haptics = false))
        val touch = SteamPad.LEFT_PAD_TOUCH
        pad.report(report(buttons = touch, leftPadY = 0), sink = heard)
        pad.report(report(buttons = touch, leftPadY = 20000), sink = heard)
        assertTrue(heard.said.any { it.startsWith("wheel ") && it.removePrefix("wheel ").toInt() > 0 })
        pad.report(report(buttons = touch or SteamPad.LEFT_PAD_CLICK, leftPadY = 20000), sink = heard)
        assertTrue("click right true" in heard.said)
    }

    @Test
    fun goneLetsGoOfEverything() {
        val heard = Heard()
        val pad = SteamPad()
        pad.report(report(buttons = 0x01L or SteamPad.RIGHT_PAD_TOUCH, leftX = 30000), sink = heard)
        heard.said.clear()
        pad.release(heard)
        assertTrue("A up" in heard.said)
        assertTrue("LEFT_X 0" in heard.said)
        assertTrue("touch false" in heard.said)
    }

    @Test
    fun theDescriptorWithTheStateReportIsKnown() {
        assertTrue(SteamUsb.hasStateReport(byteArrayOf(0x06, 0x00, 0xFF.toByte(), 0x85.toByte(), 0x42, 0x09)))
        assertFalse(SteamUsb.hasStateReport(byteArrayOf(0x06, 0x00, 0xFF.toByte(), 0x85.toByte(), 0x01)))
    }

    @Test
    fun theMessagesAreTheDesktops() {
        assertEquals(listOf<Byte>(1, 0x87.toByte(), 3, 9, 0, 0), SteamPad.LIZARD_OFF.toList())
        val tick = SteamPad.hapticMessage(SteamPad.Pulse(1, 0x0384))
        assertEquals(listOf<Byte>(0x81.toByte(), 1, 0x84.toByte(), 0x03, 0, 0, 1, 0), tick.toList())
    }
}
