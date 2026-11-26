// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.view.SurfaceView
import java.lang.ref.WeakReference

/**
 * The game blurred behind a see-through popup, as a computer's compositor does it for the desktop
 * popup (LookupPopup::UpdateBackdropBlur). The popup's page cannot see the game's surface, so its
 * own backdrop-filter blurs nothing here: [LookupPopupView] takes a small copy of the game's
 * picture ([ReaderHost.copyFrame]), cuts out what lies under the popup ([under]), blurs it
 * ([blur]) and draws it behind the page. The sums are here, without Android's classes, for the
 * JVM tests (PopupBlurTest).
 */
object PopupBlur {
    /** The looks whose ground is see-through by itself. */
    val THEMES = setOf("glass", "kuro", "furoshiki")

    /** The copy of the game's picture is this many times smaller than the screen. */
    const val SHRINK = 6

    /** How far the blur reaches, in dp of the screen. */
    const val REACH_DP = 14f

    /** While a popup is shown the picture behind it is taken again this often. */
    const val REFRESH_MS = 300L

    /** The game's surface, where an app plays the game itself ([ReaderHost.provideFrames]). */
    @Volatile
    var surface: WeakReference<SurfaceView>? = null

    /**
     * Whether the game is blurred behind the popup: where the look's opacity makes it see-through
     * (1 to 99), else in the looks that are see-through themselves. An opacity of 0 or 100 is the
     * look's own ground here (Android's setting starts at 100, the desktop's at 0). The classic
     * popup is never see-through.
     */
    fun on(theme: String, opacity: Int): Boolean = when {
        theme == "classic" -> false
        opacity in 1..99 -> true
        else -> theme in THEMES
    }

    /** A rectangle in whole pixels; [right] and [bottom] are outside it. */
    data class Box(val left: Int, val top: Int, val right: Int, val bottom: Int) {
        val width get() = right - left
        val height get() = bottom - top
    }

    /** A picture as ARGB pixels, row after row. */
    class Picture(val width: Int, val height: Int, val pixels: IntArray)

    /**
     * What of [frame] lies under [popup]: [frame] is a copy of all of [surface], and both boxes
     * are in the same coordinates (the screen's). The answer keeps the frame's scale, so it is as
     * many times smaller than the popup as the frame is than the surface. Where the popup reaches
     * past the surface the answer is black, as the screen is there. Null when either is empty.
     */
    fun under(frame: Picture, surface: Box, popup: Box): Picture? {
        if (frame.width <= 0 || frame.height <= 0 || surface.width <= 0 || surface.height <= 0) return null
        if (popup.width <= 0 || popup.height <= 0) return null
        val width = maxOf(1, Math.round(popup.width.toFloat() * frame.width / surface.width))
        val height = maxOf(1, Math.round(popup.height.toFloat() * frame.height / surface.height))
        val out = IntArray(width * height) { BLACK }
        for (y in 0 until height) {
            // The middle of this pixel on the screen, then in the frame.
            val screenY = popup.top + (y + 0.5f) * popup.height / height
            val frameY = Math.floor(((screenY - surface.top) * frame.height / surface.height).toDouble()).toInt()
            if (frameY < 0 || frameY >= frame.height) continue
            for (x in 0 until width) {
                val screenX = popup.left + (x + 0.5f) * popup.width / width
                val frameX = Math.floor(((screenX - surface.left) * frame.width / surface.width).toDouble()).toInt()
                if (frameX < 0 || frameX >= frame.width) continue
                out[y * width + x] = frame.pixels[frameY * frame.width + frameX] or BLACK
            }
        }
        return Picture(width, height, out)
    }

    /**
     * Blurs [picture] in place by [radius] pixels: three box blurs along the rows and three along
     * the columns, which is close to a Gaussian. The edge's pixels stand in for what is past it.
     */
    fun blur(picture: Picture, radius: Int) {
        if (radius <= 0 || picture.width <= 0 || picture.height <= 0) return
        val line = IntArray(maxOf(picture.width, picture.height))
        repeat(3) {
            for (y in 0 until picture.height) box(picture.pixels, y * picture.width, 1, picture.width, radius, line)
            for (x in 0 until picture.width) box(picture.pixels, x, picture.width, picture.height, radius, line)
        }
    }

    /** One box blur of [count] pixels of [pixels], from [start] every [step]. */
    private fun box(pixels: IntArray, start: Int, step: Int, count: Int, radius: Int, line: IntArray) {
        for (i in 0 until count) line[i] = pixels[start + i * step]
        val span = 2 * radius + 1
        var red = 0
        var green = 0
        var blue = 0
        for (i in -radius..radius) {
            val p = line[i.coerceIn(0, count - 1)]
            red += p shr 16 and 0xff
            green += p shr 8 and 0xff
            blue += p and 0xff
        }
        for (i in 0 until count) {
            pixels[start + i * step] = BLACK or (red / span shl 16) or (green / span shl 8) or (blue / span)
            val leaves = line[(i - radius).coerceIn(0, count - 1)]
            val comes = line[(i + radius + 1).coerceIn(0, count - 1)]
            red += (comes shr 16 and 0xff) - (leaves shr 16 and 0xff)
            green += (comes shr 8 and 0xff) - (leaves shr 8 and 0xff)
            blue += (comes and 0xff) - (leaves and 0xff)
        }
    }

    /** The blur's reach in the small copy's pixels, for a screen of [density] and a copy [shrink] times smaller. */
    fun reach(density: Float, shrink: Float): Int = maxOf(1, Math.round(REACH_DP * density / maxOf(1f, shrink)))

    private const val BLACK = 0xff000000.toInt()
}
