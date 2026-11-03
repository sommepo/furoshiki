// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import androidx.preference.PreferenceManager
import com.github.stenzek.duckstation.reader.ReaderCatalog
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * The app's home in the manner of the PlayStation's own system screen, the Android twin of the
 * desktop app's (reader/ui/ps_home.cpp, the same layout, looks and words): grey bevelled
 * buttons across the top for four pages (Games, Memory Card, Reader, Settings), the time in a
 * dark window as the CD player shows a track's, the game list as a column of blocks with the
 * chosen one framed in a pulsing light and large beside it, each game a script reads marked
 * with a book, and the console's four button marks at the foot. A short start-up screen comes
 * first. Everything is drawn from lines, rectangles and circles; nothing of the console's is used.
 *
 * It lies over DuckStation's game list (MainActivity), which does what it asks ([Host]). The
 * picture is laid out on the console's own 640x480 sheet and scaled to the screen. A touch on a
 * tab or a row chooses it, a touch on what is chosen opens it, a drag moves a list; a
 * controller's d-pad, its shoulder buttons for the pages, A or B to open.
 *
 * Its switches are kept where DuckStation keeps its settings, under the desktop app's names:
 * Reader/PlayStationHome, Reader/PlayStationStyle (the look, 0 to 2), Reader/ReadableOnly.
 */
@SuppressLint("ViewConstructor")
class PsHome(context: Context, private val host: Host) : View(context) {

    /** What the game list's activity does for the home. */
    interface Host {
        fun games(): List<Game>
        /** Whether the game folders have been looked through once. */
        fun gamesReady(): Boolean
        /**
         * One of: start (with the game's path), file, console, resume, settings, controllers,
         * folders, refresh, bios, cards, classic.
         */
        fun perform(action: String, path: String?)
    }

    class Game(
        val path: String,
        val code: String?,
        val title: String?,
        val region: String?,
        val size: Long,
        val coverPath: String?,
    )

    private class Look(
        val name: String, val about: String, val light: Boolean,
        val groundTop: Int, val groundBottom: Int,
        val panel: Int, val panelHi: Int, val panelLo: Int,
        val button: Int, val buttonHi: Int, val buttonLo: Int,
        val text: Int, val textSoft: Int, val buttonText: Int, val shadow: Int,
        val select: Int, val selectFill: Int, val accent: Int,
        val inset: Int, val insetText: Int,
    )

    private class Row(val name: String, val value: String, val about: String, val act: (() -> Unit)?)

    private class Hit(val area: RectF, val action: () -> Unit)

    private enum class Mark { CIRCLE, CROSS, TRIANGLE, SQUARE }

    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val type = Paint(Paint.ANTI_ALIAS_FLAG or Paint.SUBPIXEL_TEXT_FLAG or Paint.LINEAR_TEXT_FLAG).apply {
        typeface = Typeface.create("sans-serif", Typeface.NORMAL)
    }
    private val path = Path()
    private val rect = RectF()

    // ---- The sheet: where the 640x480 picture is on the screen ----
    private var scale = 1f
    private var originX = 0f
    private var originY = 0f
    private var sheetLeft = 0f
    private var sheetRight = 640f

    // ---- State ----
    private var page = 0
    private var pageSlide = 0f
    private var game = 0
    private var gameSlide = 0f
    private val rowChoice = IntArray(PAGE_COUNT)
    private val rowSlide = FloatArray(PAGE_COUNT)
    private var booting = true
    private val startedAt = now()
    private var shownAt = startedAt
    private var lastFrame = startedAt
    private var frameSeconds = 0f
    private var notice = ""
    private var noticeAt = 0.0
    private var games: List<Game> = emptyList()
    private val hits = ArrayList<Hit>()
    private val covers = HashMap<String, Bitmap?>()

