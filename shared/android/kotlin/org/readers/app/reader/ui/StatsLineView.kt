// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.TypedValue
import android.widget.TextView
import kotlin.math.max

/**
 * The session line in a corner of the game ("1,186字 · 6,910/h · 10m"), drawn in one of its
 * looks, as the desktop's reader_overlay.cpp draws it (the maintainer's proposals G, H, F, D):
 * "paper" (a cream slip, the default), "paper-dark", "card" (a small pink-edged card, a row
 * for each figure) or "ink" (no box). It stays a TextView for what it says (its text, the pause
 * mark as its compound drawable) and draws itself.
 */
@SuppressLint("AppCompatCustomView", "ViewConstructor")
class StatsLineView(context: Context) : TextView(context) {

    /** The look: ReaderSettings.statsStyle. */
    var style: String = "paper"
        set(value) {
            if (field != value) {
                field = value
                requestLayout()
                invalidate()
            }
        }

    /** The statistics are paused: two bars before the line. */
    var paused = false
        set(value) {
            if (field != value) {
                field = value
                requestLayout()
                invalidate()
            }
        }

    /**
     * TextView asks for a new layout only once it has made a text layout of its own, which
     * [onMeasure] here never lets it, so a new text would stay undrawn. Runs in TextView's
     * constructor too, before this class's fields exist.
     */
    override fun onTextChanged(text: CharSequence?, start: Int, lengthBefore: Int, lengthAfter: Int) {
        super.onTextChanged(text, start, lengthBefore, lengthAfter)
        requestLayout()
        invalidate()
    }

    private data class Figure(val number: String, val unit: String, val label: String)

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val ink = Paint(Paint.ANTI_ALIAS_FLAG)
    private val serif = Typeface.create(Typeface.SERIF, Typeface.BOLD)
    private val serifItalic = Typeface.create(Typeface.SERIF, Typeface.BOLD_ITALIC)
    private val sans = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    private val box = RectF()

