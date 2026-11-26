// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.readers.app.reader.ui.PopupBlur
import org.readers.app.reader.ui.PopupBlur.Box
import org.readers.app.reader.ui.PopupBlur.Picture

class PopupBlurTest {
    private val black = 0xff000000.toInt()

    @Test
    fun theBlurIsOnWhereThePopupIsSeeThrough() {
        // The look's opacity under 100, whatever the look.
        assertTrue(PopupBlur.on("kon-neo", 80))
        assertTrue(PopupBlur.on("paper", 20))
        // A solid look at its own opacity (Android's 100, the desktop's 0) has none.
        assertFalse(PopupBlur.on("kon-neo", 100))
        assertFalse(PopupBlur.on("kon-neo", 0))
        // The looks that are see-through themselves.
        for (theme in listOf("glass", "kuro", "furoshiki")) {
            assertTrue(theme, PopupBlur.on(theme, 100))
            assertTrue(theme, PopupBlur.on(theme, 0))
        }
        // The classic popup is drawn solid by the app's own views.
        assertFalse(PopupBlur.on("classic", 60))
    }

    /** A frame whose pixel says where it is: red its column, green its row. */
    private fun frame(width: Int, height: Int) =
        Picture(width, height, IntArray(width * height) { black or (it % width shl 16) or (it / width shl 8) })

    @Test
    fun whatIsUnderThePopupIsCutAtTheFramesScale() {
        // A surface of 1200x600 on the screen at (100, 50), copied six times smaller.
        val frame = frame(200, 100)
        val surface = Box(100, 50, 1300, 650)
        val cut = PopupBlur.under(frame, surface, Box(400, 170, 700, 350))!!
        assertEquals(50, cut.width)
        assertEquals(30, cut.height)
        // The popup's corner is 300 right and 120 down of the surface's: 50 and 20 in the frame.
        assertEquals(black or (50 shl 16) or (20 shl 8), cut.pixels[0])
        assertEquals(black or (99 shl 16) or (49 shl 8), cut.pixels[cut.pixels.size - 1])
    }

    @Test
    fun pastTheSurfaceItIsBlack() {
        // The picture stands between black bars: the popup begins 60 left of it and 30 above.
        val frame = frame(200, 100)
        val surface = Box(100, 50, 1300, 650)
        val cut = PopupBlur.under(frame, surface, Box(40, 20, 340, 200))!!
        assertEquals(50, cut.width)
        assertEquals(30, cut.height)
        assertEquals(black, cut.pixels[0])
        assertEquals(black, cut.pixels[4 * cut.width + 20])  // above the picture
        assertEquals(black, cut.pixels[20 * cut.width + 9])  // left of it
        assertEquals(black or (0 shl 16) or (0 shl 8), cut.pixels[5 * cut.width + 10]) // its first pixel
        assertNull(PopupBlur.under(frame, surface, Box(0, 0, 0, 10)))
        assertNull(PopupBlur.under(frame, Box(0, 0, 0, 0), Box(0, 0, 10, 10)))
    }

    @Test
    fun theBlurSpreadsAPointAndKeepsAFlatPicture() {
        val flat = Picture(12, 9, IntArray(12 * 9) { 0xff336699.toInt() })
        PopupBlur.blur(flat, 3)
        assertTrue(flat.pixels.all { it == 0xff336699.toInt() })

        val point = Picture(21, 21, IntArray(21 * 21) { black })
        point.pixels[10 * 21 + 10] = 0xffffffff.toInt()
        PopupBlur.blur(point, 2)
        val middle = point.pixels[10 * 21 + 10] and 0xff
        val beside = point.pixels[10 * 21 + 12] and 0xff
        val far = point.pixels[0] and 0xff
        assertTrue("the point is dimmer", middle < 255)
        assertTrue("its light is beside it", beside in 1 until middle + 1)
        assertEquals(0, far)
        // Opaque everywhere, and much the same along a row as along a column.
        assertTrue(point.pixels.all { it ushr 24 == 0xff })
        assertTrue(Math.abs((point.pixels[10 * 21 + 12] and 0xff) - (point.pixels[12 * 21 + 10] and 0xff)) <= 3)
    }

    @Test
    fun theReachFollowsTheScreenAndTheCopy() {
        assertEquals(6, PopupBlur.reach(2.5f, 6f))
        assertEquals(1, PopupBlur.reach(1f, 40f))
    }
}
