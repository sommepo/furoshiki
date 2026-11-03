// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors (from the PPSSPP Reader fork)
// SPDX-License-Identifier: GPL-3.0+

package io.github.sommepo.furoshiki.reader.ui

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
 * Here: the PS2's system menu: black-indigo glass, a thin light-blue edge, the headword lit
 * from within; the pointer is one of the menu's lights.
 */
@Suppress("unused")
object Ps2Look : ConsoleLook.Look {
    private fun argb(a: Int, r: Int, g: Int, b: Int) = (a shl 24) or (r shl 16) or (g shl 8) or b

    private abstract class Plain : Drawable() {
        protected val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        override fun setAlpha(alpha: Int) = Unit

        override fun setColorFilter(filter: ColorFilter?) = Unit

        @Deprecated("Deprecated in Java")
        override fun getOpacity() = PixelFormat.TRANSLUCENT
    }

    /** The popup's ground and its edge. */
    override fun ground(density: Float): Drawable = object : Plain() {
        override fun draw(canvas: Canvas) {
            val area = RectF(bounds)
            val one = max(1f, density)
            paint.style = Paint.Style.FILL
            paint.shader = LinearGradient(
                area.left, area.top, area.right, area.bottom,
                argb(255, 8, 12, 34), argb(255, 2, 3, 12), Shader.TileMode.CLAMP
            )
            canvas.drawRect(area, paint)
            // the glass is lit a little from its upper edge
            paint.shader = LinearGradient(
                0f, area.top, 0f, area.top + area.height() * 0.45f,
                argb(44, 60, 110, 255), argb(0, 60, 110, 255), Shader.TileMode.CLAMP
            )
            canvas.drawRect(area, paint)
            paint.shader = null
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = one
            paint.color = ReaderStyle.POPUP_EDGE
            canvas.drawRect(area.left + one / 2, area.top + one / 2, area.right - one / 2, area.bottom - one / 2, paint)
        }
    }

    /** The rule between two entries. */
    override fun rule(density: Float): Drawable = object : Plain() {
        override fun getIntrinsicHeight() = 1

        override fun draw(canvas: Canvas) {
            val area = RectF(bounds)
            val one = max(1f, density)
            paint.style = Paint.Style.FILL
            paint.shader = LinearGradient(
                area.left, 0f, area.right, 0f,
                argb(180, 127, 196, 255), argb(0, 127, 196, 255), Shader.TileMode.CLAMP
            )
            canvas.drawRect(area, paint)
            paint.shader = null
        }
    }

    /**
     * What lies behind an entry, [head] being its headword's line and [first] true for the
     * entry at the top; null where the look has nothing there.
     */
    override fun entry(density: Float, first: Boolean, head: View): Drawable? = object : Plain() {
        override fun draw(canvas: Canvas) {
            // the headword, lit as the menu's chosen entry is
            val top = (head.parent as? View)?.let { it.top + head.top }?.toFloat() ?: head.top.toFloat()
            val height = head.height.toFloat()
            if (height <= 0f) return
            val left = bounds.left + head.left.toFloat()
            paint.style = Paint.Style.FILL
            paint.shader = RadialGradient(
                left + height * 1.2f, top + height / 2, height * 2.4f, argb(96, 61, 125, 255), argb(0, 61, 125, 255), Shader.TileMode.CLAMP
            )
            canvas.drawRect(
                max(bounds.left.toFloat(), left - 8 * density), max(bounds.top.toFloat(), top - 6 * density),
                bounds.right.toFloat(), min(bounds.bottom.toFloat(), top + height + 6 * density), paint
            )
            paint.shader = null
        }
    }

    /** The pointer, [size] across, pointing at its own centre. */
    override fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float) {
        val style = paint.style
        val width = paint.strokeWidth
        val cap = paint.strokeCap
        val join = paint.strokeJoin
        // one of the menu's lights: a white core in a blue halo
        paint.style = Paint.Style.FILL
        paint.shader = RadialGradient(
            x, y, size / 2,
            intArrayOf(argb(255, 255, 255, 255), argb(220, 159, 212, 255), argb(0, 42, 107, 255)),
            floatArrayOf(0f, 0.3f, 1f), Shader.TileMode.CLAMP
        )
        canvas.drawCircle(x, y, size / 2, paint)
        paint.shader = null
        paint.color = argb(255, 234, 246, 255)
        canvas.drawCircle(x, y, size * 0.09f, paint)
        paint.shader = null
        paint.style = style
        paint.strokeWidth = width
        paint.strokeCap = cap
        paint.strokeJoin = join
    }

}