    // ---- Touch ----
    private val slop = ViewConfiguration.get(context).scaledTouchSlop
    private var downX = 0f
    private var downY = 0f
    private var dragging = false
    private var dragRows = 0f

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        setBackgroundColor(0xFF000000.toInt())
        gamesChanged()
    }

    /** The game list changed (it was looked through again). */
    fun gamesChanged() {
        games = host.games().sortedBy { (it.title ?: it.path).lowercase() }
        invalidate()
    }

    private val style: Int get() = preferences.getInt(STYLE_KEY, 0).coerceIn(0, LOOKS.size - 1)
    private val look: Look get() = LOOKS[style]
    private val readableOnly: Boolean get() = preferences.getBoolean(READABLE_ONLY_KEY, false)

    private fun now(): Double = SystemClock.uptimeMillis() / 1000.0

    private fun readable(g: Game): Boolean = !g.code.isNullOrEmpty() && ReaderCatalog.covers(context, g.code)

    private fun shown(): List<Game> = if (readableOnly) games.filter(::readable) else games

    private fun approach(value: Float, target: Float, seconds: Float): Float {
        val k = 1f - exp(-frameSeconds * 4f / max(seconds, 0.01f))
        return value + (target - value) * k
    }

    private fun notice(text: String) {
        notice = text
        noticeAt = now()
    }

    private fun goPage(to: Int) {
        val p = (to + PAGE_COUNT) % PAGE_COUNT
        if (p != page) {
            page = p
            shownAt = now()
        }
    }

    // ---- Drawing helpers, in the sheet's units ----

    private fun text(x: Float, y: Float, size: Float, colour: Int, s: String, canvas: Canvas, shadow: Boolean = false) {
        type.textSize = size
        val baseline = y + size * 0.82f
        if (shadow) {
            type.color = fade(look.shadow, (colour ushr 24) / 255f)
            canvas.drawText(s, x + 1f, baseline + 1f, type)
        }
        type.color = colour
        canvas.drawText(s, x, baseline, type)
    }

    private fun width(s: String, size: Float): Float {
        type.textSize = size
        return type.measureText(s)
    }

    private fun textCentred(cx: Float, y: Float, size: Float, colour: Int, s: String, c: Canvas, shadow: Boolean = false) =
        text(cx - width(s, size) / 2, y, size, colour, s, c, shadow)

    private fun textRight(right: Float, y: Float, size: Float, colour: Int, s: String, c: Canvas, shadow: Boolean = false) =
        text(right - width(s, size), y, size, colour, s, c, shadow)

    /** `s` cut to fit `w`, with an ellipsis when cut. */
    private fun fit(s: String, size: Float, w: Float): String {
        if (width(s, size) <= w) return s
        var cut = s
        while (cut.length > 1 && width("$cut...", size) > w) cut = cut.dropLast(1)
        return "$cut..."
    }

    /** `s` in lines no wider than `w`: broken at spaces where there are any, else between characters. */
    private fun wrap(s: String, size: Float, w: Float): List<String> {
        val lines = ArrayList<String>()
        var line = ""
        for (ch in s) {
            if (width(line + ch, size) > w && line.isNotEmpty()) {
                val space = line.lastIndexOf(' ')
                if (space > 0 && ch != ' ') {
                    lines += line.substring(0, space)
                    line = line.substring(space + 1)
                } else {
                    lines += line
                    line = ""
                }
                if (ch == ' ') continue
            }
            line += ch
        }
        if (line.isNotEmpty()) lines += line
        return lines
    }

    private fun box(x: Float, y: Float, w: Float, h: Float, colour: Int, c: Canvas, round: Float = 0f) {
        fill.color = colour
        fill.shader = null
        rect.set(x, y, x + w, y + h)
        if (round > 0) c.drawRoundRect(rect, round, round, fill) else c.drawRect(rect, fill)
    }

    private fun frame(x: Float, y: Float, w: Float, h: Float, colour: Int, round: Float, thick: Float, c: Canvas) {
        stroke.color = colour
        stroke.strokeWidth = thick
        rect.set(x, y, x + w, y + h)
        c.drawRoundRect(rect, round, round, stroke)
    }

    private fun line(x0: Float, y0: Float, x1: Float, y1: Float, colour: Int, thick: Float, c: Canvas) {
        stroke.color = colour
        stroke.strokeWidth = thick
        c.drawLine(x0, y0, x1, y1, stroke)
    }

    private fun disc(x: Float, y: Float, r: Float, colour: Int, c: Canvas) {
        fill.color = colour
        fill.shader = null
        c.drawCircle(x, y, r, fill)
    }

    private fun ring(x: Float, y: Float, r: Float, colour: Int, thick: Float, c: Canvas) {
        stroke.color = colour
        stroke.strokeWidth = thick
        c.drawCircle(x, y, r, stroke)
    }

    /** A raised (or pressed) bevelled rectangle, as the console's menu draws its buttons. */
    private fun bevel(x: Float, y: Float, w: Float, h: Float, fillColour: Int, hi: Int, lo: Int, c: Canvas,
                      raised: Boolean = true, edge: Float = 1.6f) {
        box(x, y, w, h, fillColour, c, 2f)
        val top = if (raised) hi else lo
        val bottom = if (raised) lo else hi
        val e = edge / 2
        stroke.strokeCap = Paint.Cap.BUTT
        line(x + 1, y + e, x + w - 1, y + e, top, edge, c)
        line(x + e, y + 1, x + e, y + h - 1, top, edge, c)
        line(x + 1, y + h - e, x + w - 1, y + h - e, bottom, edge, c)
        line(x + w - e, y + 1, x + w - e, y + h - 1, bottom, edge, c)
        stroke.strokeCap = Paint.Cap.ROUND
    }

    private fun hit(x: Float, y: Float, w: Float, h: Float, action: () -> Unit) {
        hits += Hit(RectF(x, y, x + w, y + h), action)
    }

    private val pulse: Float get() = 0.55f + 0.45f * sin(now().toFloat() * 4f)

    // ---- The ground ----

    private fun drawGround(c: Canvas, t: Float) {
        val l = look
        fill.shader = LinearGradient(0f, 0f, 0f, height.toFloat(), l.groundTop, l.groundBottom, Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), fill)
        fill.shader = null
        c.save()
        c.translate(originX, originY)
        c.scale(scale, scale)
        when (style) {
            0 -> {
                // Slow bands of coloured light drifting across the dark, each with a fine bright edge.
                val bands = intArrayOf(rgb(0x50, 0x60, 0xFF), rgb(0x40, 0xC8, 0xC0), rgb(0xB0, 0x60, 0xFF))
                for (b in 0 until 3) {
                    val base = 150f + b * 95f
                    val amp = 34f + b * 10f
                    val freq = 0.010f + b * 0.003f
                    val speed = 0.22f + b * 0.07f
                    path.reset()
                    var x = sheetLeft - 20
                    var first = true
                    while (x <= sheetRight + 20) {
                        val y = base + amp * sin(x * freq + t * speed + b * 1.9f) + 10f * sin(x * freq * 2.7f - t * speed * 1.3f)
                        if (first) path.moveTo(x, y) else path.lineTo(x, y)
                        first = false
                        x += 12
                    }
                    for ((w, a) in listOf(46f to 0.07f, 16f to 0.09f)) {
                        stroke.color = fade(bands[b], a)
                        stroke.strokeWidth = w
                        c.drawPath(path, stroke)
                    }
                    stroke.color = fade(0xFFFFFFFF.toInt(), 0.16f)
                    stroke.strokeWidth = 1.1f
                    c.drawPath(path, stroke)
                }
            }
            1 -> {
                // Rows of faint blocks, as a memory card's fifteen are laid out, and a slow sweep of light.
                val cell = 32f
                val sweep = ((t * 40f) % 1200f) - 300f
                var y = 0f
                while (y < 480) {
                    var x = floor(sheetLeft / cell) * cell
                    while (x < sheetRight) {
                        val glow = max(0f, 1f - abs((x + y * 0.6f) - sweep) / 160f)
                        box(x + 4, y + 4, cell - 8, cell - 8, fade(rgb(0x80, 0xA0, 0xFF), 0.05f + 0.10f * glow), c, 3f)
                        x += cell
                    }
                    y += cell
                }
            }
            else -> {
                // The grey case: the fine ridges of its lid along the top, and a groove along the foot.
                for (i in 0 until 4) line(sheetLeft, 6f + i * 3f, sheetRight, 6f + i * 3f, argb(0x90, 0xB8, 0xB7, 0xB1), 1f, c)
                line(sheetLeft, 434f, sheetRight, 434f, argb(0xA0, 0xA9, 0xA8, 0xA2), 1.2f, c)
                line(sheetLeft, 435.5f, sheetRight, 435.5f, argb(0xA0, 0xFF, 0xFF, 0xFF), 1.2f, c)
            }
        }
        c.restore()
    }

    // ---- Pictures ----

    /** A disc seen from below: black, a sheen of colour, a grey clamp ring (the reader's pointer is the same disc). */
    private fun drawDisc(cx: Float, cy: Float, r: Float, alpha: Float, c: Canvas) {
        disc(cx, cy, r, fade(rgb(0x0C, 0x0C, 0x10), alpha), c)
        disc(cx, cy, r * 0.92f, fade(rgb(0x26, 0x26, 0x30), alpha), c)
        val colours = intArrayOf(argb(110, 140, 120, 255), argb(90, 120, 220, 255), argb(60, 255, 255, 255))
        stroke.strokeCap = Paint.Cap.BUTT
        for (i in 0 until 3) {
            val rr = r * (0.52f + i * 0.13f)
            val a0 = -1.1f + i * 0.05f
            stroke.color = fade(colours[i], alpha)
            stroke.strokeWidth = r * 0.11f
            rect.set(cx - rr, cy - rr, cx + rr, cy + rr)
            c.drawArc(rect, Math.toDegrees(a0.toDouble()).toFloat(), Math.toDegrees(0.9).toFloat(), false, stroke)
        }
        stroke.strokeCap = Paint.Cap.ROUND
        ring(cx, cy, r, fade(rgb(0xB8, 0xB8, 0xBE), alpha), max(0.5f, r * 0.04f), c)
        disc(cx, cy, r * 0.34f, fade(rgb(0xC8, 0xC7, 0xC1), alpha), c)
        disc(cx, cy, r * 0.12f, fade(rgb(0x23, 0x24, 0x2B), alpha), c)
    }

    /** An open book, small: the mark on a game whose text can be read. */
    private fun drawBook(cx: Float, cy: Float, r: Float, page: Int, lineColour: Int, c: Canvas) {
        for (side in intArrayOf(-1, 1)) {
            val x0 = if (side < 0) cx - r else cx + r * 0.06f
            val lift = r * 0.14f
            path.reset()
            path.moveTo(x0, cy - r * 0.62f - (if (side < 0) 0f else lift))
            path.lineTo(x0 + r * 0.94f, cy - r * 0.62f - (if (side < 0) lift else 0f))
            path.lineTo(x0 + r * 0.94f, cy + r * 0.62f - (if (side < 0) lift else 0f))
            path.lineTo(x0, cy + r * 0.62f - (if (side < 0) 0f else lift))
            path.close()
            fill.color = page
            c.drawPath(path, fill)
            for (i in 0 until 3) {
                box(x0 + r * 0.16f, cy - r * 0.30f + i * r * 0.28f, r * 0.62f, max(0.6f, r * 0.08f), lineColour, c)
            }
        }
    }

    private fun drawReadableMark(x: Float, y: Float, size: Float, alpha: Float, c: Canvas) {
        val l = look
        box(x, y, size, size, fade(l.accent, alpha), c, size * 0.18f)
        frame(x, y, size, size, fade(0xFFFFFFFF.toInt(), alpha * 0.5f), size * 0.18f, 1f, c)
        drawBook(x + size / 2, y + size * 0.56f, size * 0.36f, fade(0xFFFFFFFF.toInt(), alpha), fade(l.accent, alpha), c)
    }

    /** A memory card, upright: its body, a label, the notch in one corner, its contacts. */
    private fun drawCard(x: Float, y: Float, w: Float, h: Float, alpha: Float, number: Int, c: Canvas) {
        val l = look
        val notch = w * 0.18f
        path.reset()
        path.moveTo(x, y)
        path.lineTo(x + w - notch, y)
        path.lineTo(x + w, y + notch)
        path.lineTo(x + w, y + h)
        path.lineTo(x, y + h)
        path.close()
        fill.color = fade(if (l.light) rgb(0x8E, 0x8F, 0x98) else rgb(0x9A, 0x9C, 0xA8), alpha)
        c.drawPath(path, fill)
        stroke.color = fade(if (l.light) rgb(0x5A, 0x5B, 0x63) else rgb(0xE0, 0xE1, 0xEA), alpha)
        stroke.strokeWidth = 1.2f
        c.drawPath(path, stroke)
        for (i in 0 until 5) {
            line(x + w * 0.14f, y + h * 0.10f + i * 3f, x + w * 0.60f, y + h * 0.10f + i * 3f, fade(rgb(0x5A, 0x5B, 0x66), alpha * 0.7f), 1f, c)
        }
        box(x + w * 0.12f, y + h * 0.36f, w * 0.76f, h * 0.48f, fade(rgb(0xEE, 0xED, 0xE8), alpha), c, 2f)
        textCentred(x + w / 2, y + h * 0.44f, h * 0.24f, fade(rgb(0x23, 0x24, 0x2B), alpha), number.toString(), c)
        for (i in 0 until 6) box(x + w * 0.14f + i * w * 0.12f, y + h - 7, w * 0.07f, 5f, fade(rgb(0xD8, 0xB8, 0x60), alpha), c, 1f)
    }

    /** The console's button marks, drawn as they are printed. */
    private fun drawMark(cx: Float, cy: Float, r: Float, mark: Mark, c: Canvas) {
        val l = look
        disc(cx, cy, r, if (l.light) rgb(0x3A, 0x3B, 0x44) else rgb(0x1C, 0x1C, 0x24), c)
        ring(cx, cy, r, if (l.light) rgb(0x20, 0x21, 0x28) else rgb(0x6A, 0x6B, 0x78), 1f, c)
        val k = r * 0.52f
        val t = max(1.2f, r * 0.17f)
        when (mark) {
            Mark.CIRCLE -> ring(cx, cy, k, MARK_CIRCLE, t, c)
            Mark.CROSS -> {
                line(cx - k, cy - k, cx + k, cy + k, MARK_CROSS, t, c)
                line(cx - k, cy + k, cx + k, cy - k, MARK_CROSS, t, c)
            }
            Mark.TRIANGLE -> {
                path.reset()
                path.moveTo(cx, cy - k)
                path.lineTo(cx + k * 1.05f, cy + k * 0.75f)
                path.lineTo(cx - k * 1.05f, cy + k * 0.75f)
                path.close()
                stroke.color = MARK_TRIANGLE
                stroke.strokeWidth = t
                c.drawPath(path, stroke)
            }
            Mark.SQUARE -> frame(cx - k * 0.8f, cy - k * 0.8f, k * 1.6f, k * 1.6f, MARK_SQUARE, 0f, t, c)
        }
    }

    private fun cover(g: Game): Bitmap? {
        val file = g.coverPath?.takeIf { it.isNotEmpty() } ?: return null
        return covers.getOrPut(file) {
            try {
                val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(file, bounds)
                var sample = 1
                while (max(bounds.outWidth, bounds.outHeight) / sample > 384) sample *= 2
                BitmapFactory.decodeFile(file, BitmapFactory.Options().apply { inSampleSize = sample })
            } catch (_: Throwable) {
                null
            }
        }
    }

    private fun drawCover(x: Float, y: Float, size: Float, alpha: Float, g: Game, c: Canvas) {
        val bitmap = cover(g)
        if (bitmap != null) {
            box(x - 1, y - 1, size + 2, size + 2, fade(argb(0x80, 0, 0, 0), alpha), c)
            fill.alpha = (255 * alpha).toInt()
            rect.set(x, y, x + size, y + size)
            c.drawBitmap(bitmap, null, rect, fill)
            fill.alpha = 255
        } else {
            drawDisc(x + size / 2, y + size / 2, size * 0.48f, alpha, c)
        }
    }

    // ---- Parts every page has ----

    private fun drawHeader(c: Canvas) {
        val l = look
        text(max(sheetLeft + 20, 20f), 15f, 15f, l.textSoft, "Station Reader", c, !l.light)
        val date = Date()
        val second = (System.currentTimeMillis() / 1000) % 2 == 0L
        val clock = SimpleDateFormat(if (second) "HH:mm" else "HH mm", Locale.ROOT).format(date)
        val day = SimpleDateFormat("yyyy.MM.dd", Locale.ROOT).format(date)
        val right = min(sheetRight - 20, 620f)
        val w = 70f
        bevel(right - w, 10f, w, 24f, l.inset, l.buttonHi, l.buttonLo, c, raised = false, edge = 1.4f)
        textCentred(right - w / 2, 13f, 17f, l.insetText, clock, c)
        textRight(right - w - 10, 16f, 13f, l.textSoft, day, c, !l.light)
    }

    /** The grey buttons across the top: one per page, the chosen one pressed in. */
    private fun drawTabs(c: Canvas) {
        val l = look
        val w = 132f
        val h = 30f
        val gap = 10f
        val y = 44f
        val x0 = 320 - (PAGE_COUNT * w + (PAGE_COUNT - 1) * gap) / 2
        pageSlide = approach(pageSlide, page.toFloat(), 0.10f)
        for (i in 0 until PAGE_COUNT) {
            val x = x0 + i * (w + gap)
            val chosen = i == page
            bevel(x, y, w, h, if (chosen) l.buttonLo else l.button, l.buttonHi, l.buttonLo, c, raised = !chosen)
            if (chosen) box(x + 2, y + 2, w - 4, h - 4, l.selectFill, c, 1f)
            val colour = if (chosen) (if (l.light) 0xFFFFFFFF.toInt() else l.text) else l.buttonText
            textCentred(x + w / 2 + (if (chosen) 1f else 0f), y + 6.5f + (if (chosen) 1f else 0f), 17f, colour, PAGE_NAMES[i], c, !l.light || chosen)
            hit(x, y, w, h) { goPage(i) }
        }
        // The light under the chosen one slides along as the choice changes.
        val lx = x0 + pageSlide * (w + gap)
        box(lx + 10, y + h + 4, w - 20, 2.5f, fade(l.select, pulse), c, 1f)
    }

    private fun drawFooter(c: Canvas, enter: String?) {
        val l = look
        var x = max(sheetLeft + 24, 24f)
        val y = 452f
        if (enter != null) {
            drawMark(x + 9, y + 1, 9f, Mark.CIRCLE, c)
            text(x + 23, y - 7, 14f, l.text, enter, c, !l.light)
            x += 23 + width(enter, 14f) + 20
        }
        // The pages go by left and right; a touch on an arrow turns them too.
        val ax = x + 4
        fill.color = l.textSoft
        path.reset()
        path.moveTo(ax, y + 1); path.lineTo(ax + 7, y - 5); path.lineTo(ax + 7, y + 7); path.close()
        c.drawPath(path, fill)
        path.reset()
        path.moveTo(ax + 18, y + 1); path.lineTo(ax + 11, y - 5); path.lineTo(ax + 11, y + 7); path.close()
        c.drawPath(path, fill)
        hit(ax - 6, y - 14, 15f, 30f) { goPage(page - 1) }
        hit(ax + 9, y - 14, 15f, 30f) { goPage(page + 1) }
        text(ax + 26, y - 7, 14f, l.text, "Pages", c, !l.light)
        val rx = min(sheetRight - 24, 616f)
        drawMark(rx - 9, y + 1, 7f, Mark.SQUARE, c)
        drawMark(rx - 27, y + 1, 7f, Mark.TRIANGLE, c)
    }

    private fun drawPanel(x: Float, y: Float, w: Float, h: Float, c: Canvas) {
        val l = look
        box(x, y, w, h, l.panel, c, 3f)
        stroke.strokeCap = Paint.Cap.BUTT
        line(x + 1, y + 0.6f, x + w - 1, y + 0.6f, l.panelHi, 1.2f, c)
        line(x + 0.6f, y + 1, x + 0.6f, y + h - 1, l.panelHi, 1.2f, c)
        line(x + 1, y + h - 0.6f, x + w - 1, y + h - 0.6f, l.panelLo, 1.2f, c)
        line(x + w - 0.6f, y + 1, x + w - 0.6f, y + h - 1, l.panelLo, 1.2f, c)
        stroke.strokeCap = Paint.Cap.ROUND
    }

    /** The frame round what is chosen: a light that pulses, as the console's menus have it. */
    private fun drawChosen(x: Float, y: Float, w: Float, h: Float, c: Canvas) {
        val l = look
        box(x, y, w, h, l.selectFill, c, 2f)
        frame(x, y, w, h, fade(l.select, 0.45f + 0.55f * pulse), 2f, 1.8f, c)
    }

    // ---- Games: the main page ----

    private fun drawGames(c: Canvas, appear: Float) {
        val l = look
        val list = shown()
        val rise = (1 - appear) * 16
        val lx = 24f
        val ly = 86f + rise
        val lw = 372f
        val lh = 340f
        val dx = 408f
        val dw = 208f
        drawPanel(lx, ly, lw, lh, c)
        drawPanel(dx, ly, dw, lh, c)
        val count = list.size
        if (count == 0) {
            val ready = host.gamesReady()
            val first = when {
                !ready -> "Looking through the game folders..."
                readableOnly && games.isNotEmpty() -> "No game here whose text can be read"
                else -> "No games yet"
            }
            val second = when {
                !ready -> ""
                readableOnly && games.isNotEmpty() -> "Settings: Only Games That Can Be Read, off shows them all"
                else -> "Settings, Game Folders: add the folder your discs are in"
            }
            textCentred(lx + lw / 2, ly + 130, 19f, l.text, first, c, !l.light)
            textCentred(lx + lw / 2, ly + 162, 13f, l.textSoft, second, c, !l.light)
            drawDisc(dx + dw / 2, ly + 110, 56f, 0.6f, c)
            textCentred(dx + dw / 2, ly + 190, 13f, l.textSoft, "Settings has a disc image", c, !l.light)
            textCentred(dx + dw / 2, ly + 208, 13f, l.textSoft, "and the console without one", c, !l.light)
            hit(lx, ly, lw, lh) { goPage(3) }
            drawFooter(c, null)
            return
        }
        game = game.coerceIn(0, count - 1)
        gameSlide = approach(gameSlide, game.toFloat(), 0.08f)
        val rowH = 48f
        val visible = 7
        val first = gameSlide.minus(3f).coerceIn(0f, max(0f, (count - visible).toFloat()))
        listArea.set(lx, ly, lx + lw, ly + lh)
        listRowHeight = rowH
        c.save()
        c.clipRect(lx + 2, ly + 2, lx + lw - 2, ly + lh - 2)
        for (i in 0 until count) {
            val y = ly + 2 + (i - first) * rowH
            if (y < ly - rowH || y > ly + lh) continue
            val g = list[i]
            val chosen = i == game
            if (chosen) {
                drawChosen(lx + 5, y + 2, lw - 16, rowH - 4, c)
            } else if (i > 0) {
                line(lx + 12, y, lx + lw - 18, y, fade(l.panelLo, 0.5f), 1f, c)
            }
            bevel(lx + 10, y + 5, 38f, 38f, if (l.light) rgb(0xD8, 0xD7, 0xD1) else rgb(0x24, 0x24, 0x30), l.panelLo, l.panelHi, c, edge = 1f)
            drawCover(lx + 12, y + 7, 34f, 1f, g, c)
            val canRead = readable(g)
            val textW = lw - 80 - (if (canRead) 34 else 0)
            text(lx + 58, y + 6, 17f, l.text, fit(g.title ?: g.path, 17f, textW), c, !l.light)
            val detail = listOfNotNull(g.code?.takeIf { it.isNotEmpty() }, g.region).joinToString("   ")
            text(lx + 58, y + 27, 12f, l.textSoft, fit(detail, 12f, textW), c, !l.light)
            if (canRead) drawReadableMark(lx + lw - 48, y + 11, 26f, 1f, c)
            val top = max(y, ly)
            val bottom = min(y + rowH, ly + lh)
            if (bottom > top) {
                hit(lx, top, lw, bottom - top) {
                    if (game == i) start(g) else game = i
                }
            }
        }
        c.restore()
        if (count > visible) {
            val track = lh - 16
            val bar = max(20f, track * visible / count)
            val at = (track - bar) * first / max(1f, (count - visible).toFloat())
            box(lx + lw - 7, ly + 8, 3f, track, fade(l.panelLo, 0.6f), c, 1f)
            box(lx + lw - 7, ly + 8 + at, 3f, bar, fade(l.select, 0.8f), c, 1f)
        }

        // The chosen game, large.
        val chosen = list[game]
        val cs = 112f
        val cx = dx + (dw - cs) / 2
        bevel(cx - 4, ly + 14, cs + 8, cs + 8, if (l.light) rgb(0xD8, 0xD7, 0xD1) else rgb(0x18, 0x18, 0x22), l.panelLo, l.panelHi, c, edge = 1.4f)
        drawCover(cx, ly + 18, cs, 1f, chosen, c)
        var ty = ly + 146
        val lines = wrap(chosen.title ?: chosen.path, 17f, dw - 20)
        for (i in 0 until min(3, lines.size)) {
            val s = if (i == 2 && lines.size > 3) fit(lines[i] + " ...", 17f, dw - 20) else lines[i]
            textCentred(dx + dw / 2, ty, 17f, l.text, s, c, !l.light)
            ty += 21
        }
        ty += 4
        if (!chosen.code.isNullOrEmpty()) {
            textCentred(dx + dw / 2, ty, 13f, l.textSoft, listOfNotNull(chosen.code, chosen.region).joinToString("   "), c, !l.light)
            ty += 18
        }
        if (chosen.size > 0) {
            textCentred(dx + dw / 2, ty, 13f, l.textSoft, String.format(Locale.ROOT, "%.0f MB", chosen.size / 1048576.0), c, !l.light)
            ty += 18
        }
        val canRead = readable(chosen)
        val what = if (canRead) "Its text can be read" else "No game script for it yet"
        val tw = width(what, 13f) + (if (canRead) 24 else 0)
        var mx = dx + (dw - tw) / 2
        if (canRead) {
            drawReadableMark(mx, ty + 10, 18f, 1f, c)
            mx += 24
        }
        text(mx, ty + 12, 13f, if (canRead) l.text else l.textSoft, what, c, !l.light)
        // The button that starts it.
        val bw = 120f
        val bh = 28f
        val bx = dx + (dw - bw) / 2
        val by = ly + lh - 44
        bevel(bx, by, bw, bh, l.button, l.buttonHi, l.buttonLo, c)
        drawMark(bx + 22, by + bh / 2, 8f, Mark.CIRCLE, c)
        text(bx + 38, by + 5.5f, 16f, l.buttonText, "Play", c, !l.light)
        hit(bx, by, bw, bh) { start(chosen) }
        hit(cx - 4, ly + 14, cs + 8, cs + 8) { start(chosen) }
        textRight(min(sheetRight - 24, 616f) - 46, 445f, 14f, l.textSoft, "${game + 1} / $count", c, !l.light)
        drawFooter(c, "Play")
    }

    private fun start(g: Game) = host.perform("start", g.path)

    // ---- Pages that are lists: what the chosen row does on the left, the rows on the right ----

    private fun drawList(c: Canvas, appear: Float, rows: List<Row>, picture: (Float, Float, Float, Canvas) -> Unit) {
        val l = look
        val rise = (1 - appear) * 16
        val px = 24f
        val py = 86f + rise
        val pw = 220f
        val ph = 340f
        val lx = 256f
        val lw = 360f
        drawPanel(px, py, pw, ph, c)
        drawPanel(lx, py, lw, ph, c)
        val count = rows.size
        rowChoice[page] = rowChoice[page].coerceIn(0, max(0, count - 1))
        val choice = rowChoice[page]
        rowSlide[page] = approach(rowSlide[page], choice.toFloat(), 0.08f)
        val rowH = 40f
        val visible = 8
        val first = rowSlide[page].minus(3.5f).coerceIn(0f, max(0f, (count - visible).toFloat()))
        listArea.set(lx, py, lx + lw, py + ph)
        listRowHeight = rowH
        c.save()
        c.clipRect(lx + 2, py + 2, lx + lw - 2, py + ph - 2)
        for (i in 0 until count) {
            val y = py + 10 + (i - first) * rowH
            if (y < py - rowH || y > py + ph) continue
            val row = rows[i]
            val chosen = i == choice
            if (chosen) {
                drawChosen(lx + 6, y + 1, lw - 16, rowH - 2, c)
            } else if (i > 0) {
                line(lx + 14, y, lx + lw - 18, y, fade(l.panelLo, 0.5f), 1f, c)
            }
            val colour = if (row.act != null) l.text else l.textSoft
            val valueW = if (row.value.isEmpty()) 0f else min(150f, width(row.value, 14f))
            text(lx + 20, y + 10, 17f, colour, fit(row.name, 17f, lw - 52 - valueW), c, !l.light)
            if (row.value.isNotEmpty()) {
                textRight(lx + lw - 22, y + 12, 14f, if (chosen) l.text else l.textSoft, fit(row.value, 14f, 150f), c, !l.light)
            }
            val top = max(y, py)
            val bottom = min(y + rowH, py + ph)
            if (bottom > top) {
                hit(lx, top, lw, bottom - top) {
                    if (rowChoice[page] == i) row.act?.invoke() else rowChoice[page] = i
                }
            }
        }
        c.restore()
        picture(px, py + 18, pw, c)
        if (count > 0) {
            val row = rows[choice]
            textCentred(px + pw / 2, py + 176, 18f, l.text, fit(row.name, 18f, pw - 20), c, !l.light)
            line(px + 30, py + 202, px + pw - 30, py + 202, fade(l.panelLo, 0.7f), 1f, c)
            var ty = py + 212
            for (s in wrap(row.about, 14f, pw - 28)) {
                textCentred(px + pw / 2, ty, 14f, l.textSoft, s, c, !l.light)
                ty += 18
                if (ty > py + ph - 20) break
            }
            if (row.act != null) hit(px, py, pw, ph) { row.act.invoke() }
        }
        drawFooter(c, if (count > 0 && rows[choice].act != null) "Enter" else null)
    }

    private fun cardsPicture(x: Float, y: Float, w: Float, c: Canvas) {
        drawCard(x + w / 2 - 84, y + 10, 72f, 104f, 1f, 1, c)
        drawCard(x + w / 2 + 12, y + 10, 72f, 104f, 1f, 2, c)
    }

    private fun readerPicture(x: Float, y: Float, w: Float, c: Canvas) {
        val l = look
        val cx = x + w / 2
        val cy = y + 64
        box(cx - 66, cy - 46, 132f, 96f, l.accent, c, 8f)
        drawBook(cx, cy + 4, 58f, 0xFFFFFFFF.toInt(), fade(l.accent, 0.7f), c)
        frame(cx - 66, cy - 46, 132f, 96f, fade(0xFFFFFFFF.toInt(), 0.4f), 8f, 1.2f, c)
    }

    private fun settingsPicture(x: Float, y: Float, w: Float, c: Canvas) {
        // The console seen from above: its grey case, the round lid, the buttons.
        val cx = x + w / 2
        val cy = y + 64
        box(cx - 74, cy - 52, 148f, 108f, rgb(0xC8, 0xC7, 0xC1), c, 6f)
        frame(cx - 74, cy - 52, 148f, 108f, rgb(0x84, 0x83, 0x7E), 6f, 1.2f, c)
        disc(cx - 8, cy, 44f, rgb(0xB8, 0xB7, 0xB1), c)
        ring(cx - 8, cy, 44f, rgb(0x9A, 0x99, 0x93), 1.5f, c)
        ring(cx - 8, cy, 30f, rgb(0xA8, 0xA7, 0xA1), 1f, c)
        for (i in 0 until 3) {
            disc(cx + 56, cy - 26 + i * 26, 8f, rgb(0x9A, 0x99, 0x93), c)
            disc(cx + 56, cy - 26 + i * 26, 6f, rgb(0xDA, 0xD9, 0xD3), c)
        }
    }

    private fun open(activity: Class<*>) = context.startActivity(Intent(context, activity))

    private fun cardRows(): List<Row> {
        val rows = ArrayList<Row>()
        rows += Row("Memory Card Editor", "", "The saves on a memory card: copy them, delete them, bring in a card.") {
            host.perform("cards", null)
        }
        rows += Row("Memory Card Settings", "", "Which card each slot holds, and where the cards are kept: in the emulator's settings.") {
            host.perform("settings", null)
        }
        rows += Row("Slot 1", cardType(preferences.getString("MemoryCards/Card1Type", "PerGameTitle")),
            "The first slot. A card of its own for every game keeps each game's saves apart (and is the card save sync sends).", null)
        rows += Row("Slot 2", cardType(preferences.getString("MemoryCards/Card2Type", "None")), "The second slot.", null)
        return rows
    }

    private fun readerRows(): List<Row> {
        val rows = arrayListOf(
            Row("Reading Statistics", "", "How much you have read, and how fast, game by game and day by day.") { open(ReaderStatsActivity::class.java) },
            Row("Game Scripts", "", "The scripts that know where a game keeps its text: the ones there are, and new ones.") { open(ReaderGamesActivity::class.java) },
            Row("Dictionaries", "", "The dictionaries a word is looked up in. A Readers folder shares them with the other reader apps.") { open(DictionaryManagerActivity::class.java) },
            Row("Anki", "", "Cards made from a word, with the sentence, a picture of the game and its sound.") { open(AnkiSetupActivity::class.java) },
            Row("Backlog", "", "Every line read, to read again or make a card from.") { open(ReaderBacklogActivity::class.java) },
            Row("Reader Settings", "", "The popup, playing by touch, the CRT picture and the rest.") { open(ReaderSettingsActivity::class.java) },
        )
        return rows
    }

    private fun settingsRows(): List<Row> = listOf(
        Row("Emulator Settings", "", "The console, its BIOS, the picture, sound and the touchscreen controller.") { host.perform("settings", null) },
        Row("Controllers", "", "Which of this device's buttons are the console's.") { host.perform("controllers", null) },
        Row("Game Folders", "", "The folders the game list is made from.") { host.perform("folders", null) },
        Row("Look for Games Again", "", "Look through the game folders for new discs.") {
            host.perform("refresh", null)
            notice("Looking through the game folders")
        },
        Row("Start a Disc Image", "", "A disc image from anywhere, not only the game folders.") { host.perform("file", null) },
        Row("The Console Without a Disc", "", "The console's own screen: its memory cards and CD player.") { host.perform("console", null) },
        Row("Resume", "", "Where the console was when it was last closed.") { host.perform("resume", null) },
        Row("Import a BIOS", "", "The console's BIOS, from a file of your own console's.") { host.perform("bios", null) },
        Row("Look", look.name, look.about) {
            preferences.edit().putInt(STYLE_KEY, (style + 1) % LOOKS.size).apply()
        },
        Row("Only Games That Can Be Read", if (readableOnly) "On" else "Off", "The game list shows only the games a game script reads.") {
            preferences.edit().putBoolean(READABLE_ONLY_KEY, !readableOnly).apply()
            game = 0
        },
        Row("PlayStation Home", "On", "Off brings back DuckStation's own game list; its menu brings this home back.") {
            preferences.edit().putBoolean(HOME_KEY, false).apply()
            host.perform("classic", null)
        },
    )

    // ---- Start-up: a line of light opens and leaves the app's name behind it ----

    private fun drawBoot(c: Canvas, t: Float) {
        c.drawColor(0xFF000000.toInt())
        c.save()
        c.translate(originX, originY)
        c.scale(scale, scale)
        val out = 1f - ease((t - 2.1f) / 0.5f)
        val half = 190 * ease(t / 0.7f)
        val glow = (0.4f + 0.6f * ease(1f - (t - 0.8f) / 0.8f)) * out
        for (i in 4 downTo 1) {
            box(320 - half - i * 6, 262 - i * 1.6f, (half + i * 6) * 2, i * 3.2f, fade(rgb(0x9C, 0xA8, 0xFF), 0.08f * glow), c, i * 2f)
        }
        box(320 - half, 261.2f, half * 2, 1.6f, fade(0xFFFFFFFF.toInt(), glow), c, 1f)
        val name = "S T A T I O N   R E A D E R"
        val size = 28f
        val total = width(name, size)
        var x = 320 - total / 2
        for (ch in name) {
            val letter = ch.toString()
            val w = width(letter, size)
            val at = 0.45f + (x - (320 - total / 2)) / total * 0.75f
            val a = ease((t - at) / 0.25f) * out
            if (a > 0 && ch != ' ') {
                text(x, 216 + (1 - a) * 6, size, fade(rgb(0x9C, 0xA8, 0xFF), a * 0.35f), letter, c)
                text(x, 215 + (1 - a) * 6, size, fade(rgb(0xEE, 0xEE, 0xF4), a), letter, c)
            }
            x += w
        }
        val tag = ease((t - 1.35f) / 0.4f) * out
        if (tag > 0) textCentred(320f, 276f, 14f, fade(rgb(0x9A, 0x9C, 0xB0), tag), "read  -  look up  -  remember", c)
        c.restore()
    }

    // ---- The view ----

    private val listArea = RectF()
    private var listRowHeight = 40f

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val time = now()
        frameSeconds = (time - lastFrame).toFloat().coerceIn(0f, 0.1f)
        lastFrame = time
        scale = min(width / 640f, height / 480f)
        originX = (width - 640f * scale) / 2
        originY = (height - 480f * scale) / 2
        sheetLeft = -originX / scale
        sheetRight = 640 + originX / scale
        hits.clear()
        if (booting && time - startedAt > 2.6) {
            booting = false
            shownAt = time
        }
        if (booting) {
            drawBoot(canvas, (time - startedAt).toFloat())
        } else {
            drawGround(canvas, time.toFloat())
            canvas.save()
            canvas.translate(originX, originY)
            canvas.scale(scale, scale)
            val appear = ease(((time - shownAt) / 0.28).toFloat())
            drawHeader(canvas)
            drawTabs(canvas)
            when (page) {
                0 -> drawGames(canvas, appear)
                1 -> drawList(canvas, appear, cardRows(), ::cardsPicture)
                2 -> drawList(canvas, appear, readerRows(), ::readerPicture)
                else -> drawList(canvas, appear, settingsRows(), ::settingsPicture)
            }
            if (notice.isNotEmpty()) {
                val age = (time - noticeAt).toFloat()
                if (age > 3.5f) {
                    notice = ""
                } else {
                    val l = look
                    val a = min(1f, min(age * 5, (3.5f - age) * 3))
                    val w = width(notice, 15f) + 40
                    bevel(320 - w / 2, 396f, w, 30f, fade(l.button, a), fade(l.buttonHi, a), fade(l.buttonLo, a), canvas)
                    textCentred(320f, 402f, 15f, fade(l.buttonText, a), notice, canvas, !l.light)
                }
            }
            canvas.restore()
            // Coming from the start-up screen, the black clears away.
            if (time - startedAt < 3.2) {
                val dark = 1f - ease(((time - shownAt) / 0.45).toFloat())
                if (dark > 0) canvas.drawColor(fade(0xFF000000.toInt(), dark))
            }
        }
        if (isShown) postInvalidateOnAnimation()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = event.x
                downY = event.y
                dragging = false
                dragRows = 0f
            }
            MotionEvent.ACTION_MOVE -> {
                if (!dragging && (abs(event.x - downX) > slop || abs(event.y - downY) > slop)) dragging = true
                if (dragging && !booting) {
                    // A drag over a list moves its choice a row for each row's height.
                    val sx = (downX - originX) / scale
                    val sy = (downY - originY) / scale
                    if (listArea.contains(sx, sy)) {
                        val rows = (downY - event.y) / scale / listRowHeight
                        val step = rows.toInt() - dragRows.toInt()
                        if (step != 0) {
                            move(step)
                            dragRows = rows
                        }
                    }
                }
            }
            MotionEvent.ACTION_UP -> if (!dragging) tap((event.x - originX) / scale, (event.y - originY) / scale)
        }
        return true
    }

    private fun tap(x: Float, y: Float) {
        if (booting) {
            booting = false
            shownAt = now()
            return
        }
        hits.lastOrNull { it.area.contains(x, y) }?.action?.invoke()
        invalidate()
    }

    /** Moves the choice of the page's list by `step` rows. */
    private fun move(step: Int) {
        if (page == 0) {
            val count = shown().size
            if (count > 0) game = (game + step).coerceIn(0, count - 1)
        } else {
            rowChoice[page] += step
        }
    }

    private fun rowsOf(page: Int): List<Row> = when (page) {
        1 -> cardRows()
        2 -> readerRows()
        else -> settingsRows()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (booting) {
            if (keyCode == KeyEvent.KEYCODE_BACK) return super.onKeyDown(keyCode, event)
            booting = false
            shownAt = now()
            return true
        }
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_BUTTON_L1 -> goPage(page - 1)
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_BUTTON_R1 -> goPage(page + 1)
            KeyEvent.KEYCODE_DPAD_UP -> wrapMove(-1)
            KeyEvent.KEYCODE_DPAD_DOWN -> wrapMove(1)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_SPACE -> {
                if (event.repeatCount == 0) accept()
            }
            else -> return super.onKeyDown(keyCode, event)
        }
        invalidate()
        return true
    }

    private fun wrapMove(step: Int) {
        val count = if (page == 0) shown().size else rowsOf(page).size
        if (count == 0) return
        if (page == 0) game = (game + step + count) % count
        else rowChoice[page] = (rowChoice[page] + step + count) % count
    }

    private fun accept() {
        if (page == 0) {
            shown().getOrNull(game)?.let(::start)
        } else {
            rowsOf(page).getOrNull(rowChoice[page])?.act?.invoke()
        }
    }

    companion object {
        const val HOME_KEY = "Reader/PlayStationHome"
        const val STYLE_KEY = "Reader/PlayStationStyle"
        const val READABLE_ONLY_KEY = "Reader/ReadableOnly"

        /** Whether the home is on (it is unless switched off). */
        @JvmStatic
        fun enabled(context: Context): Boolean =
            PreferenceManager.getDefaultSharedPreferences(context).getBoolean(HOME_KEY, true)

        @JvmStatic
        fun setEnabled(context: Context, on: Boolean) {
            PreferenceManager.getDefaultSharedPreferences(context).edit().putBoolean(HOME_KEY, on).apply()
        }

        private const val PAGE_COUNT = 4
        private val PAGE_NAMES = arrayOf("Games", "Memory Card", "Reader", "Settings")

        private fun rgb(r: Int, g: Int, b: Int) = argb(255, r, g, b)
        private fun argb(a: Int, r: Int, g: Int, b: Int) = (a shl 24) or (r shl 16) or (g shl 8) or b

        private fun fade(colour: Int, alpha: Float): Int {
            val a = ((colour ushr 24) * alpha.coerceIn(0f, 1f)).toInt()
            return (colour and 0x00FFFFFF) or (a shl 24)
        }

        private fun ease(t: Float): Float {
            val x = t.coerceIn(0f, 1f)
            return x * x * (3f - 2f * x)
        }

        private fun cardType(saved: String?): String = when (saved) {
            "None" -> "No Memory Card"
            "Shared" -> "Shared Between All Games"
            "PerGame" -> "Separate Card Per Game (Serial)"
            "PerGameTitle" -> "Separate Card Per Game (Title)"
            "PerGameFileTitle" -> "Separate Card Per Game (File Title)"
            else -> saved ?: ""
        }

        // The console's four button marks, in the colours they are printed in.
        private val MARK_TRIANGLE = rgb(0x3C, 0xC8, 0x8C)
        private val MARK_CIRCLE = rgb(0xF0, 0x50, 0x6E)
        private val MARK_CROSS = rgb(0x6E, 0x8C, 0xF0)
        private val MARK_SQUARE = rgb(0xE6, 0x78, 0xC8)

        private val LOOKS = arrayOf(
            Look("System Screen", "The console's own menu: a dark ground with slow bands of light", false,
                rgb(0x06, 0x06, 0x0C), rgb(0x18, 0x15, 0x2E),
                argb(0x2C, 0x96, 0x98, 0xAC), argb(0x60, 0xFF, 0xFF, 0xFF), argb(0xA0, 0x00, 0x00, 0x00),
                rgb(0x8E, 0x90, 0x9E), rgb(0xDA, 0xDB, 0xE4), rgb(0x3E, 0x3F, 0x4A),
                rgb(0xF2, 0xF2, 0xF6), rgb(0xA6, 0xA8, 0xBC), rgb(0xFF, 0xFF, 0xFF), argb(0xB0, 0, 0, 0),
                rgb(0xFF, 0xFF, 0xFF), argb(0x38, 0x8C, 0x96, 0xFF), rgb(0x8C, 0x78, 0xFF),
                rgb(0x08, 0x0A, 0x0E), rgb(0x7F, 0xE0, 0xC8)),
            Look("Memory Card", "The memory card manager: deep blue, rows of blocks", false,
                rgb(0x10, 0x24, 0x78), rgb(0x03, 0x08, 0x22),
                argb(0x48, 0x3C, 0x5C, 0xC8), argb(0x70, 0xB4, 0xC4, 0xFF), argb(0xA0, 0x00, 0x00, 0x18),
                rgb(0x80, 0x88, 0xB2), rgb(0xCC, 0xD4, 0xF2), rgb(0x2A, 0x30, 0x60),
                rgb(0xFF, 0xFF, 0xFF), rgb(0xA8, 0xB6, 0xE8), rgb(0xFF, 0xFF, 0xFF), argb(0xB0, 0x00, 0x00, 0x20),
                rgb(0xFF, 0xFF, 0xFF), argb(0x24, 0xFF, 0xFF, 0xFF), rgb(0x7F, 0xD0, 0xFF),
                rgb(0x04, 0x08, 0x20), rgb(0x9F, 0xD8, 0xFF)),
            Look("Console Grey", "The grey of the console's case, as the reader's popup has it", true,
                rgb(0xEC, 0xEB, 0xE6), rgb(0xCD, 0xCC, 0xC6),
                argb(0x70, 0xFF, 0xFF, 0xFF), rgb(0xFF, 0xFF, 0xFF), rgb(0x9C, 0x9B, 0x95),
                rgb(0xC2, 0xC1, 0xBB), rgb(0xF6, 0xF5, 0xF0), rgb(0x84, 0x83, 0x7E),
                rgb(0x23, 0x24, 0x2B), rgb(0x6A, 0x6B, 0x74), rgb(0x23, 0x24, 0x2B), argb(0x90, 0xFF, 0xFF, 0xFF),
                rgb(0x4B, 0x3F, 0xC8), argb(0x2A, 0x5A, 0x50, 0xDC), rgb(0x5B, 0x4F, 0xD0),
                rgb(0x23, 0x24, 0x2B), rgb(0x9F, 0xE8, 0xD0)),
        )
    }
}
