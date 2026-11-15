// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import android.content.Context
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The controller mapping screen's model (ButtonMap): an action on one button and a button doing
 * one thing in each layer, a console's layer over all consoles', and the reader's settings
 * reading the console in play first.
 */
@RunWith(RobolectricTestRunner::class)
class ButtonMapTest {
    private val context: Context = ApplicationProvider.getApplicationContext()
    private val preferences get() = PreferenceManager.getDefaultSharedPreferences(context)

    private val l1 = 102
    private val r1 = 103
    private val l2 = 104
    private val l3 = 106
    private val r3 = 107
    private val north = 100
    private val south = 96

    @Before
    fun clean() {
        preferences.edit().clear().commit()
        ReaderSettings.console = null
    }

    @After
    fun forget() {
        ReaderSettings.console = null
    }

    @Test
    fun anActionIsOnOneButtonAndAButtonDoesOneThing() {
        val values = HashMap<String, Any?>()
        // The pointer is on L2 until chosen otherwise.
        assertEquals(ButtonMap.Binding(l2, true), ButtonMap.binding(values, "pointer", null))

        assertTrue(ButtonMap.assign(values, "reader_backlog", l3, null))
        assertEquals("reader_backlog", values["reader_button_action_$l3"])
        assertEquals(ButtonMap.Binding(l3, true), ButtonMap.binding(values, "reader_backlog", null))

        // Moved: off L3, on R3.
        ButtonMap.assign(values, "reader_backlog", r3, null)
        assertNull(ButtonMap.action(values, l3, null))
        assertEquals("reader_backlog", ButtonMap.action(values, r3, null))

        // Another action on R3 takes the button: the backlog is on nothing.
        ButtonMap.assign(values, "reader_sentence_card", r3, null)
        assertEquals("reader_sentence_card", ButtonMap.action(values, r3, null))
        assertEquals(-1, ButtonMap.binding(values, "reader_backlog", null).button)

        // A setting's action frees the button of what it did, and the other way about.
        ButtonMap.assign(values, "reader_pause_stats", r3, null)
        assertEquals("$r3", values[ReaderSettings.KEY_STATS_BUTTON])
        assertNull(ButtonMap.action(values, r3, null))
        ButtonMap.assign(values, "reader_backlog", l2, null)
        assertEquals("the pointer let go of L2", "-1", values[ReaderSettings.KEY_CURSOR_BUTTON])
        assertEquals(ButtonMap.Binding(-1, true), ButtonMap.binding(values, "pointer", null))
        ButtonMap.assign(values, "pointer", r3, null)
        assertEquals(-1, ButtonMap.setting(values, ReaderSettings.KEY_STATS_BUTTON, null))
        assertEquals(r3, ButtonMap.setting(values, ReaderSettings.KEY_CURSOR_BUTTON, null))
    }

    @Test
    fun clearTakesTheActionOffAndThePointerRefusesAButtonItCannotBeHeldOn() {
        val values = HashMap<String, Any?>()
        ButtonMap.assign(values, "reader_backlog", l3, null)
        ButtonMap.clear(values, "reader_backlog", null)
        assertEquals(-1, ButtonMap.binding(values, "reader_backlog", null).button)
        ButtonMap.clear(values, "pointer", null)
        assertEquals("cleared, not back to L2", -1, ButtonMap.setting(values, ReaderSettings.KEY_CURSOR_BUTTON, null))

        val before = HashMap(values)
        assertFalse(ButtonMap.assign(values, "pointer", south, null))
        assertEquals(before, values)
        assertTrue(ButtonMap.assign(values, "pointer", r1, null))
    }

