// SPDX-FileCopyrightText: Station Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader.ui

import com.github.stenzek.duckstation.R
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PixelFormat
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.drawable.Drawable
import android.view.View
import kotlin.math.max

/**
 * The console's look for the word popup, the question about a card's audio and the pointer:
 * the same as the desktop app's (reader/qt/reader_look.cpp, "The console's look"). Here: the
 * warm light grey of the console's case, a shade darker towards the bottom, with the thin
 * ridges of its lid along the top; the pointer is a disc as the console's discs are
 * underneath, black with a sheen of colour across it and a grey ring round the hole.
 */
@Suppress("unused")
object ConsoleLook {
    private fun argb(a: Int, r: Int, g: Int, b: Int) = (a shl 24) or (r shl 16) or (g shl 8) or b

    private abstract class Plain : Drawable() {
        protected val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        override fun setAlpha(alpha: Int) = Unit

        override fun setColorFilter(filter: ColorFilter?) = Unit

        @Deprecated("Deprecated in Java")
        override fun getOpacity() = PixelFormat.TRANSLUCENT
    }

    /** The popup's ground and its edge. */
    fun ground(density: Float): Drawable = object : Plain() {
        override fun draw(canvas: Canvas) {
            val area = RectF(bounds)
            val one = max(1f, density)
            paint.style = Paint.Style.FILL
            paint.shader = LinearGradient(0f, area.top, 0f, area.bottom,
                argb(255, 0xEE, 0xED, 0xE8), argb(255, 0xDC, 0xDB, 0xD5), Shader.TileMode.CLAMP)
            canvas.drawRect(area, paint)
            paint.shader = null
            // the lid's ridges
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = one
            paint.color = argb(255, 0xC3, 0xC2, 0xBC)
            for (i in 0 until 3) {
                val y = area.top + (3 + i * 3) * one
                canvas.drawLine(area.left + 8 * one, y, area.right - 8 * one, y, paint)
            }
            paint.color = ReaderStyle.POPUP_EDGE
            canvas.drawRect(area.left + one / 2, area.top + one / 2, area.right - one / 2, area.bottom - one / 2, paint)
        }
    }

    /** The rule between two entries. */
    fun rule(density: Float): Drawable = object : Plain() {
        override fun getIntrinsicHeight() = 1

        override fun draw(canvas: Canvas) {
            paint.style = Paint.Style.FILL
            paint.color = argb(255, 0xC8, 0xC7, 0xC1)
            canvas.drawRect(RectF(bounds), paint)
        }
    }

    /**
     * Dresses one of the reader's pages ([ReaderScreen]: its [root] holds the [toolbar] and the
     * page) in the console's look, where it has one; [back] leaves the page. Nothing here.
     */
    @Suppress("UNUSED_PARAMETER")
    fun dressScreen(activity: android.app.Activity, root: android.widget.LinearLayout,
                    toolbar: com.google.android.material.appbar.MaterialToolbar, back: () -> Unit) {
    }

    /**
     * What lies behind an entry, [head] being its headword's line and [first] true for the
     * entry at the top; null where the look has nothing there: the grey is the look.
     */
    fun entry(density: Float, first: Boolean, head: View): Drawable? = null

    /** The pointer, [size] across, pointing at its own centre. */
    fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float) {
        val style = paint.style
        val width = paint.strokeWidth
        val radius = size * 0.44f
        // the disc's black face, with a light rim so that it shows on a dark picture
        paint.style = Paint.Style.FILL
        paint.shader = RadialGradient(x, y, radius, argb(255, 0x3A, 0x3A, 0x44), argb(255, 0x0C, 0x0C, 0x10),
            Shader.TileMode.CLAMP)
        canvas.drawCircle(x, y, radius, paint)
        paint.shader = null
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = max(1f, size * 0.04f)
        paint.color = argb(255, 0xB8, 0xB8, 0xBE)
        canvas.drawCircle(x, y, radius, paint)
        // the sheen: a thin band of colour, as light catches the underside
        val band = Path().apply {
            moveTo(x, y)
            arcTo(RectF(x - radius, y - radius, x + radius, y + radius), -70f, 40f)
            close()
        }
        paint.style = Paint.Style.FILL
        paint.shader = LinearGradient(x + radius, y - radius, x, y,
            intArrayOf(argb(120, 140, 120, 255), argb(70, 120, 220, 255), argb(0, 255, 255, 255)),
            floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
        canvas.drawPath(band, paint)
        paint.shader = null
        // the clamp ring and the hole
        paint.color = argb(255, 0xC8, 0xC7, 0xC1)
        canvas.drawCircle(x, y, radius * 0.34f, paint)
        paint.color = argb(255, 0x23, 0x24, 0x2B)
        canvas.drawCircle(x, y, radius * 0.12f, paint)
        paint.style = style
        paint.strokeWidth = width
    }

    /** The theme of the reader's own pages ([ReaderScreen]), set before they are made. */
    fun theme(activity: android.app.Activity) {
        activity.setTheme(R.style.Theme_Reader)
    }

}
