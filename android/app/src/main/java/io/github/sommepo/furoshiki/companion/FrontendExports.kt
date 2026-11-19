// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.companion

import org.json.JSONObject

/**
 * The files PC game apps write for launchers ("frontends"), read as titles that start their game
 * at once (`android/AGENTS.md`, "Games another app exports").
 *
 * - **GameNative** (`app.gamenative`), its *Frontend Sync* (Settings › Interface) or a game's
 *   *Export for frontend*: `<name>.steam`, `.epic`, `.gog`, `.amazon` or `.pcgame`, holding the
 *   game's number alone (`Broforce.steam` holds `274190`). Started by the action
 *   `app.gamenative.LAUNCH_GAME` to its `MainActivity`, `app_id` an int, `game_source` the store.
 * - **Winlator** (and its forks, WinNative), its exported shortcuts: a `.desktop` file
 *   (`[Desktop Entry]`, `Name=`, an `Exec=` with wine), started by its `XServerDisplayActivity`
 *   with `shortcut_path` the file's path.
 *
 * Pure (no Android), so that the JVM tests read it.
 */
object FrontendExports {
    /** GameNative's packages: the release, and its "gold" build. */
    val GAMENATIVE = listOf("app.gamenative", "app.gamenative.gold")
    const val GAMENATIVE_ACTION = "app.gamenative.LAUNCH_GAME"
    const val GAMENATIVE_ACTIVITY = "app.gamenative.MainActivity"

    /** The store each of GameNative's extensions names. */
    val STORES = mapOf(
        "steam" to "STEAM", "epic" to "EPIC", "gog" to "GOG", "amazon" to "AMAZON", "pcgame" to "CUSTOM_GAME",
    )

    /** Winlator's packages and the activity a shortcut is started by (ES-DE's find rules). */
    val WINLATOR = listOf(
        "com.winlator.cmod" to "com.winlator.cmod.XServerDisplayActivity",
        "com.winlator" to "com.winlator.XServerDisplayActivity",
        "com.cmodded.winlator" to "com.winlator.XServerDisplayActivity",
        "com.winnative.cmod" to "com.winlator.cmod.runtime.display.XServerDisplayActivity",
    )

    /** The extensions worth reading at all. */
    val EXTENSIONS = STORES.keys + "desktop"

    /** One exported game, as its file says it. */
    sealed class Export {
        abstract val title: String

        /** GameNative's: [store] (STEAM, EPIC...) and the game's [number] there. */
        data class GameNative(override val title: String, val store: String, val number: Int) : Export()

        /** A Winlator shortcut at [path]. */
        data class Winlator(override val title: String, val path: String) : Export()
    }

    /** What starts the game: an activity, an action and its extras. */
    data class Launch(
        val app: String,
        val activity: String,
        val action: String?,
        val ints: Map<String, Int> = emptyMap(),
        val strings: Map<String, String> = emptyMap(),
        /** A new task on its own: the app's old screens cleared (Winlator's way). */
        val clearTask: Boolean = false,
    ) {
        fun toJson(): JSONObject = JSONObject().put("app", app).put("activity", activity)
            .put("action", action ?: JSONObject.NULL)
            .put("ints", JSONObject(ints as Map<*, *>)).put("strings", JSONObject(strings as Map<*, *>))
            .put("clearTask", clearTask)

        companion object {
            fun of(json: JSONObject?): Launch? {
                if (json == null) return null
                val app = json.optString("app")
                val activity = json.optString("activity")
                if (app.isEmpty() || activity.isEmpty()) return null
                val ints = json.optJSONObject("ints")?.let { o -> o.keys().asSequence().associateWith { o.getInt(it) } }.orEmpty()
                val strings = json.optJSONObject("strings")?.let { o -> o.keys().asSequence().associateWith { o.getString(it) } }.orEmpty()
                val action = if (json.isNull("action")) null else json.optString("action").ifEmpty { null }
                return Launch(app, activity, action, ints, strings, json.optBoolean("clearTask"))
            }
        }
    }

    /** The export a file [name] at [path] holding [text] is, or null for any other file. */
    fun read(name: String, path: String, text: String): Export? {
        val dot = name.lastIndexOf('.')
        if (dot <= 0) return null
        val stem = name.substring(0, dot).trim()
        val extension = name.substring(dot + 1).lowercase()
        STORES[extension]?.let { store ->
            // The number alone (GameNative writes no newline; one by hand is let be).
            val number = text.trim().toIntOrNull() ?: return null
            if (number <= 0 || stem.isEmpty()) return null
            return Export.GameNative(stem, store, number)
        }
        if (extension == "desktop") {
            val lines = text.lineSequence().map { it.trim() }.toList()
            if (lines.none { it.equals("[Desktop Entry]", ignoreCase = true) }) return null
            val exec = lines.firstOrNull { it.startsWith("Exec=") } ?: return null
            if (!exec.contains("wine", ignoreCase = true)) return null
            val title = lines.firstOrNull { it.startsWith("Name=") }?.removePrefix("Name=")?.trim().orEmpty().ifEmpty { stem }
            if (title.isEmpty()) return null
            return Export.Winlator(title, path)
        }
        return null
    }

    /**
     * What starts [export], of the [installed] packages; null where no app that plays it is
     * installed. A GameNative game is given the release's package when none is (it is the one
     * launchers name), so the title is there once the app is.
     */
    fun launchOf(export: Export, installed: Set<String>): Launch? = when (export) {
        is Export.GameNative -> Launch(
            app = GAMENATIVE.firstOrNull { it in installed } ?: GAMENATIVE.first(),
            activity = GAMENATIVE_ACTIVITY,
            action = GAMENATIVE_ACTION,
            ints = mapOf("app_id" to export.number),
            strings = mapOf("game_source" to export.store),
        )
        is Export.Winlator -> {
            // WinNative's own folder for its shortcuts names it; otherwise the first installed.
            val own = if (export.path.contains("WinNative", ignoreCase = true)) WINLATOR.filter { it.first == "com.winnative.cmod" } else emptyList()
            (own + WINLATOR).firstOrNull { it.first in installed }?.let { (app, activity) ->
                Launch(app, activity, null, strings = mapOf("shortcut_path" to export.path), clearTask = true)
            }
        }
    }

    /** The app's name for the shelf's *Plays in*. */
    fun labelOf(export: Export): String = when (export) {
        is Export.GameNative -> "GameNative"
        is Export.Winlator -> "Winlator"
    }

    /**
     * A title's ID, the same on every look (its reading figures and cards are kept under it):
     * GameNative's by store and number (`APP-STEAM-274190`), a shortcut's by its name's letters.
     */
    fun idOf(export: Export): String = when (export) {
        is Export.GameNative -> "APP-" + export.store.replace("_", "") + "-" + export.number
        is Export.Winlator -> {
            val letters = export.title.uppercase().filter { it in 'A'..'Z' || it in '0'..'9' }.take(12)
            "APP-W-" + letters.ifEmpty { Integer.toHexString(export.title.hashCode()).uppercase() }
        }
    }
}