    @Test
    fun aConsolesLayerWinsAndShadowsAllConsolesWithoutChangingThem() {
        val values = HashMap<String, Any?>()
        ButtonMap.assign(values, "reader_backlog", l3, null)
        ButtonMap.assign(values, "reader_pause_stats", r3, null)

        // On the PSP: the backlog from all consoles, dimmer.
        assertEquals(ButtonMap.Binding(l3, false), ButtonMap.binding(values, "reader_backlog", "psp"))

        // The PSP's own: the backlog on △; L3 does nothing there, and all consoles keep theirs.
        ButtonMap.assign(values, "reader_backlog", north, "psp")
        assertEquals(ButtonMap.Binding(north, true), ButtonMap.binding(values, "reader_backlog", "psp"))
        assertNull(ButtonMap.action(values, l3, "psp"))
        assertEquals("reader_backlog", ButtonMap.action(values, l3, null))
        assertNull(ButtonMap.action(values, north, null))
        assertEquals(ButtonMap.Binding(l3, true), ButtonMap.binding(values, "reader_backlog", null))
        assertEquals("another console still has all consoles'", "reader_backlog", ButtonMap.action(values, l3, "ps2"))

        // The pointer on R3 for the PSP: the statistics' R3 (from all consoles) is let go there only.
        ButtonMap.assign(values, "pointer", r3, "psp")
        assertEquals(r3, ButtonMap.setting(values, ReaderSettings.KEY_CURSOR_BUTTON, "psp"))
        assertEquals(-1, ButtonMap.setting(values, ReaderSettings.KEY_STATS_BUTTON, "psp"))
        assertEquals(r3, ButtonMap.setting(values, ReaderSettings.KEY_STATS_BUTTON, null))
        assertEquals(l2, ButtonMap.setting(values, ReaderSettings.KEY_CURSOR_BUTTON, null))

        // Cleared on the PSP only.
        ButtonMap.clear(values, "reader_backlog", "psp")
        assertEquals(ButtonMap.Binding(-1, true), ButtonMap.binding(values, "reader_backlog", "psp"))
        assertEquals(l3, ButtonMap.binding(values, "reader_backlog", null).button)

        // Same as all consoles: the PSP's keys gone, all consoles' as they were.
        assertTrue(ButtonMap.hasOwn(values, "psp"))
        ButtonMap.sameAsAll(values, "psp")
        assertFalse(ButtonMap.hasOwn(values, "psp"))
        assertTrue(values.keys.none { it.endsWith("@psp") })
        assertEquals(ButtonMap.Binding(l3, false), ButtonMap.binding(values, "reader_backlog", "psp"))
        assertEquals(r3, ButtonMap.setting(values, ReaderSettings.KEY_STATS_BUTTON, "psp"))
    }

    @Test
    fun theReaderSettingsFollowTheConsoleInPlay() {
        ButtonMap.assign(preferences, "reader_pause_stats", r1, null)
        ButtonMap.assign(preferences, "reader_menu", l1, null)
        ButtonMap.assign(preferences, "reader_backlog", l3, null)
        ButtonMap.assign(preferences, "pointer", r3, "ps2")
        ButtonMap.assign(preferences, "reader_next_choice", l1, "ps2")
        ButtonMap.assign(preferences, "reader_word_card", north, "ps2")

        val settings = ReaderSettings(context)
        assertEquals(l2, settings.cursorButton)
        assertEquals(r1, settings.statsButton)
        assertEquals(l1, settings.menuButton)
        assertEquals(-1, settings.nextChoiceButton)
        assertEquals("reader_backlog", settings.buttonAction(l3))
        assertNull(settings.buttonAction(north))

        ReaderSettings.console = "ps2"
        assertEquals(r3, settings.cursorButton)
        assertEquals("all consoles' where the PS2 sets nothing", r1, settings.statsButton)
        assertEquals("L1 is the next choice's on the PS2", l1, settings.nextChoiceButton)
        assertEquals(-1, settings.menuButton)
        assertEquals("reader_word_card", settings.buttonAction(north))
        assertEquals("reader_backlog", settings.buttonAction(l3))

        ReaderSettings.console = "psp"
        assertEquals(l2, settings.cursorButton)
        assertEquals(l1, settings.menuButton)

        // Written as strings, as the settings' lists always kept them; only the map's own keys are touched.
        preferences.edit().putBoolean("reader_touch_play", true).commit()
        ButtonMap.sameAsAll(preferences, "ps2")
        assertTrue(preferences.all.keys.none { it.endsWith("@ps2") })
        assertTrue(preferences.getBoolean("reader_touch_play", false))
        assertEquals("$r1", preferences.getString(ReaderSettings.KEY_STATS_BUTTON, null))
    }

