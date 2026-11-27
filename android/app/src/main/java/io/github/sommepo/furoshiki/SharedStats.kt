// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.os.Build
import android.os.Environment
import java.io.File
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject

/**
 * Reading statistics shared between devices through a folder the user keeps in sync themself
 * (Syncthing, for one). Each device writes one file of its own, `<folder>/<device id>.json`, and
 * reads every other device's to add its reading to the Statistics page. No two devices write the
 * same file, so a sync tool never has two versions of one file to choose between. The goals are
 * `goals.json`, the newest written wins. The desktop program writes and reads the same files
 * (`home/src/shared_stats.h` has the format).
 */
object SharedStats {
    private const val FORMAT = "furoshiki.stats"
    private const val KEY_FOLDER = "stats_folder"
    private const val KEY_DEVICE = "stats_device_id"
    private const val KEY_GOALS_WRITTEN = "goals_written"

    /** One day of one game's reading on another device. */
    data class Day(val day: String, val game: String, val title: String, val chars: Long, val activeMs: Long)

    private fun prefs(context: Context) = context.getSharedPreferences("home", Context.MODE_PRIVATE)

    /** The folder chosen on Settings, else Furoshiki/Stats on the device's shared storage. */
    fun folder(context: Context): File {
        val chosen = prefs(context).getString(KEY_FOLDER, "").orEmpty().trim()
        return if (chosen.isNotEmpty()) File(chosen) else File(Environment.getExternalStorageDirectory(), "Furoshiki/Stats")
    }

    fun setFolder(context: Context, path: String) = prefs(context).edit().putString(KEY_FOLDER, path.trim()).apply()

    fun deviceId(context: Context): String {
        val kept = prefs(context).getString(KEY_DEVICE, "").orEmpty()
        if (kept.isNotEmpty()) return kept
        val made = UUID.randomUUID().toString()
        prefs(context).edit().putString(KEY_DEVICE, made).apply()
        return made
    }

    /** This device's reading as a file, from the database's own record (`ReaderDatabase.statsRecord`). */
    fun record(context: Context, own: JSONObject): JSONObject {
        val days = JSONArray()
        val source = own.optJSONArray("days") ?: JSONArray()
        for (i in 0 until source.length()) {
            val one = source.getJSONObject(i)
            val chars = one.optLong("chars")
            val active = one.optLong("active_ms")
            if (chars <= 0 && active <= 0) continue
            days.put(JSONObject().put("day", one.optString("day")).put("game", one.optString("game"))
                .put("chars", chars).put("active_ms", active))
        }
        return JSONObject().put("format", FORMAT).put("version", 1).put("device", deviceId(context))
            .put("name", Build.MODEL).put("platform", "android")
            .put("games", own.optJSONObject("games") ?: JSONObject()).put("systems", JSONObject()).put("days", days)
    }

    /** Writes the record when it differs from what is there; false when the folder cannot be written. */
    fun writeOwn(context: Context, record: JSONObject): Boolean = runCatching {
        val dir = folder(context)
        if (!dir.isDirectory && !dir.mkdirs()) return false
        val file = File(dir, record.getString("device") + ".json")
        val was = runCatching { JSONObject(file.readText()).apply { remove("written") } }.getOrNull()
        if (was != null && was.toString() == record.toString()) return true
        val out = JSONObject(record.toString()).put("written", System.currentTimeMillis())
        val part = File(dir, file.name + ".part")
        part.writeText(out.toString(1))
        part.renameTo(file)
    }.getOrDefault(false)

    /** Every other device's reading in the folder, and how many devices that is. */
    fun others(context: Context): Pair<List<Day>, Int> {
        val own = deviceId(context)
        val out = ArrayList<Day>()
        var devices = 0
        val files = folder(context).listFiles { file -> file.isFile && file.name.endsWith(".json") } ?: return out to 0
        for (file in files) {
            val root = runCatching { JSONObject(file.readText()) }.getOrNull() ?: continue
            if (root.optString("format") != FORMAT || root.optInt("version") > 1) continue
            val device = root.optString("device")
            if (device.isEmpty() || device == own) continue
            val games = root.optJSONObject("games") ?: JSONObject()
            val days = root.optJSONArray("days") ?: continue
            for (i in 0 until days.length()) {
                val one = days.getJSONObject(i)
                val game = one.optString("game")
                val chars = one.optLong("chars")
                val active = one.optLong("active_ms")
                if (chars <= 0 && active <= 0) continue
                out.add(Day(one.optString("day"), game, games.optString(game, game), chars, active))
            }
            devices++
        }
        return out to devices
    }

    /** The goals in the folder where they are newer than this device's, taken into `home`'s preferences. */
    fun takeGoals(context: Context) = runCatching {
        val root = JSONObject(File(folder(context), "goals.json").readText())
        val written = root.optLong("written")
        if (written <= prefs(context).getLong(KEY_GOALS_WRITTEN, 0)) return@runCatching
        prefs(context).edit().apply {
            for (kind in listOf("daily", "weekly", "monthly", "yearly")) putInt("goal_$kind", root.optInt(kind))
            putLong(KEY_GOALS_WRITTEN, written)
        }.apply()
    }

    /** Every goal of this device into the folder, so the other devices take them too. */
    fun shareGoals(context: Context) = runCatching {
        val now = System.currentTimeMillis()
        val p = prefs(context)
        p.edit().putLong(KEY_GOALS_WRITTEN, now).apply()
        val dir = folder(context).apply { mkdirs() }
        val goals = JSONObject().put("format", "furoshiki.goals").put("written", now)
        for (kind in listOf("daily", "weekly", "monthly", "yearly")) goals.put(kind, p.getInt("goal_$kind", 0))
        val part = File(dir, "goals.json.part")
        part.writeText(goals.toString(1))
        part.renameTo(File(dir, "goals.json"))
    }
}
