// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.SharedPreferences

/**
 * The controller's buttons and what each does: the model of the controller mapping screen, the
 * desktop program's alike (shared/controllers/controllers.json has the actions and the inputs).
 *
 * Buttons are Android's key codes. Two layers, in the reader's preferences:
 *  - all consoles: the settings as they always were (the pointer's, the statistics', the next
 *    choice's, the lookup menu's button), and `reader_button_action_<code>` = an action's id, or
 *    "none", for any other action put on a button;
 *  - one console: the same keys followed by `@<console>` (`reader_cursor_button@ps2`,
 *    `reader_button_action_106@psp`), which win over all consoles' while a game of it plays.
 *
 * Within a layer an action is on one button and a button does one thing: [assign] takes the
 * action off its other buttons and frees the button first. On a console's layer what comes from
 * all consoles is shadowed rather than changed ("-1", "none" there).
 *
 * The game's own buttons are here as well, with the desktop's keys: `pad_button_<id>` is the
 * input (south, l2...: the controller's button, by where it lies) that presses the console's
 * button `<id>`; by itself each is its own, "none" is nothing. A console's layer the same way
 * (`pad_button_south@psp`). [assignPad] swaps, so that no two are on one button of the controller.
 */
object ButtonMap {
    const val ACTION_PREFIX = "reader_button_action_"
    const val NONE = "none"
    const val POINTER = "pointer"

    /** The actions kept in a setting of their own, by action id. */
    val SETTINGS: Map<String, String> = linkedMapOf(
        POINTER to ReaderSettings.KEY_CURSOR_BUTTON,
        "reader_menu" to ReaderSettings.KEY_MENU_BUTTON,
        "reader_pause_stats" to ReaderSettings.KEY_STATS_BUTTON,
        "reader_next_choice" to ReaderSettings.KEY_NEXT_CHOICE_BUTTON,
    )

    /**
     * The buttons the pointer can be held on (the settings' old list): shoulders, triggers, the
     * sticks pressed, Select and Start. The face buttons and the d-pad are the game's while it is held.
     */
    val POINTER_BUTTONS = setOf(102, 103, 104, 105, 106, 107, 108, 109)

    /** A key of [console]'s layer, or of all consoles' for null or "". */
    fun key(base: String, console: String?): String = if (console.isNullOrEmpty()) base else "$base@$console"

    private fun text(values: Map<String, *>, key: String): String? = values[key]?.toString()

    private fun defaultOf(setting: String): Int =
        if (setting == ReaderSettings.KEY_CURSOR_BUTTON) Console.CURSOR_BUTTON_DEFAULT else -1

    /** A setting's button as a game of [console] has it (null: all consoles'). */
    fun setting(values: Map<String, *>, setting: String, console: String?): Int {
        if (!console.isNullOrEmpty()) text(values, key(setting, console))?.toIntOrNull()?.let { return it }
        return text(values, setting)?.toIntOrNull() ?: defaultOf(setting)
    }

    /** The action put on [button] as a game of [console] has it, or null. */
    fun action(values: Map<String, *>, button: Int, console: String?): String? {
        val own = if (console.isNullOrEmpty()) null else text(values, key(ACTION_PREFIX + button, console))
        return (own ?: text(values, ACTION_PREFIX + button))?.takeIf { it.isNotEmpty() && it != NONE }
    }

    /** The same, read from the preferences at once (a key pressed in a game). */
    fun action(preferences: SharedPreferences, button: Int, console: String?): String? {
        fun read(key: String): String? = if (!preferences.contains(key)) null else
            runCatching { preferences.getString(key, null) }.getOrNull()
        val own = if (console.isNullOrEmpty()) null else read(key(ACTION_PREFIX + button, console))
        return (own ?: read(ACTION_PREFIX + button))?.takeIf { it.isNotEmpty() && it != NONE }
    }

    /** Every button that has an action's key, in any layer. */
    private fun actionButtons(values: Map<String, *>): List<Int> = values.keys.mapNotNull { key ->
        if (key.startsWith(ACTION_PREFIX)) key.removePrefix(ACTION_PREFIX).substringBefore('@').toIntOrNull() else null
    }.distinct().sorted()

    /** Where an action is: its button (-1 for none), and whether [console]'s own layer says so. */
    data class Binding(val button: Int, val own: Boolean)

    fun binding(values: Map<String, *>, action: String, console: String?): Binding {
        val layered = !console.isNullOrEmpty()
        SETTINGS[action]?.let { setting ->
            if (layered && values.containsKey(key(setting, console))) return Binding(setting(values, setting, console), true)
            return Binding(setting(values, setting, null), !layered)
        }
        val buttons = actionButtons(values)
        val found = buttons.firstOrNull { action(values, it, console) == action } ?: -1
        val own = !layered || (found >= 0 && values.containsKey(key(ACTION_PREFIX + found, console))) ||
            (found < 0 && buttons.any { text(values, key(ACTION_PREFIX + it, null)) == action })
        return Binding(found, own)
    }

    /** True when [console]'s layer sets anything of its own ("Same as all consoles" would change it). */
    fun hasOwn(values: Map<String, *>, console: String): Boolean = ownKeys(values, console).isNotEmpty()

