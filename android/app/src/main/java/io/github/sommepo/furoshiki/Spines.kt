// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import java.io.File
import kotlin.math.roundToInt

/**
 * A spine drawn in its console's look where no picture of the real one is had (the maintainer,
 * 2026-11-23: "i preferred the spines when they looked like the original consoles' real spines ...
 * a tool in the public build to generate those when spine art is missing? android and windows"):
 * the desktop's `MadeSpine` (`home/src/shelf_page.inc`), the same table and drawing. The band at
 * its head with the console's name in plain type, the title set down it as a Japanese book's is,
 * the serial at its foot. Only colours, bands and type: nothing of a console maker's (no logo,
 * wordmark or rating board's mark), as everywhere in Furoshiki. Kept beside the found spines as
 * `<ID>.drawn.png`, which a found one replaces.
 */
object Spines {
    private class Style(
        val shape: Float, // width to height, as the real case's spine
        val body: Int, val ink: Int,
        val band: Int, val bandInk: Int, val bandShare: Float,
        val name: String,
        val foot: Int, val footInk: Int,
        val serial: Boolean,
    )

    private const val NONE = Color.TRANSPARENT
    private val WHITE = Color.WHITE

    /** By the app's console key; the desktop's SpineStyleOf by platform. */
    private val STYLES = mapOf(
        // A red case's red insert: the title in white, no band.
        "switch" to Style(0.066f, Color.rgb(0xe6, 0x00, 0x12), WHITE, Color.rgb(0xe6, 0x00, 0x12), WHITE, 0f, "SWITCH", NONE, WHITE, false),
        // The black head over a white spine, the serial at the foot.
        "ps2" to Style(0.075f, Color.rgb(0xf4, 0xf4, 0xf2), Color.rgb(0x14, 0x14, 0x14), Color.rgb(0x0c, 0x0c, 0x0c), WHITE, 0.16f, "PlayStation 2",
            Color.rgb(0xf4, 0xf4, 0xf2), Color.rgb(0x14, 0x14, 0x14), true),
        "ps1" to Style(0.05f, Color.rgb(0xf2, 0xf2, 0xf0), Color.rgb(0x14, 0x14, 0x14), Color.rgb(0x0c, 0x0c, 0x0c), WHITE, 0.17f, "PlayStation",
            Color.rgb(0xf2, 0xf2, 0xf0), Color.rgb(0x14, 0x14, 0x14), true),
        "psp" to Style(0.082f, Color.rgb(0xf4, 0xf4, 0xf2), Color.rgb(0x14, 0x14, 0x14), Color.rgb(0x0c, 0x0c, 0x0c), WHITE, 0.13f, "PSP",
            Color.rgb(0x1c, 0x2f, 0x6b), WHITE, true),
        "psvita" to Style(0.08f, Color.rgb(0xf4, 0xf4, 0xf2), Color.rgb(0x14, 0x14, 0x14), Color.rgb(0x0c, 0x0c, 0x0c), WHITE, 0.13f, "PS VITA",
            Color.rgb(0xf4, 0xf4, 0xf2), Color.rgb(0x14, 0x14, 0x14), true),
        // White with the orange head of Japan's Dreamcast boxes.
        "dc" to Style(0.06f, Color.rgb(0xf6, 0xf5, 0xf1), Color.rgb(0x14, 0x14, 0x14), Color.rgb(0xf2, 0x6b, 0x1d), WHITE, 0.15f, "Dreamcast",
            Color.rgb(0xf6, 0xf5, 0xf1), Color.rgb(0x14, 0x14, 0x14), true),
        // White, the console's name in red at the head.
        "3ds" to Style(0.07f, Color.rgb(0xf6, 0xf6, 0xf4), Color.rgb(0x14, 0x14, 0x14), Color.rgb(0xf6, 0xf6, 0xf4), Color.rgb(0xd7, 0x0f, 0x1f), 0.12f, "3DS",
            Color.rgb(0xf6, 0xf6, 0xf4), Color.rgb(0x14, 0x14, 0x14), true),
    )

    private const val HEIGHT = 1000
    /** This drawing's version: a spine drawn by an older one is drawn again. */
    private const val DRAWING = 1