    /** The size the line is set at, in pixels: that of 11sp, as before. */
    private val size get() = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 11f, resources.displayMetrics)

    private fun figures(): List<Figure> = text.toString().split(" · ").filter { it.isNotEmpty() }.map { part ->
        val at = part.indexOfLast { it.isDigit() } + 1
        val number = if (at > 0) part.substring(0, at) else part
        val unit = if (at > 0) part.substring(at) else ""
        val label = when {
            unit.contains('字') -> "chars"
            unit == "/h" -> "speed"
            else -> "time"
        }
        Figure(number, unit, label)
    }

    private fun Paint.set(face: Typeface, pixels: Float, colour: Int): Paint {
        typeface = face
        textSize = pixels
        color = colour
        clearShadowLayer()
        return this
    }

    // ---- The card: a heading with the pause sign, then a row for each figure ----

    private class CardLayout(val width: Float, val height: Float, val labelWidth: Float, val row: Float, val head: Float)

    private fun card(): CardLayout {
        val s = size
        val figures = figures()
        ink.set(serifItalic, s * 0.85f, 0)
        val labelWidth = figures.maxOfOrNull { ink.measureText(it.label) } ?: 0f
        val row = ink.fontSpacing * 0.92f
        ink.set(sans, s * 0.9f, 0)
        val valueWidth = figures.maxOfOrNull { ink.measureText(it.number + it.unit) } ?: 0f
        ink.set(sans, s * 0.55f, 0)
        ink.letterSpacing = 0.5f
        val headWidth = ink.measureText(HEADING) + s * 0.5f + (if (paused) s * 0.75f else 0f)
        val head = ink.fontSpacing
        ink.letterSpacing = 0f
        val inner = max(labelWidth + s * 0.7f + valueWidth, headWidth)
        return CardLayout(inner + s * 1.5f, s * 0.84f + head + row * figures.size, labelWidth, row, head)
    }

    // ---- A row: pause sign, each figure with its unit smaller, a separator between ----

    private fun numberPaint() = if (style == "ink") ink.set(sans, size * 1.05f, 0) else ink.set(serif, size, 0)
    private fun unitPaint() = if (style == "ink") ink.set(sans, size * 0.72f, 0) else ink.set(serif, size * 0.72f, 0)

    private fun rowWidth(): Float {
        val s = size
        val figures = figures()
        val sep = if (style == "ink") s * 1.1f else s
        val barsHeight = numberPaint().fontMetrics.let { -it.ascent } * 0.72f
        var width = if (paused) max(2f, barsHeight * 0.28f) * 2.6f + (if (figures.isEmpty()) 0f else s * 0.55f) else 0f
        figures.forEachIndexed { i, figure ->
            width += numberPaint().measureText(figure.number) + unitPaint().measureText(figure.unit) +
                (if (figure.unit.isEmpty()) 0f else s * 0.1f) + (if (i > 0) sep else 0f)
        }
        return width
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val s = size
        // Room for the slip's shadow around it.
        val shade = s * 0.4f
        if (style == "card") {
            val layout = card()
            setMeasuredDimension((layout.width + shade * 2).toInt() + 1, (layout.height + shade * 2).toInt() + 1)
            return
        }
        val pad = if (style == "ink") s * 0.2f else s * 0.7f
        val metrics = numberPaint().fontMetrics
        val height = metrics.descent - metrics.ascent + (if (style == "ink") s * 0.2f else s * 0.56f)
        setMeasuredDimension((rowWidth() + pad * 2 + shade * 2).toInt() + 1, (height + shade * 2).toInt() + 1)
    }

    private fun shadow(radius: Float, alpha: Int) {
        val s = size
        for (i in 3 downTo 1) {
            val spread = s * 0.12f * i
            fill.color = Color.argb(alpha / (i + 1), 0, 0, 0)
            canvas?.drawRoundRect(
                box.left - spread, box.top - spread + s * 0.18f, box.right + spread, box.bottom + spread + s * 0.18f,
                radius + spread, radius + spread, fill
            )
        }
    }

    private var canvas: Canvas? = null

    private fun bars(left: Float, baseline: Float, height: Float, colour: Int, halo: Boolean) {
        val bar = max(2f, height * 0.28f)
        fill.color = colour
        if (halo) fill.setShadowLayer(size * 0.15f, 0f, 1f, Color.argb(0xCC, 0, 0, 0)) else fill.clearShadowLayer()
        for (x in listOf(left, left + bar * 1.6f)) {
            canvas?.drawRoundRect(x, baseline - height, x + bar, baseline, bar * 0.35f, bar * 0.35f, fill)
        }
        fill.clearShadowLayer()
    }

    override fun onDraw(canvas: Canvas) {
        this.canvas = canvas
        val s = size
        val shade = s * 0.4f
        if (style == "card") drawCard(shade) else drawRow(shade)
        this.canvas = null
    }

    private fun drawCard(shade: Float) {
        val c = canvas ?: return
        val s = size
        val layout = card()
        box.set(width - shade - layout.width, shade, width - shade, shade + layout.height)
        val radius = s * 0.6f
        shadow(radius, 70)
        fill.color = Color.argb(0xDC, 0xF4, 0xF0, 0xFF)
        c.drawRoundRect(box, radius, radius, fill)
        stroke.color = Color.rgb(0xF3, 0x9A, 0xCB)
        stroke.strokeWidth = max(1f, s * 0.09f)
        c.drawRoundRect(box, radius, radius, stroke)
        val left = box.left + s * 0.75f
        val right = box.right - s * 0.75f
        var y = box.top + s * 0.42f
        ink.set(sans, s * 0.55f, Color.rgb(0x8A, 0x8C, 0xC0))
        ink.letterSpacing = 0.5f
        val ascent = -ink.fontMetrics.ascent
        c.drawText(HEADING, left, y + ascent, ink)
        ink.letterSpacing = 0f
        if (paused) bars(right - s * 0.5f, y + ascent, ascent, Color.rgb(0xE0, 0x4F, 0xA8), false)
        y += layout.head
        for (figure in figures()) {
            y += layout.row
            ink.set(serifItalic, s * 0.85f, Color.rgb(0x4B, 0x4F, 0x9A))
            c.drawText(figure.label, left, y - layout.row * 0.18f, ink)
            ink.set(sans, s * 0.9f, Color.rgb(0x1D, 0x2A, 0x6B))
            val value = figure.number + figure.unit
            c.drawText(value, right - ink.measureText(value), y - layout.row * 0.18f, ink)
        }
    }

    private fun drawRow(shade: Float) {
        val c = canvas ?: return
        val s = size
        val inkStyle = style == "ink"
        val dark = style == "paper-dark"
        val figures = figures()
        val width = rowWidth()
        val pad = if (inkStyle) s * 0.2f else s * 0.7f
        val padY = if (inkStyle) s * 0.1f else s * 0.28f
        val metrics = numberPaint().fontMetrics
        box.set(this.width - shade - width - pad * 2, shade, this.width - shade, shade + metrics.descent - metrics.ascent + padY * 2)
        val baseline = box.top + padY - metrics.ascent
        val text: Int
        val unit: Int
        val sepColour: Int
        val barsColour: Int
        when {
            inkStyle -> {
                text = Color.WHITE; unit = Color.argb(0xCC, 0xFF, 0xFF, 0xFF)
                sepColour = Color.rgb(0xB9, 0xB0, 0xF2); barsColour = Color.rgb(0x7F, 0xF0, 0xC8)
            }
            dark -> {
                shadow(s * 0.27f, 110)
                fill.color = Color.rgb(0x2B, 0x21, 0x19)
                c.drawRoundRect(box, s * 0.27f, s * 0.27f, fill)
                stroke.color = Color.rgb(0x5A, 0x46, 0x32); stroke.strokeWidth = 1f
                c.drawRoundRect(box, s * 0.27f, s * 0.27f, stroke)
                text = Color.rgb(0xEF, 0xE4, 0xCF); unit = Color.rgb(0xB8, 0xA8, 0x8A)
                sepColour = Color.rgb(0x6E, 0x58, 0x40); barsColour = Color.rgb(0xD9, 0xA5, 0x5A)
            }
            else -> {
                shadow(s * 0.27f, 80)
                fill.color = Color.rgb(0xFB, 0xF8, 0xF1)
                c.drawRoundRect(box, s * 0.27f, s * 0.27f, fill)
                stroke.color = Color.rgb(0xD9, 0xD0, 0xBD); stroke.strokeWidth = 1f
                c.drawRoundRect(box, s * 0.27f, s * 0.27f, stroke)
                text = Color.rgb(0x2A, 0x26, 0x20); unit = Color.rgb(0x7A, 0x6E, 0x5A)
                sepColour = Color.rgb(0xC9, 0xBD, 0xA4); barsColour = Color.rgb(0xB0, 0x78, 0x2A)
            }
        }
        // Ink holds on any picture by a soft dark halo round its letters, not a box.
        fun draw(what: String, x: Float, paint: Paint, colour: Int) {
            paint.color = colour
            if (inkStyle) paint.setShadowLayer(s * 0.25f, 0f, s * 0.06f, Color.argb(0xE0, 0, 0, 0))
            c.drawText(what, x, baseline, paint)
            paint.clearShadowLayer()
        }
        var x = box.left + pad
        if (paused) {
            val barsHeight = -metrics.ascent * 0.72f
            bars(x, baseline, barsHeight, barsColour, inkStyle)
            x += max(2f, barsHeight * 0.28f) * 2.6f + (if (figures.isEmpty()) 0f else s * 0.55f)
        }
        val sep = if (inkStyle) s * 1.1f else s
        figures.forEachIndexed { i, figure ->
            if (i > 0) {
                val middle = x + sep / 2
                if (inkStyle) {
                    val dot = max(1.5f, s * 0.12f)
                    fill.color = sepColour
                    fill.setShadowLayer(s * 0.15f, 0f, 1f, Color.argb(0xCC, 0, 0, 0))
                    c.drawCircle(middle, baseline + metrics.ascent * 0.32f, dot, fill)
                    fill.clearShadowLayer()
                } else {
                    val paint = numberPaint()
                    draw("·", middle - paint.measureText("·") / 2, paint, sepColour)
                }
                x += sep
            }
            val number = numberPaint()
            draw(figure.number, x, number, text)
            x += number.measureText(figure.number)
            if (figure.unit.isNotEmpty()) {
                x += s * 0.1f
                val small = unitPaint()
                draw(figure.unit, x, small, unit)
                x += small.measureText(figure.unit)
            }
        }
    }

    private companion object {
        const val HEADING = "READING"
    }
}
