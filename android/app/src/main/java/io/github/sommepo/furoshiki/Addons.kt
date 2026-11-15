// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * Game add-ons (the desktop's `home/src/addons.*`, home/AGENTS.md, "Game add-ons"): a folder
 * with an `addon.json`, here in a folder `Addons` beside the games (`Games/Addons/<name>`). When
 * a game of an add-on starts, the files of its `scripts` folder go into the user's scripts
 * where they differ (the user's own file of that name is kept once as `NAME.before-addon`), and
 * its `settings` go into that game's layer, each once: one the game has already is left, one
 * the user changes or takes out later stays so. What was put where is kept in [record]
 * (`files/addons.json`, the desktop's `program/addons.json`).
 *
 * **Not here**: an add-on's companion. It is a program run beside Furoshiki on a computer and
 * talks to it through a socket of the desktop's reader; Android runs no such program.
 */
class Addons(private val record: File, private val scripts: File, private val play: PlaySettings) {
    class Addon(val name: String, val folder: File, val console: String, val games: List<String>, val scripts: String, val settings: Map<String, String>)

    private fun kept(): JSONObject = runCatching { JSONObject(record.readText()) }.getOrDefault(JSONObject())

    /** Does for a starting game what its add-ons ask. Returns the names of those that were its. */
    fun apply(found: List<Addon>, console: String, game: String): List<String> {
        val mine = found.filter { game in it.games && (it.console.isEmpty() || it.console == console) }
        if (mine.isEmpty()) return emptyList()
        val kept = kept()
        val owned = kept.optJSONObject("scripts") ?: JSONObject().also { kept.put("scripts", it) }
        val set = kept.optJSONObject("settings") ?: JSONObject().also { kept.put("settings", it) }
        for (addon in mine) {
            // Its scripts, where they differ from what is there.
            val from = File(addon.folder, addon.scripts)
            if (addon.scripts.isNotEmpty() && from.isDirectory) {
                scripts.mkdirs()
                for (file in from.listFiles().orEmpty().filter { it.isFile }) {
                    val to = File(scripts, file.name)
                    if (to.isFile && to.readBytes().contentEquals(file.readBytes())) continue
                    // The user's own file of that name (none an add-on put there): kept, once.
                    val aside = File(scripts, file.name + ".before-addon")
                    if (to.isFile && !owned.has(file.name) && !aside.exists()) to.copyTo(aside)
                    file.copyTo(to, overwrite = true)
                    owned.put(file.name, addon.name)
                }
            }
            // Its settings, each once in this game's layer.
            val games = set.optJSONObject(addon.name) ?: JSONObject().also { set.put(addon.name, it) }
            val done = games.optJSONArray(game) ?: JSONArray().also { games.put(game, it) }
            val had = (0 until done.length()).map { done.getString(it) }.toSet()
            for ((key, value) in addon.settings) {
                if (key in had) continue
                if (play.raw(key, console, game).isEmpty()) play.set(key, value, console, game)
                done.put(key)
            }
        }
        record.writeText(kept.put("format", 1).toString())
        return mine.map { it.name }
    }

    companion object {
        /** The add-ons in the folders `Addons` of [roots]: each folder with an `addon.json` that names its games. */
        fun find(roots: List<File>): List<Addon> {
            val found = ArrayList<Addon>()
            for (root in roots) {
                for (folder in File(root, "Addons").listFiles().orEmpty().filter { it.isDirectory }.sortedBy { it.name }) {
                    val about = runCatching { JSONObject(File(folder, "addon.json").readText()) }.getOrNull() ?: continue
                    val games = about.optJSONArray("games") ?: continue
                    val name = about.optString("name").ifEmpty { folder.name }
                    if (about.optInt("format", 1) > 1 || games.length() == 0 || found.any { it.name == name }) continue
                    val settings = about.optJSONObject("settings") ?: JSONObject()
                    found.add(
                        Addon(
                            name, folder, about.optString("console"), List(games.length()) { games.getString(it) },
                            about.optString("scripts"), settings.keys().asSequence().associateWith { settings.optString(it) }
                        )
                    )
                }
            }
            return found
        }
    }
}
