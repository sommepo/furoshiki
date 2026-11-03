// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.model

import org.json.JSONArray
import org.json.JSONObject

/**
 * Where the text of a line is in the game's picture, as reported by the game's profile. This
 * is what makes the game's own text something the player can point at. A console with two
 * screens (the 3DS) says which of them the text is on ([screen]).
 *
 * @property window The [TextLine.window] this belongs to.
 * @property sequence The [TextLine.sequence] of the line this was reported for.
 * @property closed The text is no longer on screen; the other fields are empty.
 * @property spaceWidth The width of the whole picture in the units of the rectangles: a game
 * lays its screen out in units of its own (often 1280x720 or 1920x1080), whatever it is drawn
 * at.
 * @property box The text box as a whole, when the profile knows it.
 * @property cells One entry per Unicode code point of the line's text: the cell the character
 * occupies, or null where nothing is drawn (a line break, a character not yet typed out).
 * @property screen [SCREEN_GAME], or [SCREEN_LOWER] for the lower screen of a console that has
 * one.
 */
data class ScreenLayout(
    val window: String,
    val sequence: Long,
    val closed: Boolean,
    val spaceWidth: Float,
    val spaceHeight: Float,
    val box: Cell?,
    val cells: List<Cell?>,
    val screen: Int = SCREEN_GAME
) {
    /** A rectangle in the layout's units. */
    data class Cell(val x: Float, val y: Float, val width: Float, val height: Float) {
        val right: Float get() = x + width
        val bottom: Float get() = y + height
    }

    companion object {
        /** The game's picture: the console's screen, or the upper one of two. */
        const val SCREEN_GAME = 0

        /** The lower screen of a console that has two (the engine's "screen":1). */
        const val SCREEN_LOWER = 1

        /**
         * The trackpad: wherever the cursor is steered from. The second display of a device
         * that has one, where the console has one screen; the lower screen, where it has two.
         * No text is ever on it as such.
         */
        const val SCREEN_PAD = -1

        const val DEFAULT_WIDTH = 1280f
        const val DEFAULT_HEIGHT = 720f

        /** Parses the engine's layout JSON. Returns null when it is not a layout. */
        fun fromJson(json: String): ScreenLayout? {
            val root = try {
                JSONObject(json)
            } catch (_: Exception) {
                return null
            }
            val window = root.optString("window")
            if (window.isEmpty()) return null
            val sequence = root.optLong("seq")
            if (root.optBoolean("closed")) {
                return ScreenLayout(window, sequence, true, DEFAULT_WIDTH, DEFAULT_HEIGHT, null, emptyList())
            }
            val glyphs = root.optJSONArray("glyphs") ?: return null
            val cells = ArrayList<Cell?>(glyphs.length())
            for (i in 0 until glyphs.length()) cells.add(cellFrom(glyphs.optJSONArray(i)))
            val space = root.optJSONArray("space")
            val width = space?.optDouble(0)?.toFloat()?.takeIf { it.isFinite() && it >= 1f } ?: DEFAULT_WIDTH
            val height = space?.optDouble(1)?.toFloat()?.takeIf { it.isFinite() && it >= 1f } ?: DEFAULT_HEIGHT
            return ScreenLayout(
                window = window,
                sequence = sequence,
                closed = false,
                spaceWidth = width,
                spaceHeight = height,
                box = cellFrom(root.optJSONArray("box")),
                cells = cells,
                screen = if (root.optInt("screen") == SCREEN_LOWER) SCREEN_LOWER else SCREEN_GAME
            )
        }

        private fun cellFrom(array: JSONArray?): Cell? {
            if (array == null || array.length() < 4) return null
            val values = FloatArray(4)
            for (i in 0 until 4) {
                val value = array.optDouble(i)
                if (value.isNaN() || value.isInfinite()) return null
                values[i] = value.toFloat()
            }
            if (values[2] < 0f || values[3] < 0f) return null
            return Cell(values[0], values[1], values[2], values[3])
        }
    }
}
