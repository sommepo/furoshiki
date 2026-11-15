// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.content.Context
import android.view.KeyEvent
import androidx.appcompat.app.AlertDialog
import org.json.JSONObject
import java.io.File

/**
 * What can be done over a game without its menu, each by a key on the second display
 * ([LowerKeys]) and by a button of the controller given to it here: what a computer's controller
 * has on its back buttons (the desktop's `reader_back_button_N`, whose actions these are). A
 * button given to an action is no longer the game's. Kept in `files/action-keys.json` as
 * `{"<action>": <Android key code>}`, a file both of the app's processes read: the page's
 * Settings and the menu over a game open the same list.
 */
object ActionKeys {
    /** An action: its name in the file, and what the list calls it. */
    class Action(val id: String, val label: Int)

    val ALL = listOf(
        Action("pause", R.string.action_pause),
        Action("stats", R.string.action_stats),
        Action("card", R.string.action_card),
        Action("log", R.string.action_log),
        Action("explain", R.string.action_explain),
        Action("ocr_now", R.string.action_ocr_now),
        Action("panel", R.string.action_panel),
        Action("next_choice", R.string.action_next_choice),
        Action("fast", R.string.action_fast),
        Action("guide", R.string.action_guide),
        Action("guide_done", R.string.action_guide_done),
        Action("quick_menu", R.string.action_quick_menu),
        Action("menu", R.string.action_menu),
    )

    private fun file(context: Context) = File(context.filesDir, "action-keys.json")

    /** What the file says: each action's key code. */
    fun assigned(context: Context): Map<String, Int> = parse(runCatching { file(context).readText() }.getOrDefault(""))

    /** The file's text as actions and their keys; what is no action of ours, or no key, is left out. */
    fun parse(text: String): Map<String, Int> {
        val kept = runCatching { JSONObject(text) }.getOrNull() ?: return emptyMap()
        val known = ALL.map { it.id }.toSet()
        return kept.keys().asSequence().filter { it in known && kept.optInt(it, 0) > 0 }.associateWith { kept.getInt(it) }
    }

    /** Each key and the action it does, for the game's own look at a key. */
    fun byKey(context: Context): Map<Int, String> = assigned(context).entries.associate { it.value to it.key }

    /**
     * [action] given [key] (null: none) in what was [assigned] before: one action a key, so the
     * action that key had loses it.
     */
    fun give(assigned: Map<String, Int>, action: String, key: Int?): Map<String, Int> {
        val next = assigned.filter { it.key != action && it.value != key }.toMutableMap()
        if (key != null) next[action] = key
        return next
    }

    private fun save(context: Context, assigned: Map<String, Int>) {
        val next = File(file(context).path + ".new")
        next.writeText(JSONObject(assigned).toString())
        next.renameTo(file(context))
    }

    /** A key as a person says it: "R2", "THUMBL", "F5". */
    fun name(key: Int): String = KeyEvent.keyCodeToString(key).removePrefix("KEYCODE_").removePrefix("BUTTON_")

    /** Keys that stay what they are: the way out of every dialog, and the volume. */
    private fun fixed(key: Int) = key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_HOME ||
        key == KeyEvent.KEYCODE_VOLUME_UP || key == KeyEvent.KEYCODE_VOLUME_DOWN

    /** The list: each action with its button; a tap, then the button to give it. [done] when it is shut. */
    fun pick(activity: Activity, done: () -> Unit) {
        val themed = android.view.ContextThemeWrapper(activity, R.style.Theme_Reader)
        val assigned = assigned(activity)
        val labels = ALL.map { activity.getString(it.label) + "  ·  " + (assigned[it.id]?.let(::name) ?: "—") } +
            activity.getString(R.string.action_keys_none)
        var again = false
        AlertDialog.Builder(themed)
            .setTitle(R.string.action_keys_title)
            .setItems(labels.toTypedArray()) { _, which ->
                again = true
                if (which >= ALL.size) {
                    save(activity, emptyMap())
                    pick(activity, done)
                    return@setItems
                }
                val action = ALL[which]
                val waiting = AlertDialog.Builder(themed)
                    .setTitle(action.label)
                    .setMessage(R.string.action_keys_press)
                    .setNeutralButton(R.string.action_keys_no_button) { _, _ -> save(activity, give(assigned(activity), action.id, null)) }
                    .setNegativeButton(android.R.string.cancel, null)
                    .setOnDismissListener { pick(activity, done) }
                    .create()
                // The dialog's own window takes the keys: the first that is not a fixed one is the answer.
                waiting.setOnKeyListener { _, code, event ->
                    if (fixed(code)) return@setOnKeyListener false
                    if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                        save(activity, give(assigned(activity), action.id, code))
                        waiting.dismiss()
                    }
                    true
                }
                waiting.show()
            }
            .setOnDismissListener { if (!again) done() }
            .show()
    }
}
