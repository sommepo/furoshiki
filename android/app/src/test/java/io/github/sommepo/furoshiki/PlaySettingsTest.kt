// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class PlaySettingsTest {
    @get:Rule
    val folder = TemporaryFolder()

    private val volume = PlaySetting("sound.volume", "Volume", PlaySetting.NUMBER, "100", 0.0, 100.0, 1.0)
    private val blending =
        PlaySetting("ps2.blending", "Blending", PlaySetting.CHOICE, "basic", choices = listOf("basic", "full"))
    private val file: File get() = File(folder.root, "settings.json")

    @Test
    fun theFirstLayerThatHasItWins() {
        val play = PlaySettings(file)
        assertEquals(PlaySettings.Layer.DEFAULT, play.value(volume, "ps2", "SLPM-1").from)
        play.set("sound.volume", "80")
        assertEquals("80", play.value(volume, "ps2", "SLPM-1").value)
        assertEquals(PlaySettings.Layer.ALL, play.value(volume, "ps2", "SLPM-1").from)
        play.set("sound.volume", "60", "ps2")
        assertEquals("60", play.value(volume, "ps2", "SLPM-1").value)
        assertEquals("80", play.value(volume, "psp").value)
        play.set("sound.volume", "40", "ps2", "SLPM-1")
        assertEquals("40", play.value(volume, "ps2", "SLPM-1").value)
        assertEquals("60", play.value(volume, "ps2", "SLPM-2").value)
    }

    @Test
    fun anEmptyValueLetsTheNextLayerShow() {
        val play = PlaySettings(file)
        play.set("sound.volume", "80")
        play.set("sound.volume", "60", "ps2")
        play.set("sound.volume", "", "ps2")
        assertFalse(play.isSet("sound.volume", "ps2"))
        assertEquals("80", play.value(volume, "ps2").value)
    }

    @Test
    fun aConsolesOwnIsNeverTakenFromAllConsoles() {
        val play = PlaySettings(file)
        play.set("ps2.blending", "full")
        assertEquals("basic", play.value(blending, "ps2").value)
    }

    @Test
    fun theFileIsTheDesktopProgramsForm() {
        PlaySettings(file).apply {
            set("sound.volume", "80")
            set("picture.resolution", "3", "ps1")
            set("ps1.true_color", "1", "ps1", "SLPS-01645")
        }
        val root = JSONObject(file.readText())
        assertEquals(1, root.getInt("format"))
        assertEquals("80", root.getJSONObject("all").getString("sound.volume"))
        assertEquals("3", root.getJSONObject("consoles").getJSONObject("ps1").getString("picture.resolution"))
        assertEquals(
            "1",
            root.getJSONObject("games").getJSONObject("ps1").getJSONObject("SLPS-01645").getString("ps1.true_color"),
        )
    }

    @Test
    fun aSettingLastsAcrossAProcess() {
        PlaySettings(file).set("sound.volume", "70", "psp")
        assertTrue(PlaySettings(file).isSet("sound.volume", "psp"))
    }

    @Test
    fun aCoreIsHandedOnlyWhatWasSet() {
        val play = PlaySettings(file)
        val crt = PlaySetting("picture.crt", "CRT", PlaySetting.TEXT, "")
        play.set("picture.crt", "x")
        play.set("ps2.blending", "full", "ps2")
        assertEquals(listOf("ps2.blending" to "full"), play.valuesFor("ps2", "SLPM-1", listOf(volume, blending, crt)))
    }

    @Test
    fun thePageSaysWhereAValueComesFrom() {
        val play = PlaySettings(file)
        play.set("sound.volume", "80")
        val inherited = play.page(listOf(volume), "ps2").getJSONObject(0)
        assertEquals("Same as all consoles", inherited.getString("from"))
        assertFalse(inherited.getBoolean("set"))
        play.set("sound.volume", "60", "ps2")
        val own = play.page(listOf(volume), "ps2").getJSONObject(0)
        assertEquals("", own.getString("from"))
        assertEquals("60", own.getString("value"))
        assertEquals("Same as its console", play.page(listOf(volume), "ps2", "SLPM-1").getJSONObject(0).getString("from"))
    }

    @Test
    fun aCoresDescriptionBecomesItsSettings() {
        val info = JSONObject(
            """{"settings":[{"key":"picture.resolution","label":"","kind":1,"default":"1","low":1,"high":8,"step":1},
            {"key":"3ds.layout","label":"Layout (from the next game)","kind":2,"default":"top_bottom","choices":["top_bottom","large_screen"]},
            {"key":"picture.renderer","label":"","kind":2,"default":"opengl"}]}""",
        )
        val settings = PlaySetting.list("3ds", info)
        assertEquals("Internal resolution", settings[0].label)
        assertEquals("3", settings[0].default) // what GameActivity starts a 3DS with
        assertEquals("1", PlaySetting.list("psp", info)[0].default)
        assertEquals("Layout", settings[1].label)
        assertEquals(listOf("top_bottom", "large_screen"), settings[1].choices)
        assertTrue(settings[2].hidden)
        assertEquals("3×", settings[0].shown("3"))
    }
}
