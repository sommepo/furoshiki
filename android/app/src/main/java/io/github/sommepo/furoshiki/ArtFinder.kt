// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.text.Normalizer
import org.json.JSONArray
import org.json.JSONObject

/**
 * A game's box front and spine from ScreenScraper and VNDB, each where the user has it on
 * (ArtSources): the desktop's way (`home/src/art_fetch.cpp`), on a worker thread. ScreenScraper:
 * the game of that console by its serial, else by its exact name, its Japanese box first. VNDB:
 * the entry named exactly so (or the title record's), a release on that console, Japanese first,
 * no picture VNDB marks as sexual. Never a looser match: another game's box is worse than none.
 */
object ArtFinder {
    private const val TIMEOUT_MS = 15_000
    private const val VNDB = "https://api.vndb.org/kana/"
    private const val SCREENSCRAPER = "https://api.screenscraper.fr/api2/"

    /** VNDB's name of each console, by the app's key. */
    private val VNDB_PLATFORM = mapOf(
        "psp" to "psp", "ps1" to "ps1", "ps2" to "ps2", "psvita" to "psv", "dc" to "drc", "3ds" to "n3d", "nds" to "nds", "switch" to "swi",
    )
    /** ScreenScraper's numbers of the consoles (tools/titles.py, SCREENSCRAPER_SYSTEMS). */
    private val SS_SYSTEM = mapOf(
        "psp" to 61, "ps1" to 57, "ps2" to 58, "psvita" to 62, "dc" to 23, "3ds" to 17, "switch" to 225, "nds" to 15,
    )
    private val REGIONS = listOf("jp", "wor", "ss", "us", "eu")

    data class Want(
        val console: String,
        val id: String,
        val title: String,
        val english: String = "",
        val vndb: String = "",
        val cover: File?, ///< Where a cover goes; null: none wanted
        val spine: File?, ///< Where a spine goes; null: none wanted
    )

    data class Found(val cover: Boolean, val spine: Boolean, val from: List<String>, val problem: String = "")

    fun find(sources: ArtSources, want: Want): Found {
        var cover = want.cover == null
        var spine = want.spine == null
        val from = ArrayList<String>()
        var problem = ""
        if (sources.screenScraperReady && SS_SYSTEM.containsKey(want.console)) {
            runCatching { screenScraperGame(sources, want) }.onSuccess { (game, said) ->
                problem = said
                if (game != null) {
                    if (!cover) best(game, "box-2D")?.let { if (save(it, want.cover!!, false)) { cover = true; from.add("ScreenScraper") } }
                    if (!spine) best(game, "box-2D-side")?.let { if (save(it, want.spine!!, true)) { spine = true; from.add("ScreenScraper spine") } }
                }
            }
        }
        if (sources.vndb && (!cover || !spine) && VNDB_PLATFORM.containsKey(want.console)) {
            runCatching {
                val vn = want.vndb.ifEmpty { vndbEntry(want) }
                if (vn.isNotEmpty()) {
                    val (front, side) = vndbPictures(vn, VNDB_PLATFORM.getValue(want.console))
                    if (!cover && front.isNotEmpty() && save(front, want.cover!!, false)) { cover = true; from.add("VNDB") }
                    if (!spine && side.isNotEmpty() && save(side, want.spine!!, true)) { spine = true; from.add("VNDB spine") }
                }
            }.onFailure { Log.i("Furoshiki", "VNDB: ${it.message}") }
        }
        return Found(want.cover != null && cover, want.spine != null && spine, from, problem)
    }

    /** Letters and figures only, folded: names compared as tools/titles.py compares them. */
    fun plainName(text: String): String =
        Normalizer.normalize(text, Normalizer.Form.NFKC).lowercase().filter { it.isLetterOrDigit() }

    private fun plainSerial(text: String): String = text.uppercase().filter { it in '0'..'9' || it in 'A'..'Z' }

    private fun ssUrl(sources: ArtSources, page: String, query: Map<String, String>): String {
        val all = query + mapOf(
            "devid" to sources.ssDevId, "devpassword" to sources.ssDevPassword, "softname" to "Furoshiki", "output" to "json",
            "ssid" to sources.ssUser, "sspassword" to sources.ssPassword,
        )
        return SCREENSCRAPER + page + "?" + all.entries.joinToString("&") { "${it.key}=${Uri.encode(it.value)}" }
    }

