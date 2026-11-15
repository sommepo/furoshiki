// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import kotlin.math.abs

/**
 * What a text area looks like, small: its brightness on a grid. Two of them say how much of
 * the area changed, which is all that is asked before the area is read.
 */
class OcrSignature(val columns: Int, val rows: Int, val cells: ByteArray) {
    /** The share of the grid (0 to 1) that differs from [other] by more than a flicker; 1 for another shape. */
    fun changedShare(other: OcrSignature): Float {
        if (other.columns != columns || other.rows != rows) return 1f
        var changed = 0
        for (i in cells.indices) {
            if (abs((cells[i].toInt() and 0xFF) - (other.cells[i].toInt() and 0xFF)) > TOLERANCE) changed++
        }
        return changed.toFloat() / cells.size.coerceAtLeast(1)
    }

    companion object {
        const val COLUMNS = 48
        const val ROWS = 16
        private const val TOLERANCE = 28

        /**
         * Of the part of [picture] from ([left], [top]) to ([right], [bottom]), 0 to 1 of it: each
         * cell the mean brightness of what lies in it.
         */
        fun of(picture: OcrPicture, left: Float, top: Float, right: Float, bottom: Float): OcrSignature {
            val x0 = (left * picture.width).toInt().coerceIn(0, picture.width - 1)
            val y0 = (top * picture.height).toInt().coerceIn(0, picture.height - 1)
            val x1 = (right * picture.width).toInt().coerceIn(x0 + 1, picture.width)
            val y1 = (bottom * picture.height).toInt().coerceIn(y0 + 1, picture.height)
            val cells = ByteArray(COLUMNS * ROWS)
            for (row in 0 until ROWS) {
                val fromY = y0 + (y1 - y0) * row / ROWS
                val toY = maxOf(fromY + 1, y0 + (y1 - y0) * (row + 1) / ROWS).coerceAtMost(y1)
                for (column in 0 until COLUMNS) {
                    val fromX = x0 + (x1 - x0) * column / COLUMNS
                    val toX = maxOf(fromX + 1, x0 + (x1 - x0) * (column + 1) / COLUMNS).coerceAtMost(x1)
                    var sum = 0L
                    var count = 0
                    for (y in fromY until toY) {
                        for (x in fromX until toX) {
                            val pixel = picture.pixels[y * picture.width + x]
                            // Brightness as the eye has it, in whole numbers.
                            sum += (((pixel shr 16) and 0xFF) * 77 + ((pixel shr 8) and 0xFF) * 150 + (pixel and 0xFF) * 29) shr 8
                            count++
                        }
                    }
                    cells[row * COLUMNS + column] = (if (count > 0) sum / count else 0).toByte()
                }
            }
            return OcrSignature(COLUMNS, ROWS, cells)
        }

        /** Several areas as one: a change in any of them is a change. */
        fun joined(parts: List<OcrSignature>): OcrSignature {
            if (parts.size == 1) return parts[0]
            val cells = ByteArray(parts.sumOf { it.cells.size })
            var at = 0
            for (part in parts) {
                System.arraycopy(part.cells, 0, cells, at, part.cells.size)
                at += part.cells.size
            }
            return OcrSignature(COLUMNS, ROWS * parts.size, cells)
        }
    }
}

/**
 * When a text area is to be read: once it looks other than when it was read last, and has then
 * stood still for a while (a line being written out, a window sliding in and a fade are not
 * read half way). Looked at a few times a second with [see]; never reads by itself.
 */
class OcrWatch(
    /** How long the area must stand still before it is read. */
    private val settleMs: Long = 350,
    /** The share of the area that must differ from what was read last. */
    private val minChange: Float = 0.006f,
    /** Under this share from one look to the next the area stands still. */
    private val stillChange: Float = 0.004f,
    /** Moving without a rest for this long (a film behind the text): read anyway. */
    private val longestWaitMs: Long = 4000
) {
    enum class Verdict {
        /** Nothing to do: the area is as it was read, or still changing. */
        WAIT,

        /** The area has begun to change from what was read: what was read there is no longer on screen. */
        CHANGED,

        /** The area changed and stands still: read it now. */
        READ
    }

    private var read: OcrSignature? = null
    private var last: OcrSignature? = null
    private var stillSince = 0L
    private var movingSince = -1L
    private var toldChanged = false

    /** A look at the area at [now] (milliseconds, any clock that only goes forward). */
    fun see(signature: OcrSignature, now: Long): Verdict {
        val before = last
        last = signature
        if (before == null || signature.changedShare(before) > stillChange) {
            stillSince = now
            if (movingSince < 0) movingSince = now
        }
        val was = read
        val differs = was == null || signature.changedShare(was) > minChange
        if (!differs) {
            // Back to what was read (a cursor that blinks): nothing new.
            movingSince = -1
            toldChanged = false
            return Verdict.WAIT
        }
        val still = now - stillSince >= settleMs
        val tooLong = movingSince >= 0 && now - movingSince >= longestWaitMs
        if (still || tooLong) {
            read = signature
            movingSince = -1
            toldChanged = false
            return Verdict.READ
        }
        if (!toldChanged && was != null) {
            toldChanged = true
            return Verdict.CHANGED
        }
        return Verdict.WAIT
    }

    /** The area is read at its next look, whatever it shows (the areas were changed, the engine too). */
    fun forget() {
        read = null
        last = null
        movingSince = -1
        toldChanged = false
    }
}

/**
 * An engine that cannot be reached (the service on another machine) is not asked at every
 * line: after a failure it is left alone for a while, longer each time, and the user is told
 * once, and once again when it answers.
 */
class OcrBackoff(private val firstMs: Long = 2000, private val longestMs: Long = 60_000) {
    private var failures = 0
    private var until = 0L
    private var told = false

    /** Whether the engine may be asked at [now]. */
    fun mayAsk(now: Long): Boolean = now >= until

    /** A read failed at [now]. True when this is to be said to the user: the first of a row of failures. */
    fun failed(now: Long): Boolean {
        val wait = (firstMs shl failures.coerceAtMost(16)).coerceAtMost(longestMs)
        failures++
        until = now + wait
        if (told) return false
        told = true
        return true
    }

    /** A read was answered. True when the user was told it failed before: say that it is back. */
    fun answered(): Boolean {
        failures = 0
        until = 0
        val was = told
        told = false
        return was
    }
}
