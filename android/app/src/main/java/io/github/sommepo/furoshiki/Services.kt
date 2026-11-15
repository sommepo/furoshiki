// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/** A request and its answer as text; anything but a 2xx is an error. Never on the main thread. */
object Http {
    fun get(address: String, bearer: String = "", timeoutMs: Int = 15000): String = ask(address, null, bearer, timeoutMs)

    fun post(address: String, body: String, bearer: String = "", timeoutMs: Int = 20000): String = ask(address, body, bearer, timeoutMs)

    private fun ask(address: String, body: String?, bearer: String, timeoutMs: Int): String {
        val connection = URL(address).openConnection() as HttpURLConnection
        connection.connectTimeout = 6000
        connection.readTimeout = timeoutMs
        if (bearer.isNotEmpty()) connection.setRequestProperty("Authorization", "Bearer $bearer")
        try {
            if (body != null) {
                connection.requestMethod = "POST"
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toByteArray()) }
            }
            if (connection.responseCode !in 200..299) error("HTTP ${connection.responseCode}")
            return connection.inputStream.bufferedReader().use { it.readText() }
        } finally {
            connection.disconnect()
        }
    }
}

/**
 * The user's other services, as the save sync service this device is paired with names them
 * (docs/pairing.md, "The other services": `tracker`, `furochibi`, `guides`, `files`, `anki`), the
 * desktop's `home/src/services.*`. Asked with a GET of `/v1/pair` and kept in a file of the app's,
 * which both of its processes read: the page's (the tracker's figures) and a game's (Explain).
 * A device that is not paired knows none.
 */
object Services {
    private fun file(context: Context) = File(context.filesDir, "services.json")

    /** The address of the service of that name, without a slash at its end; empty when none is known. */
    fun of(context: Context, name: String): String =
        runCatching { JSONObject(file(context).readText()).optString(name) }.getOrDefault("").trim().trimEnd('/')

    /**
     * The service's statistics (`GET /v1/stats`: `devices[{device|app, days[{game, day, chars}]}]`) as
     * `{game: {device, day}}`: the device that read each game last, this one ([own], its name there) left out.
     */
    fun readElsewhere(stats: String, own: String): JSONObject {
        val out = JSONObject()
        val devices = JSONObject(stats).optJSONArray("devices") ?: return out
        for (i in 0 until devices.length()) {
            val record = devices.getJSONObject(i)
            val device = record.optString("device").trim().ifEmpty { record.optString("app") }
            if (device.equals(own.trim(), ignoreCase = true)) continue
            val days = record.optJSONArray("days") ?: continue
            for (d in 0 until days.length()) {
                val day = days.getJSONObject(d)
                val game = day.optString("game")
                val on = day.optString("day")
                if (game.isEmpty() || on.isEmpty() || day.optDouble("chars", 0.0) <= 0) continue
                if (!out.has(game) || on > out.getJSONObject(game).getString("day")) out.put(game, JSONObject().put("device", device).put("day", on))
            }
        }
        return out
    }

    /** The Files page's index as `{version, url}` of this app's build there (`furoshiki-android`); empty when it has none. */
    fun readUpdate(index: String, files: String): JSONObject {
        val items = JSONObject(index).optJSONArray("items") ?: return JSONObject()
        for (i in 0 until items.length()) {
            val item = items.getJSONObject(i)
            if (item.optString("id") == "furoshiki-android") {
                return JSONObject().put("version", item.optString("version")).put("url", files + item.optString("url")).put("file", item.optString("file"))
            }
        }
        return JSONObject()
    }

    /** Whether version [there] is newer than [here], by their numbers ("0.3.0" after "0.2.9", "0.10" after "0.9"). */
    fun newer(there: String, here: String): Boolean {
        val a = there.split('.').map { it.toIntOrNull() ?: 0 }
        val b = here.split('.').map { it.toIntOrNull() ?: 0 }
        for (i in 0 until maxOf(a.size, b.size)) {
            val x = a.getOrElse(i) { 0 }
            val y = b.getOrElse(i) { 0 }
            if (x != y) return x > y
        }
        return false
    }

    /** Asks the paired service which services there are. True when it answered. Never on the main thread. */
    fun ask(context: Context): Boolean {
        return false
    }
}