    /** The game, or null; and what ScreenScraper said where it refused (a login). */
    private fun screenScraperGame(sources: ArtSources, want: Want): Pair<JSONObject?, String> {
        val system = SS_SYSTEM.getValue(want.console)
        if (want.console != "switch" && want.id.isNotEmpty() && !want.id.startsWith("LINES-")) {
            val (code, body) = get(ssUrl(sources, "jeuInfos.php", mapOf(
                "systemeid" to system.toString(), "romtype" to "iso", "romnom" to "${want.id}.iso", "serialnum" to want.id,
            )))
            val text = body.decodeToString()
            if (code != 200 && code != 404 && !text.trimStart().startsWith("{")) return null to screenScraperError(code, text)
            val game = runCatching { JSONObject(text).getJSONObject("response").getJSONObject("jeu") }.getOrNull()
            if (game != null && game.optJSONObject("systeme")?.optInt("id") == system) {
                // Its own serial among the game's roms': never another game.
                val wanted = plainSerial(want.id)
                val roms = (game.optJSONArray("roms") ?: JSONArray()).let { list -> List(list.length()) { list.optJSONObject(it) } } +
                    listOfNotNull(game.optJSONObject("rom"))
                val same = roms.filterNotNull().any { rom ->
                    plainSerial(rom.optString("romserial")) == wanted ||
                        (rom.optJSONArray("serials") ?: JSONArray()).let { s -> (0 until s.length()).any { plainSerial(s.optString(it)) == wanted } }
                }
                if (same) return game to ""
            }
        }
        val names = setOf(plainName(want.title), plainName(want.english)) - ""
        if (names.isEmpty()) return null to ""
        for (search in listOf(want.title, want.english).filter { it.isNotBlank() }) {
            val (code, body) = get(ssUrl(sources, "jeuRecherche.php", mapOf("systemeid" to system.toString(), "recherche" to search)))
            val text = body.decodeToString()
            if (code != 200 && code != 404 && !text.trimStart().startsWith("{")) return null to screenScraperError(code, text)
            val games = runCatching { JSONObject(text).getJSONObject("response").getJSONArray("jeux") }.getOrNull() ?: continue
            for (i in 0 until games.length()) {
                val game = games.optJSONObject(i) ?: continue
                if (game.optJSONObject("systeme")?.optInt("id") != system) continue
                val its = game.optJSONArray("noms") ?: continue
                if ((0 until its.length()).any { plainName(its.optJSONObject(it)?.optString("text").orEmpty()) in names }) return game to ""
            }
        }
        return null to ""
    }

    private fun screenScraperError(code: Int, text: String): String {
        val clean = text.replace(Regex("<[^>]+>"), " ").replace(Regex("\\s+"), " ").trim().take(120)
        return if (clean.isNotEmpty()) "ScreenScraper: $clean" else "ScreenScraper returned HTTP $code"
    }

    private fun best(game: JSONObject, kind: String): String? {
        val medias = game.optJSONArray("medias") ?: return null
        var url: String? = null
        var rank = 99
        for (i in 0 until medias.length()) {
            val media = medias.optJSONObject(i) ?: continue
            if (media.optString("type") != kind || media.optString("url").isEmpty()) continue
            val at = REGIONS.indexOf(media.optString("region")).let { if (it < 0) 50 else it }
            if (at < rank) {
                rank = at
                url = media.optString("url")
            }
        }
        return url
    }

    /** The VNDB entry named exactly as the game (its own name, an alias, its English one). */
    private fun vndbEntry(want: Want): String {
        val wanted = setOf(plainName(want.title), plainName(want.english)) - ""
        val search = want.title.ifBlank { want.english }
        if (wanted.isEmpty() || search.isBlank()) return ""
        val query = JSONObject().put("filters", JSONArray().put("search").put("=").put(search))
            .put("fields", "id,title,alttitle,aliases,titles.title").put("results", 20)
        val results = JSONObject(post(VNDB + "vn", query.toString())).optJSONArray("results") ?: return ""
        for (i in 0 until results.length()) {
            val entry = results.optJSONObject(i) ?: continue
            val names = mutableListOf(entry.optString("title"), entry.optString("alttitle"))
            entry.optJSONArray("aliases")?.let { a -> for (j in 0 until a.length()) names.add(a.optString(j)) }
            entry.optJSONArray("titles")?.let { t -> for (j in 0 until t.length()) names.add(t.optJSONObject(j)?.optString("title").orEmpty()) }
            if (names.any { it.isNotEmpty() && plainName(it) in wanted }) return entry.optString("id")
        }
        return ""
    }

