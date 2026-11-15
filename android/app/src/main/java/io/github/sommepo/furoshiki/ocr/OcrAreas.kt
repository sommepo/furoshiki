// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * A text area of a game: a box in its picture ([x], [y], [width], [height], 0 to 1 of the
 * picture), read whenever it has changed, or only when asked for ([ask]: a menu, a status page).
 */
data class OcrArea(val x: Float, val y: Float, val width: Float, val height: Float, val ask: Boolean = false) {
    val right: Float get() = x + width
    val bottom: Float get() = y + height

    /** Inside the picture and no smaller than a finger can draw on purpose. */
    fun fitted(): OcrArea {
        val w = width.coerceIn(SMALLEST, 1f)
        val h = height.coerceIn(SMALLEST, 1f)
        return copy(x = x.coerceIn(0f, 1f - w), y = y.coerceIn(0f, 1f - h), width = w, height = h)
    }

    companion object {
        const val SMALLEST = 0.03f

        /** Where most games have their message window: offered where no box was drawn. */
        val LOWER_THIRD = OcrArea(0.04f, 0.64f, 0.92f, 0.33f)
    }
}

/** A game's OCR: whether it is read that way, and its areas. */
data class OcrGame(val on: Boolean = false, val areas: List<OcrArea> = emptyList()) {
    /** The areas read by themselves, from the top: the lower third where none was drawn. */
    val always: List<OcrArea>
        get() = areas.filter { !it.ask }.ifEmpty { if (areas.isEmpty()) listOf(OcrArea.LOWER_THIRD) else emptyList() }
            .sortedWith(compareBy({ it.y }, { it.x }))

    /** The areas read only when asked for. */
    val asked: List<OcrArea> get() = areas.filter { it.ask }.sortedWith(compareBy({ it.y }, { it.x }))
}

/**
 * Every game's text areas, by its ID, in a file of the app's (`files/ocr-areas.json`): kept on
 * the device, written whole each time. Read fresh at each use: the page's process and a game's
 * both may.
 */
class OcrAreas(private val file: File) {
    fun of(gameId: String): OcrGame = parse(text()).optJSONObject(key(gameId))?.let { game(it) } ?: OcrGame()

    fun set(gameId: String, game: OcrGame) {
        if (gameId.isBlank()) return
        val all = parse(text())
        val areas = JSONArray()
        for (area in game.areas.map { it.fitted() }.take(MOST)) {
            val item = JSONObject().put("rect", JSONArray(listOf(round(area.x), round(area.y), round(area.width), round(area.height))))
            if (area.ask) item.put("read", "ask")
            areas.put(item)
        }
        all.put(key(gameId), JSONObject().put("on", game.on).put("areas", areas))
        val next = File(file.path + ".new")
        next.writeText(all.toString())
        if (!next.renameTo(file)) {
            file.writeText(all.toString())
            next.delete()
        }
    }

    /** The games that have a record, for a list of them. */
    fun games(): Set<String> = parse(text()).keys().asSequence().toSet()

    private fun text(): String = runCatching { file.readText() }.getOrDefault("")

    private fun key(gameId: String) = gameId.trim().uppercase()

    private fun round(value: Float): Double = Math.round(value * 10000.0) / 10000.0

    companion object {
        /** More boxes than this in one picture are a mistake. */
        const val MOST = 8

        private fun parse(text: String): JSONObject = runCatching { JSONObject(text) }.getOrDefault(JSONObject())

        private fun game(record: JSONObject): OcrGame {
            val areas = ArrayList<OcrArea>()
            val list = record.optJSONArray("areas") ?: JSONArray()
            for (i in 0 until list.length()) {
                val item = list.optJSONObject(i) ?: continue
                val rect = item.optJSONArray("rect") ?: continue
                if (rect.length() < 4) continue
                val values = FloatArray(4) { rect.optDouble(it, Double.NaN).toFloat() }
                if (values.any { !it.isFinite() } || values[2] <= 0f || values[3] <= 0f) continue
                areas.add(OcrArea(values[0], values[1], values[2], values[3], item.optString("read") == "ask").fitted())
            }
            return OcrGame(record.optBoolean("on"), areas.take(MOST))
        }
    }
}