    /** The drawn spine's file of a game. */
    fun fileOf(context: Context, key: String, id: String) =
        File(Places.console(context, key, "spines"), "${Covers.safeId(id)}.drawn.png")

    /**
     * The game's drawn spine, made where there is none yet (or [title] changed); null for a
     * console with no look of its own here (the DS).
     */
    fun drawn(context: Context, key: String, id: String, title: String): File? {
        val style = STYLES[key] ?: return null
        val file = fileOf(context, key, id)
        val said = File(file.path + ".title")
        val name = shelfName(title)
        // Drawn again when the title or this drawing changes (DRAWING).
        val stamp = "$DRAWING|$name"
        if (file.isFile && said.isFile && runCatching { said.readText() }.getOrNull() == stamp) return file
        val serial = if (key == "switch" || id.startsWith("LINES-")) "" else id
        val picture = draw(context, style, name, serial)
        val partial = File(file.path + ".new")
        return try {
            file.parentFile?.mkdirs()
            val ok = partial.outputStream().use { picture.compress(Bitmap.CompressFormat.PNG, 100, it) } &&
                partial.renameTo(file)
            if (ok) {
                said.writeText(stamp)
                file
            } else {
                partial.delete()
                null
            }
        } catch (_: Exception) {
            partial.delete()
            null
        } finally {
            picture.recycle()
        }
    }

    /** A title as the shelf says it: without what stands in brackets after it (the desktop's ShelfName). */
    private fun shelfName(title: String): String {
        var name = title.trim()
        val tail = Regex("\\s*(?:[（(][^（()）]*[)）]|\\[[^\\[\\]]*])\\s*$")
        repeat(6) {
            val shorter = name.replace(tail, "").trim()
            if (shorter.isEmpty() || shorter == name) return@repeat
            name = shorter
        }
        return name.replace("_ ", ": ")
    }

    /**
     * The serial as the foot prints it: at its hyphen ("SLPM" over "66302"), else a long one in two
     * (a Vita's "PCSG" over "00488", a 3DS title ID's sixteen figures eight over eight).
     */
    private fun serialLines(serial: String): List<String> {
        val id = serial.uppercase()
        val dash = id.indexOf('-')
        if (dash > 0 && dash < id.lastIndex) return listOf(id.substring(0, dash), id.substring(dash + 1))
        val letters = id.takeWhile { it.isLetter() }
        return when {
            letters.length in 2..5 && id.length > letters.length -> listOf(letters, id.substring(letters.length))
            id.length > 8 -> listOf(id.substring(0, id.length / 2), id.substring(id.length / 2))
            else -> listOf(id)
        }
    }

    private fun hasJapanese(text: String) = text.any { it.code >= 0x2E80 }
    private fun isLatin(c: Char) = c.code in 0x21 until 0x250
    private const val LIES_DOWN = "ー〜～−-—–…‥()（）「」『』【】〈〉《》[]:：;；=＝~"

    private val typefaces = java.util.concurrent.ConcurrentHashMap<String, Typeface>()

    private fun typeface(context: Context, file: String): Typeface =
        typefaces.getOrPut(file) {
            runCatching { Typeface.createFromAsset(context.assets, "front/fonts/$file") }.getOrDefault(Typeface.DEFAULT_BOLD)
        }

