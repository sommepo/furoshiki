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

    /** The titles found in the game folders' exports ([FrontendExports]), as the last look found them. */
    private fun exportedFile(context: Context) = File(context.filesDir, "titles-exported.json")

    /** The exported titles taken off the shelf: not put back by the next look. */
    private fun hiddenFile(context: Context) = File(context.filesDir, "titles-exported-hidden.json")

    private fun read(file: File): JSONArray = runCatching { JSONArray(file.readText()) }.getOrDefault(JSONArray())

    /** The titles added by hand. */
    private fun added(context: Context): JSONArray = read(file(context))

    /** Every title: those added by hand, then those another app exported. */
    fun list(context: Context): JSONArray {
        val all = added(context)
        val exported = read(exportedFile(context))
        for (i in 0 until exported.length()) all.put(exported.get(i))
        return all
    }

    /** The title [id], or null. */
    fun find(context: Context, id: String): JSONObject? {
        val all = list(context)
        return (0 until all.length()).map { all.getJSONObject(it) }.firstOrNull { it.optString("id") == id }
    }

    /** What starts title [id]'s game itself, where its app exported it; null: the app's own start. */
    fun launchOf(context: Context, id: String): FrontendExports.Launch? =
        FrontendExports.Launch.of(find(context, id)?.optJSONObject("launch"))

    /** A new title, played by [app] ([label] its name) from its [activity]; returns its entry. */
    fun add(context: Context, title: String, app: String, activity: String, label: String): JSONObject {
        val list = added(context)
        val id = idOf(title, list(context))
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
        val exported = read(exportedFile(context))
        if ((0 until exported.length()).any { exported.getJSONObject(it).optString("id") == id }) {
            val hidden = read(hiddenFile(context))
            hidden.put(id)
            hiddenFile(context).writeText(hidden.toString())
            exportedFile(context).writeText(without(exported, id).toString())
            return
        }
        file(context).writeText(without(added(context), id).toString())
    }

    private fun without(list: JSONArray, id: String): JSONArray {
        val kept = JSONArray()
        for (i in 0 until list.length()) {
            val one = list.getJSONObject(i)
            if (one.optString("id") != id) kept.put(one)
        }
        return kept
    }

    // ---- The games other apps export for launchers ----

    /** How deep below a game folder exports are looked for (`ROMs/steam/<name>.steam`). */
    private const val DEPTH = 3

    /** A file worth reading is small: an export is a number or a few lines. */
    private const val LARGEST = 16 * 1024L

    /**
     * Looks through [folders] for the files GameNative's *Frontend Sync* and Winlator's shortcuts
     * leave, and keeps what it found as titles; true when the shelf changed. On the scan's thread.
     */
    fun importExports(context: Context, folders: List<File>): Boolean {
        val files = ArrayList<FoundFile>()
        val seen = HashSet<String>()
        fun walk(dir: File, depth: Int) {
            val canonical = runCatching { dir.canonicalPath }.getOrDefault(dir.path)
            if (!seen.add(canonical)) return
            for (one in dir.listFiles().orEmpty().sortedBy { it.name }) {
                if (one.isDirectory) {
                    if (depth < DEPTH && !one.name.startsWith(".")) walk(one, depth + 1)
                } else if (one.extension.lowercase() in FrontendExports.EXTENSIONS && one.length() <= LARGEST) {
                    runCatching { files.add(FoundFile(one.name, one.path, one.readText())) }
                }
            }
        }
        folders.forEach { walk(it, 1) }
        val packages = context.packageManager
        val installed = (FrontendExports.GAMENATIVE + FrontendExports.WINLATOR.map { it.first })
            .filter { runCatching { packages.getPackageInfo(it, 0) }.isSuccess }.toSet()
        val launcherOf = { app: String -> packages.getLaunchIntentForPackage(app)?.component?.className }
        val hidden = read(hiddenFile(context)).let { list -> (0 until list.length()).map { list.getString(it) }.toSet() }
        val before = read(exportedFile(context))
        val found = exportedEntries(files, installed, launcherOf, hidden, before)
        if (found.toString() == before.toString()) return false
        exportedFile(context).writeText(found.toString())
        return true
    }

    /** A file found by [importExports]: its [name], [path] and what it holds. */
    data class FoundFile(val name: String, val path: String, val text: String)

    /**
     * The titles [files] make, each once (the first file of an ID), but those [hidden]; each keeps
     * when it was first found ([before]; [now] for a new one). [launcherOf] gives an app's own
     * launcher activity, which brings it back to the front ([CompanionActivity]'s `frontGame`).
     */
    fun exportedEntries(
        files: List<FoundFile>,
        installed: Set<String>,
        launcherOf: (String) -> String?,
        hidden: Set<String>,
        before: JSONArray = JSONArray(),
        now: Long = System.currentTimeMillis(),
    ): JSONArray {
        val added = (0 until before.length()).map { before.getJSONObject(it) }
            .associate { it.optString("id") to it.optLong("added") }
        val list = JSONArray()
        val ids = HashSet<String>()
        for (file in files) {
            val export = FrontendExports.read(file.name, file.path, file.text) ?: continue
            val launch = FrontendExports.launchOf(export, installed) ?: continue
            val id = FrontendExports.idOf(export)
            if (id in hidden || !ids.add(id)) continue
            val front = launcherOf(launch.app) ?: launch.activity
            list.put(
                JSONObject()
                    .put("id", id)
                    .put("title", export.title)
                    .put("path", PREFIX + launch.app + "/" + front + "#" + id)
                    .put("script", false)
                    .put("console", CONSOLE)
                    .put("system", SYSTEM)
                    .put("short", "App")
                    .put("via", FrontendExports.labelOf(export))
                    .put("exported", file.path)
                    .put("launch", launch.toJson())
                    .put("added", added[id] ?: now)
            )
        }
        return list
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
