// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/** A setting a core understands (cores/reader_core.h, reader_core_setting). */
class PlaySetting(
    val key: String,
    val label: String,
    val kind: Int,
    val default: String,
    val low: Double = 0.0,
    val high: Double = 0.0,
    val step: Double = 0.0,
    val choices: List<String> = emptyList(),
) {
    /** One every console has; a console's own key begins with its console's. */
    val common get() = key.startsWith("picture.") || key.startsWith("sound.") || key.startsWith("emulation.")

    /**
     * Not on the page: the CRT picture has its own, the speed is for writing profiles, and which
     * way a core draws here is settled by what works on Android.
     */
    val hidden get() = key in NOT_SHOWN || kind == TEXT || kind == FOLDER

    /** The value as the page shows it. */
    fun shown(value: String): String = when {
        kind == SWITCH -> if (value == "1") "ON" else "OFF"
        key == "picture.resolution" -> "$value×"
        else -> value
    }

    fun toJson(): JSONObject = JSONObject().put("key", key).put("label", label).put("kind", kind)
        .put("low", low).put("high", high).put("step", step).put("choices", JSONArray(choices))

    companion object {
        const val SWITCH = 0
        const val NUMBER = 1
        const val CHOICE = 2
        const val TEXT = 3
        const val FOLDER = 4

        private val NOT_SHOWN = setOf("picture.crt", "picture.renderer", "emulation.speed")

        /** What the common settings are called, where a core gives no label. */
        private val LABELS = mapOf(
            "picture.resolution" to "Internal resolution",
            "picture.aspect" to "Shape of the picture",
            "picture.vsync" to "Vertical sync",
            "picture.frame_limit" to "At the console's own speed",
            "sound.volume" to "Volume",
        )

        /** Where this app starts a console elsewhere than its core does (GameActivity). */
        private val DEFAULTS_HERE = mapOf("3ds" to mapOf("picture.resolution" to "3"))

        /** The settings in a core's description (GameNative.info), as this app has them. */
        fun list(console: String, info: JSONObject): List<PlaySetting> {
            val list = info.optJSONArray("settings") ?: return emptyList()
            return List(list.length()) { at ->
                val one = list.getJSONObject(at)
                val key = one.getString("key")
                val choices = one.optJSONArray("choices")
                PlaySetting(
                    // Here every setting is from the next game: the page says so once.
                    key, one.optString("label").removeSuffix(" (from the next game)").ifEmpty { LABELS[key] ?: key },
                    one.optInt("kind"),
                    DEFAULTS_HERE[console]?.get(key) ?: one.optString("default"), one.optDouble("low", 0.0),
                    one.optDouble("high", 0.0), one.optDouble("step", 0.0),
                    if (choices == null) emptyList() else List(choices.length()) { choices.getString(it) },
                )
            }
        }
    }
}

/**
 * Settings for playing, as the desktop program's (home/docs/one-program.md, "Settings", and
 * home/src/play_settings.h): one file of the same form, each setting looked up in layers and the
 * first that has it winning: this game, this console, all consoles, the core's own default. A
 * console's own setting is never taken from all consoles' layer. The page writes the file and
 * GameActivity reads it as a game starts, so no process keeps an old copy.
 */
class PlaySettings(private val file: File) {
    enum class Layer { GAME, CONSOLE, ALL, DEFAULT }

    class Resolved(val value: String, val from: Layer)

    private val root: JSONObject = try {
        JSONObject(file.readText())
    } catch (_: Exception) {
        JSONObject().put("format", 1)
    }

    private fun layer(console: String, game: String, make: Boolean = false): JSONObject? {
        var at: JSONObject = root
        val path = when {
            game.isNotEmpty() -> listOf("games", console, game)
            console.isNotEmpty() -> listOf("consoles", console)
            else -> listOf("all")
        }
        for (name in path) {
            at = at.optJSONObject(name) ?: if (make) JSONObject().also { at.put(name, it) } else return null
        }
        return at
    }

    fun isSet(key: String, console: String = "", game: String = ""): Boolean =
        layer(console, game)?.optString(key).orEmpty().isNotEmpty()

    fun value(setting: PlaySetting, console: String, game: String = ""): Resolved {
        fun of(console: String, game: String) = layer(console, game)?.optString(setting.key).orEmpty()
        if (game.isNotEmpty()) of(console, game).takeIf { it.isNotEmpty() }?.let { return Resolved(it, Layer.GAME) }
        if (console.isNotEmpty()) of(console, "").takeIf { it.isNotEmpty() }?.let { return Resolved(it, Layer.CONSOLE) }
        if (setting.common || console.isEmpty()) of("", "").takeIf { it.isNotEmpty() }?.let { return Resolved(it, Layer.ALL) }
        return Resolved(setting.default, Layer.DEFAULT)
    }

    /** Sets it in that layer, or (an empty value) takes it out so the next layer shows. */
    fun set(key: String, value: String, console: String = "", game: String = "") {
        if (value.isEmpty()) {
            layer(console, game)?.remove(key)
        } else {
            layer(console, game, make = true)?.put(key, value)
        }
        save()
    }

    /**
     * What a core is to be handed for a game. Only what was set: a core's defaults are its own,
     * and on Android some are not those it lists (cores/libretro/libretro_core.h).
     */
    fun valuesFor(console: String, game: String, settings: List<PlaySetting>): List<Pair<String, String>> =
        settings.filter { !it.hidden }.mapNotNull { setting ->
            value(setting, console, game).takeIf { it.from != Layer.DEFAULT }?.let { setting.key to it.value }
        }

    /** A layer as the page shows it, each setting with what it comes to there. */
    fun page(settings: List<PlaySetting>, console: String, game: String = ""): JSONArray {
        val out = JSONArray()
        for (setting in settings) {
            val here = isSet(setting.key, console, game)
            val resolved = value(setting, console, game)
            val shown = setting.shown(resolved.value)
            out.put(
                setting.toJson().put("value", resolved.value).put("shown", shown).put("set", here).put(
                    "from",
                    when {
                        here -> ""
                        resolved.from == Layer.CONSOLE -> "Same as its console"
                        resolved.from == Layer.ALL -> "Same as all consoles"
                        console.isEmpty() -> "As each console has it"
                        else -> "The console's own"
                    },
                )
            )
        }
        return out
    }

    private fun save() {
        val partial = File(file.path + ".new")
        partial.writeText(root.toString(1))
        partial.renameTo(file)
    }
}