    /** The box front and spine of the entry's releases on that console: Japanese first, then one of that console alone. */
    private fun vndbPictures(vn: String, platform: String): Pair<String, String> {
        val query = JSONObject()
            .put("filters", JSONArray().put("and").put(JSONArray().put("vn").put("=").put(JSONArray().put("id").put("=").put(vn)))
                .put(JSONArray().put("platform").put("=").put(platform)))
            .put("fields", "languages.lang,platforms,images{type,url,dims,sexual}").put("results", 100)
        val results = JSONObject(post(VNDB + "release", query.toString())).optJSONArray("results") ?: return "" to ""
        var front = ""
        var side = ""
        var frontRank = -1
        var sideRank = -1
        for (i in 0 until results.length()) {
            val release = results.optJSONObject(i) ?: continue
            val platforms = release.optJSONArray("platforms") ?: continue
            if ((0 until platforms.length()).none { platforms.optString(it) == platform }) continue
            val japanese = release.optJSONArray("languages")?.let { l -> (0 until l.length()).any { l.optJSONObject(it)?.optString("lang") == "ja" } } == true
            val rank = (if (japanese) 2 else 0) + (if (platforms.length() == 1) 1 else 0)
            val images = release.optJSONArray("images") ?: continue
            for (j in 0 until images.length()) {
                val image = images.optJSONObject(j) ?: continue
                if (image.optDouble("sexual", 0.0) >= 1) continue
                val dims = image.optJSONArray("dims")
                when (image.optString("type")) {
                    "pkgfront" -> if (rank > frontRank) { front = image.optString("url"); frontRank = rank }
                    // A spine stands at least four times as tall as it is wide.
                    "pkgside" -> if (dims != null && dims.length() == 2 && dims.optInt(1) >= 4 * maxOf(1, dims.optInt(0)) && rank > sideRank) {
                        side = image.optString("url"); sideRank = rank
                    }
                }
            }
        }
        return front to side
    }

    private fun connection(address: String): HttpURLConnection =
        (URL(address).openConnection() as HttpURLConnection).apply {
            connectTimeout = TIMEOUT_MS
            readTimeout = TIMEOUT_MS
            setRequestProperty("User-Agent", "Furoshiki")
        }

    private const val MAX_BYTES = 20 * 1024 * 1024

    private fun readLimited(stream: java.io.InputStream): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        val buf = ByteArray(16 * 1024)
        var total = 0
        while (true) {
            val n = stream.read(buf)
            if (n < 0) break
            total += n
            if (total > MAX_BYTES) throw java.io.IOException("Response too large")
            out.write(buf, 0, n)
        }
        return out.toByteArray()
    }

    private fun get(address: String): Pair<Int, ByteArray> {
        val parsed = URL(address)
        if (parsed.protocol != "https" && parsed.protocol != "http") throw java.io.IOException("Unsupported URL scheme")
        val c = connection(address)
        try {
            val code = c.responseCode
            val stream = if (code in 200..299) c.inputStream else c.errorStream
            return code to (stream?.use { readLimited(it) } ?: ByteArray(0))
        } finally {
            c.disconnect()
        }
    }

    private fun post(address: String, json: String): String {
        val c = connection(address)
        try {
            c.requestMethod = "POST"
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(json.toByteArray()) }
            return c.inputStream.use { readLimited(it).decodeToString() }
        } finally {
            c.disconnect()
        }
    }

    /** A picture fetched, checked to be one, at most 1400 high (a cover) or 1000 (a spine), as JPEG. */
    private fun save(address: String, to: File, spine: Boolean): Boolean {
        val partial = File(to.path + ".new")
        return try {
            val (code, bytes) = get(address)
            val picture = if (code == 200) BitmapFactory.decodeByteArray(bytes, 0, bytes.size) else null
            if (picture == null || picture.width < 20 || picture.height < 20) {
                picture?.recycle()
                false
            } else {
                var scaled: Bitmap? = null
                try {
                    val most = if (spine) 1000 else 1400
                    scaled = if (picture.height > most) {
                        val wide = maxOf(1, (picture.width.toLong() * most / picture.height).toInt())
                        Bitmap.createScaledBitmap(picture, wide, most, true)
                    } else {
                        picture
                    }
                    to.parentFile?.mkdirs()
                    val written = partial.outputStream().use { scaled.compress(Bitmap.CompressFormat.JPEG, 90, it) }
                    if (written && partial.renameTo(to)) {
                        true
                    } else {
                        partial.delete()
                        false
                    }
                } finally {
                    if (scaled != null && scaled !== picture) scaled.recycle()
                    picture.recycle()
                }
            }
        } catch (e: Exception) {
            partial.delete()
            Log.i("Furoshiki", "No picture from ${address.substringBefore('?')}: ${e.message}")
            false
        }
    }
}