    @Test
    fun aConsolesButtonIsItsOwnUntilGivenAnother() {
        val values = HashMap<String, Any?>()
        assertEquals(ButtonMap.PadBinding("south", true, false), ButtonMap.padBinding(values, "south", null))
        assertEquals(ButtonMap.PadBinding("south", false, false), ButtonMap.padBinding(values, "south", "psp"))

        // × by the controller's B: what B pressed (○) takes A, a swap.
        assertTrue(ButtonMap.assignPad(values, "south", "east", null))
        assertEquals("east", values["pad_button_south"])
        assertEquals("south", values["pad_button_east"])
        assertEquals(ButtonMap.PadBinding("east", true, true), ButtonMap.padBinding(values, "south", null))
        assertEquals(listOf("south"), ButtonMap.padKeys(values, null)[97])
        assertEquals(listOf("east"), ButtonMap.padKeys(values, null)[96])

        // A third comes in: × by L1, so L1's button takes B, and ○ keeps A.
        assertTrue(ButtonMap.assignPad(values, "south", "l1", null))
        assertEquals("l1", ButtonMap.padSource(values, "south", null))
        assertEquals("east", ButtonMap.padSource(values, "l1", null))
        assertEquals("south", ButtonMap.padSource(values, "east", null))
        assertEquals("each button of the controller presses one", 16, ButtonMap.padKeys(values, null).size)

        // Back on its own: the key is gone, and nothing else moves.
        ButtonMap.clearPad(values, "south", null)
        assertFalse(values.containsKey("pad_button_south"))
        assertEquals("south", ButtonMap.padSource(values, "south", null))
        assertEquals("south", ButtonMap.padSource(values, "east", null))
        assertEquals(listOf("south", "east"), ButtonMap.padKeys(values, null)[96])

        // Given back to itself by a press: no key is left behind.
        ButtonMap.assignPad(values, "east", "east", null)
        ButtonMap.assignPad(values, "l1", "l1", null)
        assertTrue(values.keys.none { it.startsWith(ButtonMap.PAD_PREFIX) })

        assertFalse(ButtonMap.assignPad(values, "south", "grip1", null))
        assertFalse(ButtonMap.assignPad(values, "home", "south", null))
        assertTrue(values.isEmpty())
        // What is no input is read as the button's own.
        values["pad_button_north"] = "grip1"
        assertEquals("north", ButtonMap.padSource(values, "north", null))
        values["pad_button_north"] = "none"
        assertEquals("none", ButtonMap.padSource(values, "north", null))
        assertNull(ButtonMap.padKeys(values, null)[100])
    }

    @Test
    fun aConsolesOwnGameButtonsWinInItsGames() {
        val values = HashMap<String, Any?>()
        ButtonMap.assignPad(values, "south", "east", null)

        // The PSP has all consoles' until it sets its own.
        assertEquals(ButtonMap.PadBinding("east", false, true), ButtonMap.padBinding(values, "south", "psp"))
        assertFalse(ButtonMap.hasOwn(values, "psp"))

        // △ by L1 on the PSP only: a swap within what the PSP has.
        ButtonMap.assignPad(values, "north", "l1", "psp")
        assertEquals("l1", values["pad_button_north@psp"])
        assertEquals("north", values["pad_button_l1@psp"])
        assertEquals("north", ButtonMap.padSource(values, "north", null))
        assertEquals("north", ButtonMap.padSource(values, "north", "ps2"))
        assertEquals(listOf("north"), ButtonMap.padKeys(values, "psp")[102])
        assertEquals(listOf("south"), ButtonMap.padKeys(values, "psp")[97])
        assertTrue(ButtonMap.hasOwn(values, "psp"))

        // Cleared on the PSP where all consoles have it elsewhere: written as its own.
        ButtonMap.clearPad(values, "south", "psp")
        assertEquals("south", values["pad_button_south@psp"])
        assertEquals(ButtonMap.PadBinding("south", true, false), ButtonMap.padBinding(values, "south", "psp"))
        assertEquals("east", ButtonMap.padSource(values, "south", null))
        // Cleared where all consoles have it as its own: the key taken out.
        ButtonMap.clearPad(values, "north", "psp")
        assertFalse(values.containsKey("pad_button_north@psp"))

        // On the PSP a swap with a button all consoles moved shadows it there.
        ButtonMap.assignPad(values, "west", "south", "psp")
        assertEquals("south", ButtonMap.padSource(values, "west", "psp"))
        assertEquals("west", ButtonMap.padSource(values, "south", "psp"))
        assertEquals("west", ButtonMap.padSource(values, "east", "psp"))
        assertEquals("south", ButtonMap.padSource(values, "east", null))

        // Same as all consoles: the PSP's game buttons go with its shortcuts.
        ButtonMap.assign(values, "reader_backlog", l3, "psp")
        ButtonMap.sameAsAll(values, "psp")
        assertTrue(values.keys.none { it.endsWith("@psp") })
        assertEquals("east", ButtonMap.padSource(values, "south", "psp"))
        assertEquals("east", values["pad_button_south"])
    }

