// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.companion

import android.content.Context
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * Titles another app plays (GameNative, Winlator, a game's own app...), added on the page's
 * *Add a title*: each stands on the shelf as a console's game does, and is read by OCR of the
 * screen while Furoshiki keeps the second display ([CompanionActivity]). Kept in the page's
 * process's files as the games' lists are (`titles-app.json`), in the same form.
 */
object AppTitles {
    /** The console key these titles have on the page. */
    const val CONSOLE = "app"

    /** Their system's name, which the page groups them under. */
    const val SYSTEM = "Another app"

    private const val PREFIX = "app:"

    private fun file(context: Context) = File(context.filesDir, "titles-app.json")

    fun list(context: Context): JSONArray =
        runCatching { JSONArray(file(context).readText()) }.getOrDefault(JSONArray())

    /** A new title, played by [app] ([label] its name) from its [activity]; returns its entry. */
    fun add(context: Context, title: String, app: String, activity: String, label: String): JSONObject {
        val list = list(context)
        val id = idOf(title, list)
        val entry = JSONObject()
            .put("id", id)
            .put("title", title)
            .put("path", PREFIX + app + "/" + activity + "#" + id)
            .put("script", false)
            .put("console", CONSOLE)
            .put("system", SYSTEM)
            .put("short", "App")
            .put("via", label)
            .put("added", System.currentTimeMillis())
        list.put(entry)
        file(context).writeText(list.toString())
        return entry
    }

    fun remove(context: Context, id: String) {
        val list = list(context)
        val kept = JSONArray()
        for (i in 0 until list.length()) {
            val one = list.getJSONObject(i)
            if (one.optString("id") != id) kept.put(one)
        }
        file(context).writeText(kept.toString())
    }

    /** The app and activity a title's path names, or null for a path that is no app title's. */
    fun appOf(path: String): Pair<String, String>? {
        if (!path.startsWith(PREFIX)) return null
        val body = path.removePrefix(PREFIX).substringBefore('#')
        val app = body.substringBefore('/')
        val activity = body.substringAfter('/', "")
        if (app.isEmpty() || activity.isEmpty()) return null
        return app to activity
    }

    /**
     * An ID of its own, as a game's is (its reading figures, its OCR areas and its cards are kept
     * under it): APP- and the title's letters and digits, made unique among the others.
     */
    fun idOf(title: String, existing: JSONArray): String {
        val letters = title.uppercase().filter { it in 'A'..'Z' || it in '0'..'9' }.take(12)
        val base = "APP-" + letters.ifEmpty { Integer.toHexString(title.hashCode()).uppercase() }
        val taken = (0 until existing.length()).map { existing.getJSONObject(it).optString("id") }.toSet()
        if (base !in taken) return base
        var n = 2
        while ("$base-$n" in taken) n++
        return "$base-$n"
    }
}
