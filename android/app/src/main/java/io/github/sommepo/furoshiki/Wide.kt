// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

/**
 * Widescreen as one row (the desktop's `WideStep`): every console whose core has a wide picture
 * (a setting `<console>.wide`: off and the shapes it has recipes for) set at once, in that
 * console's layer. A console without the shape chosen is set off.
 */
object Wide {
    private val ORDER = listOf("off", "16:9", "21:9", "32:9")

    /** The consoles' wide settings, by console. */
    fun settings(byConsole: Map<String, List<PlaySetting>>): Map<String, PlaySetting> =
        byConsole.mapNotNull { (console, settings) -> settings.firstOrNull { it.key.endsWith(".wide") }?.let { console to it } }.toMap()

    /** Every shape some console has, in order, "off" first; empty where no core has a wide picture. */
    fun choices(wide: Map<String, PlaySetting>): List<String> {
        val all = wide.values.flatMap { it.choices }.toSet()
        if (all.none { it != "off" }) return emptyList()
        return ORDER.filter { it == "off" || it in all } + all.filter { it !in ORDER }.sorted()
    }

    /** What the row says: the shape the consoles are set to, the widest where they differ. */
    fun value(wide: Map<String, PlaySetting>, play: PlaySettings): String {
        val set = wide.map { (console, setting) -> play.raw(setting.key, console).ifEmpty { setting.default.ifEmpty { "off" } } }
        val choices = choices(wide)
        return set.maxByOrNull { choices.indexOf(it) } ?: "off"
    }

    fun set(wide: Map<String, PlaySetting>, play: PlaySettings, shape: String) {
        for ((console, setting) in wide) play.set(setting.key, if (shape in setting.choices) shape else "off", console)
    }
}
