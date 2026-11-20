// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.view.KeyEvent
import org.json.JSONObject
import java.io.File

/**
 * The buttons of "Shortcut buttons", as they were until 2026-11-20: a list of its own
 * (`files/action-keys.json`, `{"<action>": <Android key code>}`) beside the controller screen's
 * shortcuts (ButtonMap), so that an action could be given a button in two places. There is one
 * place now, Settings › Controls › Controller: what a user had set here is given to the same
 * actions there once ([migrate]) and the file is gone. Every action the list had is one of the
 * controller screen's ([TO_MAP]; map.js lists them, GameActivity.runAction does them).
 *
 * What is still used of it: how a button is called ([name]), the same on every screen.
 */
object ActionKeys {
    /** Each action of the old list, and the controller screen's action it is. */
    val TO_MAP = linkedMapOf(
        "pause" to "reader_pause_game",
        "stats" to "reader_pause_stats",
        "card" to "reader_sentence_card",
        "log" to "reader_backlog",
        "explain" to "reader_explain",
        "ocr_now" to "furoshiki_ocr_read",
        "panel" to "reader_toggle_panel",
        "next_choice" to "reader_next_choice",
        "fast" to "furoshiki_fast_forward",
        "guide" to "furoshiki_guide",
        "guide_done" to "furoshiki_guide_done",
        "quick_menu" to "reader_menu",
        "menu" to "furoshiki_game_menu",
    )

    private fun file(context: Context) = File(context.filesDir, "action-keys.json")

    /** The file's text as actions and their keys; what is no action of ours, or no key, is left out. */
    fun parse(text: String): Map<String, Int> {
        val kept = runCatching { JSONObject(text) }.getOrNull() ?: return emptyMap()
        return kept.keys().asSequence().filter { it in TO_MAP && kept.optInt(it, 0) > 0 }.associateWith { kept.getInt(it) }
    }

    /**
     * What the old list had ([assigned]) put into the controller screen's values, all consoles'
     * layer: each action on its button, as it was. A button given here did its action before
     * anything the controller screen had on it, so it wins there too (ButtonMap.assign takes the
     * button from what had it, and the action off any other button). True when anything was set.
     */
    fun migrateInto(values: MutableMap<String, Any?>, assigned: Map<String, Int>): Boolean {
        var changed = false
        for ((old, key) in assigned) {
            val action = TO_MAP[old] ?: continue
            if (io.github.sommepo.furoshiki.reader.ButtonMap.assign(values, action, key, null)) changed = true
        }
        return changed
    }

    /** [migrateInto] on the reader's preferences, once: the old file is taken away after. */
    fun migrate(context: Context) {
        val file = file(context)
        if (!file.isFile) return
        runCatching {
            val assigned = parse(file.readText())
            if (assigned.isNotEmpty()) {
                val preferences = io.github.sommepo.furoshiki.reader.ReaderSettings.fresh(context)
                val values = io.github.sommepo.furoshiki.reader.ButtonMap.read(preferences)
                if (migrateInto(values, assigned)) io.github.sommepo.furoshiki.reader.ButtonMap.write(preferences, values)
            }
        }
        file.delete()
    }

    /** The buttons whose name is not Android's own with its prefix taken off. */
    private val NAMES = mapOf(
        KeyEvent.KEYCODE_BUTTON_THUMBL to "L3", KeyEvent.KEYCODE_BUTTON_THUMBR to "R3",
        KeyEvent.KEYCODE_BUTTON_START to "Start", KeyEvent.KEYCODE_BUTTON_SELECT to "Select",
        KeyEvent.KEYCODE_BUTTON_MODE to "Home",
        KeyEvent.KEYCODE_DPAD_UP to "D-pad up", KeyEvent.KEYCODE_DPAD_DOWN to "D-pad down",
        KeyEvent.KEYCODE_DPAD_LEFT to "D-pad left", KeyEvent.KEYCODE_DPAD_RIGHT to "D-pad right",
        KeyEvent.KEYCODE_DPAD_CENTER to "D-pad centre",
    )

    /**
     * A key as the controller screen says it: "R2", "L3", "D-pad up", "Start"; a numbered button
     * "Button 3"; any other key its Android name in plain words ("Volume mute", "F5").
     */
    fun name(key: Int): String = NAMES[key] ?: plainName(KeyEvent.keyCodeToString(key))

    /** "KEYCODE_BUTTON_R2" as "R2", "KEYCODE_BUTTON_3" as "Button 3", "KEYCODE_MEDIA_PLAY" as "Media play". */
    fun plainName(android: String): String {
        val name = android.removePrefix("KEYCODE_")
        val button = name.removePrefix("BUTTON_")
        return when {
            button != name && button.all { it.isDigit() } -> "Button $button"
            // A1, L1, R2, X, F5: said as they are printed.
            button.length <= 2 || button.matches(Regex("[A-Z][0-9]{1,2}")) -> button
            else -> button.lowercase().replace('_', ' ').replaceFirstChar { it.uppercase() }
        }
    }
}
