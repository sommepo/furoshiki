// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import org.json.JSONObject

/**
 * A line an engine read: its text, and where each of its characters is ([x, y, width, height],
 * 0 to 1 of the picture that was read), one for each code point of the text, or null where the
 * engine does not say.
 */
class OcrLine(val text: String, val boxes: List<FloatArray>?, val upright: Boolean = false)

/** What an engine read in a picture: the lines in reading order, and their texts as one. */
class OcrRead(val lines: List<OcrLine>, val engine: String = "") {
    val text: String get() = lines.joinToString("") { it.text }

    /** Each character's place, when every line says its characters'. */
    val boxes: List<FloatArray>? get() = if (lines.isNotEmpty() && lines.all { it.boxes != null }) lines.flatMap { it.boxes!! } else null

    companion object {
        /**
         * The answer of the OCR service or helper to `POST /api/ocr/read`:
         * `{"text", "engine", "ms", "lines": [{"text", "boxes": [[x, y, w, h]...], "vertical"}]}`.
         * An `error` in it, or an answer that is not this, throws with what it says.
         */
        fun fromJson(json: String): OcrRead {
            val root = try {
                JSONObject(json)
            } catch (_: Exception) {
                throw IllegalArgumentException("the OCR service's answer is not JSON")
            }
            root.optString("error").takeIf { it.isNotEmpty() }?.let { throw IllegalStateException(it) }
            val lines = ArrayList<OcrLine>()
            val list = root.optJSONArray("lines")
            if (list != null) {
                for (i in 0 until list.length()) {
                    val item = list.optJSONObject(i) ?: continue
                    val text = item.optString("text")
                    if (text.isEmpty()) continue
                    lines.add(OcrLine(text, boxesOf(item, text), item.optBoolean("vertical")))
                }
            } else if (!root.has("text")) {
                throw IllegalArgumentException("the OCR service's answer has no text")
            }
            // A service that says only the text: one line, its characters' places unknown.
            if (lines.isEmpty() && root.optString("text").isNotEmpty()) lines.add(OcrLine(root.getString("text"), null))
            return OcrRead(lines, root.optString("engine"))
        }

        /** A line's boxes, only when there is one for each of its characters and each is a box. */
        private fun boxesOf(item: JSONObject, text: String): List<FloatArray>? {
            val list = item.optJSONArray("boxes") ?: return null
            if (list.length() != text.codePointCount(0, text.length)) return null
            val out = ArrayList<FloatArray>(list.length())
            for (i in 0 until list.length()) {
                val box = list.optJSONArray(i) ?: return null
                if (box.length() < 4) return null
                val values = FloatArray(4) { box.optDouble(it, Double.NaN).toFloat() }
                if (values.any { !it.isFinite() } || values[2] < 0f || values[3] < 0f) return null
                out.add(values)
            }
            return out
        }
    }
}

/** From what the engine of the device saw to the lines that are read: the service does this itself. */
object OcrText {
    private fun japanese(point: Int): Boolean =
        point in 0x3040..0x30FF || point in 0x3400..0x4DBF || point in 0x4E00..0x9FFF || point in 0xF900..0xFAFF ||
            point in 0xFF66..0xFF9F || point == 0x3005 || point == 0x3007

    fun hasJapanese(text: String): Boolean = text.codePoints().anyMatch { japanese(it) }

    private fun kanaOnly(text: String): Boolean =
        text.isNotEmpty() && text.codePoints().allMatch { it in 0x3040..0x30FF || it == 0x30FC || Character.isWhitespace(it) }

    /** How a sentence ends. */
    private const val ENDS = "。！？!?」』…）)"

    /**
     * Whether the last of [glyphs] is not text but stands after a sentence's end: a digit or a Latin
     * letter, ・ or ー (a game's animated mark read as one: Paranormasight's hand came out as 4, 1,
     * ・ and ー, and one line was several), or a stop again (。。、).
     */
    fun strayAfterEnd(glyphs: List<String>): Boolean {
        if (glyphs.size < 2) return false
        val last = glyphs.last()
        val before = glyphs[glyphs.size - 2]
        if (before.isEmpty() || before.last() !in ENDS) return false
        // What may close a line after its end: a quote's end, a mark of a question or of feeling.
        if (last.isEmpty() || last.all { it in "」』）)！？!?…" }) return false
        // Words (kana, kanji) stay, but for the long-vowel mark, which a word never ends a line on
        // after a stop; anything else (a digit, a letter, ・, ー, a stop again) is the game's mark.
        return last == "ー" || last.none { Character.isLetter(it) && it.code >= 0x3000 && it != 'ー' }
    }

    /**
     * How many of the last row's [glyphs] stand after its last sentence's end and are not text: the
     * text read is whole, so nothing but a closing mark follows its last 。！？ (Paranormasight's
     * hand came out as ・人 and のこ too). A few at most; none where a closing mark is among them.
     */
    fun afterLastEnd(glyphs: List<String>): Int {
        val end = glyphs.indexOfLast { it.isNotEmpty() && it.last() in "。！？!?" }
        if (end < 0) return 0
        val after = glyphs.size - 1 - end
        if (after !in 1..3) return 0
        if (glyphs.drop(end + 1).any { glyph -> glyph.any { it in "」』）)…！？!?" } }) return 0
        return after
    }