    private fun draw(context: Context, style: Style, title: String, serial: String): Bitmap {
        val width = maxOf(40, (HEIGHT * style.shape).roundToInt())
        val picture = Bitmap.createBitmap(width, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(picture)
        canvas.drawColor(style.body)
        val bold = typeface(context, "ZenKakuGothicNew-Bold.ttf")
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = bold }
        var top: Float
        // The console's name, lying down along the band, in plain type, as large as fits.
        val band = HEIGHT * style.bandShare
        if (band > 0) {
            paint.color = style.band
            canvas.drawRect(0f, 0f, width.toFloat(), band, paint)
            var size = width * 0.42f
            paint.textSize = size
            while (size > width * 0.18f && paint.measureText(style.name) > band - 16) {
                size *= 0.93f
                paint.textSize = size
            }
            paint.color = style.bandInk
            canvas.save()
            canvas.translate(width / 2f, band / 2f)
            canvas.rotate(90f)
            val wide = paint.measureText(style.name)
            canvas.drawText(style.name, -wide / 2, -(paint.ascent() + paint.descent()) / 2, paint)
            canvas.restore()
            top = band
        } else {
            // No band: the console's name small at the head, in the title's colour.
            paint.textSize = width * 0.3f
            paint.color = style.ink
            canvas.save()
            canvas.translate(width / 2f, 22f)
            canvas.rotate(90f)
            canvas.drawText(style.name, 0f, width * 0.11f, paint)
            canvas.restore()
            top = 22 + paint.measureText(style.name)
        }
        // The foot: the serial, as on the real ones.
        val foot = if (style.serial) HEIGHT * 0.12f else HEIGHT * 0.05f
        if (Color.alpha(style.foot) > 0) {
            paint.color = style.foot
            canvas.drawRect(0f, HEIGHT - foot, width.toFloat(), HEIGHT.toFloat(), paint)
        }
        if (style.serial && serial.isNotEmpty()) {
            val mono = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                typeface = typeface(context, "IBMPlexMono-Medium.ttf")
                textSize = width * 0.2f
                color = style.footInk
                textAlign = Paint.Align.CENTER
            }
            val lines = serialLines(serial)
            // As large as the widest line lets it be across the spine.
            val widest = lines.maxOf { mono.measureText(it) }
            if (widest > width * 0.9f) mono.textSize *= width * 0.9f / widest
            val step = mono.fontSpacing
            var y = HEIGHT - foot / 2 - step * (lines.size - 1) / 2 - (mono.ascent() + mono.descent()) / 2
            for (line in lines) {
                canvas.drawText(line, width / 2f, y, mono)
                y += step
            }
        }
        // The title, from just under the head to just over the foot, as large as it will fit: each
        // character a cell going down, a run of Latin letters lying on its side.
        val from = top + width * 0.5f
        val until = HEIGHT - foot - width * 0.4f
        val japanese = hasJapanese(title)
        var size = width * (if (japanese) 0.62f else 0.5f)
        paint.textSize = size
        fun cell() = (paint.fontMetrics.descent - paint.fontMetrics.ascent) * 0.92f
        fun runAt(c: Int): Int {
            var run = 0
            while (c + run < title.length && isLatin(title[c + run])) run++
            return run
        }
        fun measure(): Float {
            var length = 0f
            var c = 0
            while (c < title.length) {
                if (title[c].isWhitespace()) { length += cell() * 0.4f; c++; continue }
                val run = runAt(c)
                if (run > 2 || (!japanese && run > 0)) {
                    length += paint.measureText(title, c, c + run) + cell() * 0.15f
                    c += run
                } else {
                    length += cell()
                    c++
                }
            }
            return length
        }
        while (size > width * 0.24f && measure() > until - from) {
            size *= 0.94f
            paint.textSize = size
        }
        paint.color = style.ink
        val cell = cell()
        var y = from
        var c = 0
        canvas.save()
        canvas.clipRect(0f, from, width.toFloat(), until)
        while (c < title.length && y < until) {
            val ch = title[c]
            if (ch.isWhitespace()) { y += cell * 0.4f; c++; continue }
            val run = runAt(c)
            if (run > 2 || (!japanese && run > 0)) {
                val word = title.substring(c, c + run)
                canvas.save()
                canvas.translate(width / 2f, y)
                canvas.rotate(90f)
                canvas.drawText(word, 0f, -(paint.ascent() + paint.descent()) / 2, paint)
                canvas.restore()
                y += paint.measureText(word) + cell * 0.15f
                c += run
                continue
            }
            val text = ch.toString()
            val wide = paint.measureText(text)
            if (ch in LIES_DOWN) {
                canvas.save()
                canvas.translate(width / 2f, y + cell / 2)
                canvas.rotate(90f)
                canvas.drawText(text, -wide / 2, -(paint.ascent() + paint.descent()) / 2, paint)
                canvas.restore()
            } else {
                canvas.drawText(text, width / 2f - wide / 2, y + cell / 2 - (paint.ascent() + paint.descent()) / 2, paint)
            }
            y += cell
            c++
        }
        canvas.restore()
        return picture
    }
}
