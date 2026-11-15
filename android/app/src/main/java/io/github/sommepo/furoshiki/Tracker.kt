// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import org.json.JSONObject
import java.io.File
import kotlin.concurrent.thread

/**
 * The study tracker's figures of the titles (the desktop's `HomeWindow::TrackerAsk`,
 * home/docs/furoshiki-3.md, 3): how much of a title was read on every device, its whole length,
 * time and pace, Jiten's difficulty and the share known. Its `GET /api/media`, and only ever a
 * GET, as the page comes to the front and no more often than every five minutes; what it said
 * last is kept (`files/tracker.json`) and shown until it answers again. The address is the one
 * typed on the Settings page (`tracker.url` among the settings for playing), else the one the
 * paired save sync service names ([Services]). The dev line's.
 */
object Tracker {
    private const val EVERY_MS = 5 * 60_000L
    private const val AGAIN_MS = 60_000L
    const val KEY_URL = "tracker.url"

    @Volatile
    private var asking = false

    @Volatile
    private var tried = 0L

    private fun file(context: Context) = File(context.filesDir, "tracker.json")

    /** The address typed by hand, or empty. */
    fun typed(context: Context): String = Places.playSettings(context).raw(KEY_URL).trim().trimEnd('/')

    fun address(context: Context): String = typed(context).ifEmpty { Services.of(context, "tracker") }

    /** What the tracker said last: `{"at", "address", "jiten": {deck: figures}, "title": {originalTitle: figures}}`, or empty. */
    fun kept(context: Context): String = runCatching { file(context).readText() }.getOrDefault("")

    /** Forgets what was said (another address was typed). */
    fun forget(context: Context) {
        file(context).delete()
        tried = 0
    }

    /** One item of the tracker's list as the page wants it (the desktop's TrackerTitle). */
    fun figures(item: JSONObject): JSONObject = JSONObject()
        .put("read", item.optLong("characterProgress"))
        .put("total", item.optLong("characterCount"))
        .put("unique", item.optInt("uniqueWordCount"))
        .put("speed", item.optInt("readingSpeed"))
        .put("sessions", item.optInt("sessions"))
        .put("difficulty", item.optDouble("difficulty", 0.0).let { if (it.isNaN()) 0.0 else it })
        .put("coverage", (item.optJSONObject("jiten")?.optDouble("coverage", 0.0) ?: 0.0).let { if (it.isNaN()) 0.0 else it })
        .put("hours", item.optDouble("immersionHours", 0.0).let { if (it.isNaN()) 0.0 else it })
        .put("last", if (item.isNull("lastLoggedOn")) "" else item.optString("lastLoggedOn"))

    /** The tracker's whole answer as what is kept; null when it lists nothing. */
    fun read(answer: String, address: String, now: Long): JSONObject? {
        val items = JSONObject(answer).optJSONArray("items") ?: return null
        if (items.length() == 0) return null
        val byDeck = JSONObject()
        val byTitle = JSONObject()
        for (i in 0 until items.length()) {
            val item = items.getJSONObject(i)
            val one = figures(item)
            // A title the tracker has twice (an anime and its manga) is told apart by its deck.
            val deck = item.optLong("jitenDeckId")
            if (deck != 0L) byDeck.put(deck.toString(), one)
            byTitle.put(item.optString("originalTitle"), one)
        }
        return JSONObject().put("at", now).put("address", address).put("jiten", byDeck).put("title", byTitle)
    }

    /** Asks when it is time to (or [now]); [changed] is told, on that thread, when there is something new to show. */
    fun ask(context: Context, now: Boolean = false, changed: () -> Unit) {
        val app = context.applicationContext
        val clock = System.currentTimeMillis()
        if (asking) return
        if (!now && (clock - file(app).lastModified() < EVERY_MS || clock - tried < AGAIN_MS)) return
        asking = true
        tried = clock
        thread(name = "Tracker") {
            val fresh = runCatching {
                if (typed(app).isEmpty()) Services.ask(app)
                val at = address(app)
                if (at.isEmpty()) return@runCatching false
                val kept = read(Http.get("$at/api/media"), at, System.currentTimeMillis()) ?: return@runCatching false
                val next = File(file(app).path + ".new")
                next.writeText(kept.toString())
                next.renameTo(file(app))
            }.getOrDefault(false)
            asking = false
            if (fresh) changed()
        }
    }
}