    private fun ownKeys(values: Map<String, *>, console: String): List<String> = values.keys.filter { key ->
        key.endsWith("@$console") &&
            (key.startsWith(ACTION_PREFIX) || key.startsWith(PAD_PREFIX) || SETTINGS.values.any { key == "$it@$console" })
    }

    /** The action taken off every button of the layer: what "Clear" does. */
    fun clear(values: MutableMap<String, Any?>, action: String, console: String?) {
        SETTINGS[action]?.let { setting ->
            values[key(setting, console)] = "-1"
            return
        }
        for (button in actionButtons(values)) {
            if (action(values, button, console) == action) values[key(ACTION_PREFIX + button, console)] = NONE
        }
    }

    /**
     * Puts [action] on [button] in [console]'s layer (null: all consoles): off its other buttons,
     * the button freed of what it did, then bound. False, and nothing changed, for a button the
     * pointer cannot be held on.
     */
    fun assign(values: MutableMap<String, Any?>, action: String, button: Int, console: String?): Boolean {
        if (action == POINTER && button !in POINTER_BUTTONS) return false
        clear(values, action, console)
        for ((other, setting) in SETTINGS) {
            if (other != action && setting(values, setting, console) == button) values[key(setting, console)] = "-1"
        }
        if (action(values, button, console) != null) values[key(ACTION_PREFIX + button, console)] = NONE
        val setting = SETTINGS[action]
        if (setting != null) {
            values[key(setting, console)] = button.toString()
        } else {
            values[key(ACTION_PREFIX + button, console)] = action
        }
        return true
    }

    /** A console's layer taken away: its games do as all consoles do. */
    fun sameAsAll(values: MutableMap<String, Any?>, console: String) {
        for (key in ownKeys(values, console)) values.remove(key)
    }

    // ---- The game's own buttons ----

    const val PAD_PREFIX = "pad_button_"

    /** The console's buttons, in the order the screen lists them. */
    val PAD_BUTTONS = listOf(
        "up", "down", "left", "right", "south", "east", "west", "north",
        "l1", "r1", "l2", "r2", "l3", "r3", "select", "start",
    )

    /** The controller's buttons (the inputs of controllers.json) and Android's key code of each. */
    val PAD_INPUTS: Map<String, Int> = linkedMapOf(
        "up" to 19, "down" to 20, "left" to 21, "right" to 22,
        "south" to 96, "east" to 97, "west" to 99, "north" to 100,
        "l1" to 102, "r1" to 103, "l2" to 104, "r2" to 105, "l3" to 106, "r3" to 107,
        "start" to 108, "select" to 109,
    )

    /** An input's id for a key code, or null for a key that is no button of a controller's. */
    fun padInput(code: Int): String? = PAD_INPUTS.entries.firstOrNull { it.value == code }?.key

    /** The buttons by where libretro has them (a core's bits, `Console.padBit`): the old "Buttons" dialog's places. */
    val PAD_PLACES = listOf(
        "south", "west", "select", "start", "up", "down", "left", "right",
        "east", "north", "l1", "r1", "l2", "r2", "l3", "r3",
    )

    /** The controller's button that presses the console's [button] in a game of [console]: an input's id, or "none". */
    fun padSource(values: Map<String, *>, button: String, console: String?): String {
        val own = if (console.isNullOrEmpty()) null else text(values, key(PAD_PREFIX + button, console))
        return (own ?: text(values, PAD_PREFIX + button))?.takeIf { it == NONE || it in PAD_INPUTS } ?: button
    }

    /** A console's button as a layer has it: its [source], whether [console]'s own layer says so, whether it is not its own. */
    data class PadBinding(val source: String, val own: Boolean, val changed: Boolean)

    fun padBinding(values: Map<String, *>, button: String, console: String?): PadBinding {
        val source = padSource(values, button, console)
        val own = console.isNullOrEmpty() || values.containsKey(key(PAD_PREFIX + button, console))
        return PadBinding(source, own, source != button)
    }

    /** Written so that the layer says no more than it must: a button that is its own has no key. */
    private fun putPad(values: MutableMap<String, Any?>, button: String, source: String, console: String?) {
        val key = key(PAD_PREFIX + button, console)
        val plain = source == button && (console.isNullOrEmpty() || padSource(values, button, null) == button)
        if (plain) values.remove(key) else values[key] = source
    }

    /**
     * The console's [button] is pressed by the controller's [input] in [console]'s layer (null:
     * all consoles). A button that was on that input takes the one [button] had: a swap, so no
     * two are on one. False, and nothing changed, for what is no input or no button.
     */
    fun assignPad(values: MutableMap<String, Any?>, button: String, input: String, console: String?): Boolean {
        if (button !in PAD_BUTTONS || input !in PAD_INPUTS) return false
        val before = padSource(values, button, console)
        if (before == input) return true
        val others = PAD_BUTTONS.filter { it != button && padSource(values, it, console) == input }
        for (other in others) putPad(values, other, before, console)
        putPad(values, button, input, console)
        return true
    }

