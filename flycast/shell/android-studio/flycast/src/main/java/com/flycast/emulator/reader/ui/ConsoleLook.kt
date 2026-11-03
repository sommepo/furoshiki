// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package com.flycast.emulator.reader.ui

import com.flycast.emulator.R
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
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * The console's look for the word popup, the question about a card's audio and the pointer:
 * the same as the desktop app's (its `reader_overlay.cpp`, "The console's look").
 * Here: plain white with a faint orange spiral in its corner, dark blue type, orange for the
 * plus and the book; the pointer is an orange spiral on a white disc. The spiral is drawn
 * from a formula: it is not the console maker's mark, though it is meant to call it to mind.
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
            paint.color = argb(255, 255, 255, 255)
            canvas.drawRect(area, paint)
            // the spiral, faint, half out of the bottom right corner
            canvas.save()
            canvas.clipRect(area)
            val radius = min(area.height() * 0.62f, 120f * density)
            paint.style = Paint.Style.STROKE
            paint.strokeCap = Paint.Cap.ROUND
            paint.strokeJoin = Paint.Join.ROUND
            paint.strokeWidth = radius * 0.26f
            paint.color = argb(34, 0xF2, 0x7C, 0x1E)
            canvas.drawPath(spiral(area.right - radius * 0.35f, area.bottom - radius * 0.25f, radius), paint)
            canvas.restore()
            paint.strokeCap = Paint.Cap.BUTT
            paint.strokeWidth = one
            paint.color = ReaderStyle.POPUP_EDGE
            canvas.drawRect(area.left + one / 2, area.top + one / 2, area.right - one / 2, area.bottom - one / 2, paint)
        }
    }

    /** The rule between two entries. */
    fun rule(density: Float): Drawable = object : Plain() {
        override fun getIntrinsicHeight() = 1

        override fun draw(canvas: Canvas) {
            val area = RectF(bounds)
            val one = max(1f, density)
            paint.style = Paint.Style.FILL
            paint.color = argb(255, 0xE3, 0xE9, 0xF5)
            canvas.drawRect(area, paint)
        }
    }

    /**
     * What lies behind an entry, [head] being its headword's line and [first] true for the
     * entry at the top; null where the look has nothing there.
     */
    /**
     * Dresses one of the reader's pages ([ReaderScreen]: its [root] holds the [toolbar] and the
     * page) in the console's look, where it has one; [back] leaves the page. Nothing here.
     */
    @Suppress("UNUSED_PARAMETER")
    fun dressScreen(activity: android.app.Activity, root: android.widget.LinearLayout,
                    toolbar: com.google.android.material.appbar.MaterialToolbar, back: () -> Unit) {
    }

    fun entry(density: Float, first: Boolean, head: View): Drawable? = null

    /** The pointer, [size] across, pointing at its own centre. */
    fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float) {
        val style = paint.style
        val width = paint.strokeWidth
        val cap = paint.strokeCap
        val join = paint.strokeJoin
        // an orange spiral on a white disc, with a thin dark rim so that it shows on a white picture
        val radius = size * 0.44f
        paint.style = Paint.Style.FILL
        paint.color = argb(255, 255, 255, 255)
        canvas.drawCircle(x, y, radius, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = max(1f, size * 0.03f)
        paint.color = argb(70, 0, 0, 0)
        canvas.drawCircle(x, y, radius, paint)
        paint.strokeCap = Paint.Cap.ROUND
        paint.strokeJoin = Paint.Join.ROUND
        paint.strokeWidth = radius * 0.22f
        paint.color = argb(255, 0xF2, 0x7C, 0x1E)
        canvas.drawPath(spiral(x, y, radius * 0.72f), paint)
        paint.shader = null
        paint.style = style
        paint.strokeWidth = width
        paint.strokeCap = cap
        paint.strokeJoin = join
    }

    /** A spiral of about one and three quarter turns around the centre, [radius] at its outer end. */
    private fun spiral(cx: Float, cy: Float, radius: Float): Path {
        val path = Path()
        val steps = 96
        for (i in 0..steps) {
            val t = i.toFloat() / steps
            val angle = t * 3.5 * Math.PI + 0.6
            val r = radius * (0.11f + 0.89f * t)
            val px = cx + (cos(angle) * r).toFloat()
            val py = cy + (sin(angle) * r).toFloat()
            if (i == 0) path.moveTo(px, py) else path.lineTo(px, py)
        }
        return path
    }

    /** The theme of the reader's own pages ([ReaderScreen]), set before they are made. */
    fun theme(activity: android.app.Activity) {
        activity.setTheme(R.style.Theme_Reader)
    }

}