    /** What a game draws after its text and is not text: the mark that asks for a button. */
    private const val TRAILING = "▼▽▾▿♥♡★☆◆◇■□●○◎→↓⏎⤵ 　"

    /**
     * [lines] as read by the engine (pixels of a picture [width] by [height]) made the lines to
     * read: furigana dropped (a short line of kana alone, lying on a taller line right under it, or
     * standing right of a wider column), lines without Japanese dropped, gaps closed, the mark after
     * the text taken off; upright columns from the right, else rows from the top.
     */
    fun clean(lines: List<MeikiOcr.Line>, width: Int, height: Int, engine: String = "meikiocr"): OcrRead {
        class Box(val line: MeikiOcr.Line) {
            val left = line.glyphs.minOf { it.left }
            val top = line.glyphs.minOf { it.top }
            val right = line.glyphs.maxOf { it.right }
            val bottom = line.glyphs.maxOf { it.bottom }
            val thick get() = if (line.upright) right - left else bottom - top
        }
        val all = lines.filter { it.glyphs.isNotEmpty() }.map { Box(it) }
        val kept = all.filter { ruby ->
            if (!kanaOnly(ruby.line.text)) return@filter true
            all.none { base ->
                base !== ruby && base.line.upright == ruby.line.upright && ruby.thick < base.thick * 0.7f &&
                    if (ruby.line.upright) {
                        // Right of its column, and beside it.
                        ruby.left >= base.right - base.thick * 0.3f && ruby.left <= base.right + base.thick * 0.8f &&
                            ruby.top < base.bottom && ruby.bottom > base.top
                    } else {
                        // Over its row, and above it.
                        ruby.bottom <= base.top + base.thick * 0.3f && ruby.bottom >= base.top - base.thick * 0.8f &&
                            ruby.left < base.right && ruby.right > base.left
                    }
            }
        }
        // The game's mark after its text, read as a line of its own (one or two characters lying just
        // past the end of a full row, beside it): Paranormasight's bobbing hand came out as っ or つ
        // between the rows, and one line was read anew every few seconds.
        val marks = kept.filter { short ->
            !short.line.upright && short.line.glyphs.size <= 2 && kept.any { row ->
                row !== short && !row.line.upright && row.line.glyphs.size >= 4 &&
                    short.left >= row.right - row.thick * 0.5f && short.left <= row.right + row.thick * 1.5f &&
                    Math.abs((short.top + short.bottom) / 2f - (row.top + row.bottom) / 2f) <= row.thick * 1.5f
            }
        }
        val ordered = if (kept.isNotEmpty() && kept.all { it.line.upright }) {
            kept.sortedByDescending { it.right }
        } else {
            kept.sortedWith(compareBy({ Math.round(it.top.toFloat() / maxOf(it.bottom - it.top, 1)) }, { it.left }))
        }
        val out = ArrayList<OcrLine>()
        val lastRow = ordered.indexOfLast { hasJapanese(it.line.text) && it !in marks }
        for ((index, box) in ordered.withIndex()) {
            if (!hasJapanese(box.line.text) || box in marks) continue
            var glyphs = box.line.glyphs.filter { glyph -> glyph.text.isNotBlank() }
            while (glyphs.isNotEmpty() && (glyphs.last().text in TRAILING || strayAfterEnd(glyphs.map { it.text }))) glyphs = glyphs.dropLast(1)
            if (index == lastRow) glyphs = glyphs.dropLast(afterLastEnd(glyphs.map { it.text }))
            if (glyphs.isEmpty()) continue
            out.add(
                OcrLine(
                    glyphs.joinToString("") { it.text },
                    glyphs.map {
                        floatArrayOf(
                            it.left.toFloat() / width, it.top.toFloat() / height,
                            (it.right - it.left).toFloat() / width, (it.bottom - it.top).toFloat() / height
                        )
                    },
                    box.line.upright
                )
            )
        }
        return OcrRead(out, engine)
    }

    /**
     * Whether [a] and [b] are one text read twice: the same, or (8 characters and more) apart by
     * one character in twenty at most. OCR reads a still line a little differently now and then (a
     * cloud of Paranormasight's background came out as っ, then つ), and each was a new line.
     */
    fun sameText(a: String, b: String): Boolean {
        if (a == b) return true
        val x = a.codePoints().toArray()
        val y = b.codePoints().toArray()
        val shorter = minOf(x.size, y.size)
        if (shorter < 8) return false
        val allowed = maxOf(1, shorter / 20)
        if (Math.abs(x.size - y.size) > allowed) return false
        var previous = IntArray(y.size + 1) { it }
        for (i in 1..x.size) {
            val current = IntArray(y.size + 1)
            current[0] = i
            var best = current[0]
            for (j in 1..y.size) {
                current[j] = minOf(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + if (x[i - 1] == y[j - 1]) 0 else 1)
                best = minOf(best, current[j])
            }
            if (best > allowed) return false
            previous = current
        }
        return previous[y.size] <= allowed
    }

    /** A stable name for a text: the same text read again is the same line read again. */
    fun sourceId(text: String): String {
        var hash = -0x340d631b7bdddcdbL // FNV-1a, 64 bits
        for (byte in text.toByteArray(Charsets.UTF_8)) {
            hash = hash xor (byte.toLong() and 0xFF)
            hash *= 0x100000001b3L
        }
        return "ocr:" + java.lang.Long.toHexString(hash)
    }
}