    @Test
    fun theGameButtonsAreKeptInThePreferences() {
        assertTrue(ButtonMap.assignPad(preferences, "south", "east", null))
        assertTrue(ButtonMap.assignPad(preferences, "l1", "r1", "ps2"))
        assertEquals("east", preferences.getString("pad_button_south", null))
        assertEquals("r1", preferences.getString("pad_button_l1@ps2", null))
        assertEquals(listOf("l1"), ButtonMap.padKeys(preferences.all, "ps2")[r1])
        ButtonMap.clearPad(preferences, "south", null)
        assertFalse(preferences.contains("pad_button_south"))
        assertTrue(preferences.contains("pad_button_east"))
        ButtonMap.sameAsAll(preferences, "ps2")
        assertTrue(preferences.all.keys.none { it.endsWith("@ps2") })
    }

    @Test
    fun theOldButtonsDialogsMapComesAcrossOnce() {
        // "South" given the key B (97) and "East" the key A (96), as the dialog kept them: place to key.
        val values = HashMap<String, Any?>()
        assertTrue(ButtonMap.migratePlaces(values, mapOf(0 to 97, 8 to 96)))
        assertEquals("east", values["pad_button_south"])
        assertEquals("south", values["pad_button_east"])
        assertEquals(2, values.size)

        // One place given another's key: that other is on nothing, as it was in a game.
        val one = HashMap<String, Any?>()
        assertTrue(ButtonMap.migratePlaces(one, mapOf(10 to 104)))
        assertEquals("l2", one["pad_button_l1"])
        assertEquals("none", one["pad_button_l2"])
        assertNull(ButtonMap.padKeys(one, null)[102])

        // A key that is no button of a controller's cannot be said: the place stays its own.
        val odd = HashMap<String, Any?>()
        assertFalse(ButtonMap.migratePlaces(odd, mapOf(0 to 98)))
        assertTrue(odd.isEmpty())

        // Not over what the screen has set since, and nothing from an empty map.
        assertFalse(ButtonMap.migratePlaces(values, mapOf(0 to 100)))
        assertEquals("east", values["pad_button_south"])
        assertFalse(ButtonMap.migratePlaces(HashMap(), emptyMap()))
    }

    @Test
    fun theWizardsAnswersAreWrittenAsPressed() {
        val values = HashMap<String, Any?>()
        // South and east pressed the other way round, and north pressed for west: north itself was passed over.
        ButtonMap.setPads(values, mapOf("south" to "east", "east" to "south", "west" to "north"), null)
        assertEquals("east", ButtonMap.padSource(values, "south", null))
        assertEquals("south", ButtonMap.padSource(values, "east", null))
        assertEquals("north", ButtonMap.padSource(values, "west", null))
        // North's own button is now west's: nothing presses north until it is given one.
        assertEquals(ButtonMap.NONE, ButtonMap.padSource(values, "north", null))
        // A button passed over whose own is nobody else's keeps it.
        assertEquals("l1", ButtonMap.padSource(values, "l1", null))
        // For one console only: all consoles' stay.
        ButtonMap.setPads(values, mapOf("south" to "south", "east" to "east"), "psp")
        assertEquals("south", ButtonMap.padSource(values, "south", "psp"))
        assertEquals("east", ButtonMap.padSource(values, "south", null))
    }
}
