// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import android.graphics.Bitmap
import android.graphics.Rect
import android.view.SurfaceView
import io.github.sommepo.furoshiki.GameNative
import io.github.sommepo.furoshiki.reader.Console
import io.github.sommepo.furoshiki.reader.ReaderNative
import io.github.sommepo.furoshiki.reader.ui.ReaderHost

/**
 * Where OCR takes its pictures from ([OcrSource]): the game's own surface when Furoshiki plays it,
 * or a capture of the screen when another app does (companion/ScreenCapture).
 */
interface OcrFrames {
    /** The whole picture's size, in its own pixels; 0 while there is none. */
    val width: Int
    val height: Int

    /** A copy of the whole picture at most [maxWidth] wide, or null; [done] on the main thread. */
    fun copy(maxWidth: Int, done: (Bitmap?) -> Unit)

    /** Where the game's picture is in the whole, in its pixels; null while there is none. */
    fun picture(): Rect?

    /** Where the game's sound is now, for a card's audio; -1 where nothing records it. */
    fun audioPosition(): Long = -1L
}

/** The game's surface in Furoshiki's own game screen. */
class SurfaceFrames(private val surface: SurfaceView) : OcrFrames {
    override val width: Int get() = surface.width
    override val height: Int get() = surface.height

    override fun copy(maxWidth: Int, done: (Bitmap?) -> Unit) = ReaderHost.copyFrame(surface, maxWidth, done)

    /** The game's picture on the surface: the upper screen of a console with two. */
    override fun picture(): Rect? {
        val width = surface.width
        val height = surface.height
        if (width <= 0 || height <= 0) return null
        if (Console.TWO_SCREENS) {
            val rects = runCatching { GameNative.screenRects() }.getOrNull() ?: return null
            if (rects[2] <= 0 || rects[3] <= 0) return null
            return Rect(rects[0], rects[1], rects[0] + rects[2], rects[1] + rects[3])
        }
        val values = runCatching { ReaderNative.pictureRect(width, height) }.getOrNull()
        if (values == null || values.size < 6 || values[4] <= values[2] || values[5] <= values[3]) return Rect(0, 0, width, height)
        return Rect(values[2], values[3], values[4], values[5])
    }

    override fun audioPosition(): Long = runCatching { ReaderNative.audioPosition() }.getOrDefault(-1L)
}