    /**
     * The console's [button] back on its own: on a console's layer written outright where all
     * consoles have it elsewhere, else the key taken out.
     */
    /**
     * The input wizard's answers at once: each console button asked for on the controller's
     * button that was pressed for it, as it was pressed (no swaps: the whole pad was asked). A
     * button passed over keeps what it had, unless that is now another's: then nothing presses it.
     */
    fun setPads(values: MutableMap<String, Any?>, given: Map<String, String>, console: String?) {
        val asked = given.filter { (button, input) -> button in PAD_BUTTONS && input in PAD_INPUTS }
        val before = PAD_BUTTONS.associateWith { padSource(values, it, console) }
        for ((button, input) in asked) putPad(values, button, input, console)
        for (button in PAD_BUTTONS) {
            if (button !in asked && before[button] in asked.values) putPad(values, button, NONE, console)
        }
    }

    fun clearPad(values: MutableMap<String, Any?>, button: String, console: String?) {
        if (button in PAD_BUTTONS) putPad(values, button, button, console)
    }

    /**
     * What a key of the controller presses in a game of [console]: each input's key code with
     * the console's buttons on it (mostly one; none for a button nothing is on).
     */
    fun padKeys(values: Map<String, *>, console: String?): Map<Int, List<String>> {
        val keys = HashMap<Int, MutableList<String>>()
        for (button in PAD_BUTTONS) {
            val code = PAD_INPUTS[padSource(values, button, console)] ?: continue
            keys.getOrPut(code) { ArrayList() }.add(button)
        }
        return keys
    }

    /**
     * The old "Buttons" dialog's map (a place of [PAD_PLACES] to the key code given to it) as
     * all consoles' buttons: a key given a place left the place it had, and that place had no
     * key then. Nothing where the buttons have been set here already. True when anything was set.
     */
    fun migratePlaces(values: MutableMap<String, Any?>, given: Map<Int, Int>): Boolean {
        if (values.keys.any { it.startsWith(PAD_PREFIX) && '@' !in it }) return false
        val keys = HashMap<Int, Int>()
        PAD_PLACES.forEachIndexed { place, id -> keys[PAD_INPUTS.getValue(id)] = place }
        for ((place, key) in given) {
            if (place !in PAD_PLACES.indices) continue
            keys.remove(key)
            keys.entries.removeAll { it.value == place }
            keys[key] = place
        }
        var changed = false
        PAD_PLACES.forEachIndexed { place, id ->
            val key = keys.entries.firstOrNull { it.value == place }?.key
            // A key that is no button of a controller's (it cannot be said here): the button stays its own.
            val source = if (key == null) NONE else padInput(key) ?: id
            if (source != id) {
                values[PAD_PREFIX + id] = source
                changed = true
            }
        }
        return changed
    }

    // ---- On the preferences themselves ----

    /** What the preferences hold, to be changed by the functions above and written back by [write]. */
    fun read(preferences: SharedPreferences): MutableMap<String, Any?> = HashMap(preferences.all)

    /** Writes what [changed] differs in from the preferences: at once, as another process reads them. */
    fun write(preferences: SharedPreferences, changed: Map<String, Any?>) {
        val before = preferences.all
        val edit = preferences.edit()
        for ((key, value) in changed) {
            if (isMapping(key) && before[key]?.toString() != value?.toString()) edit.putString(key, value?.toString())
        }
        for (key in before.keys) if (key !in changed && isMapping(key)) edit.remove(key)
        edit.commit()
    }

    /** A key of this map's (of any layer): only these are ever taken out. */
    private fun isMapping(key: String): Boolean {
        val base = key.substringBefore('@')
        return base.startsWith(ACTION_PREFIX) || base.startsWith(PAD_PREFIX) || base in SETTINGS.values
    }

    fun assign(preferences: SharedPreferences, action: String, button: Int, console: String?): Boolean {
        val values = read(preferences)
        if (!assign(values, action, button, console)) return false
        write(preferences, values)
        return true
    }

    fun clear(preferences: SharedPreferences, action: String, console: String?) {
        val values = read(preferences)
        clear(values, action, console)
        write(preferences, values)
    }

    fun sameAsAll(preferences: SharedPreferences, console: String) {
        val values = read(preferences)
        sameAsAll(values, console)
        write(preferences, values)
    }

    fun assignPad(preferences: SharedPreferences, button: String, input: String, console: String?): Boolean {
        val values = read(preferences)
        if (!assignPad(values, button, input, console)) return false
        write(preferences, values)
        return true
    }

    fun setPads(preferences: SharedPreferences, given: Map<String, String>, console: String?) {
        val values = read(preferences)
        setPads(values, given, console)
        write(preferences, values)
    }

    fun clearPad(preferences: SharedPreferences, button: String, console: String?) {
        val values = read(preferences)
        clearPad(values, button, console)
        write(preferences, values)
    }

    /** [migratePlaces] on the preferences. */
    fun migratePlaces(preferences: SharedPreferences, given: Map<Int, Int>): Boolean {
        val values = read(preferences)
        if (!migratePlaces(values, given)) return false
        write(preferences, values)
        return true
    }
}
