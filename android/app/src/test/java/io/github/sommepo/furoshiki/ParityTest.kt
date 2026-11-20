// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import java.io.File
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** What the app took from the desktop program on 2026-11-13 (home/docs/android-parity.md), as far as it is plain logic. */
class ParityTest {
    @get:Rule
    val folder = TemporaryFolder()

    // ---- The study tracker's answer ----

    private val media = """{"items":[
        {"jitenDeckId":283,"originalTitle":"STEINS;GATE","characterCount":695712,"characterProgress":138093,"uniqueWordCount":16181,
         "difficulty":2.93,"readingSpeed":null,"sessions":26,"immersionHours":15.35,"lastLoggedOn":"2026-11-10","jiten":{"coverage":84.23}},
        {"jitenDeckId":null,"originalTitle":"Twilight Syndrome","characterCount":0,"characterProgress":5000,"lastLoggedOn":null}]}"""

    @Test
    fun theTrackersTitlesAreKeptByDeckAndByTitle() {
        val kept = Tracker.read(media, "http://tracker", 42)!!
        val steins = kept.getJSONObject("jiten").getJSONObject("283")
        assertEquals(138093L, steins.getLong("read"))
        assertEquals(695712L, steins.getLong("total"))
        assertEquals(84.23, steins.getDouble("coverage"), 0.001)
        assertEquals(0, steins.getInt("speed"))
        assertEquals("2026-11-10", steins.getString("last"))
        // A title Jiten does not have is found by the tracker's own title alone, and a day never logged is no day.
        assertFalse(kept.getJSONObject("jiten").has("0"))
        assertEquals("", kept.getJSONObject("title").getJSONObject("Twilight Syndrome").getString("last"))
        assertEquals(42L, kept.getLong("at"))
    }

    @Test
    fun anEmptyAnswerLeavesWhatWasSaidBefore() {
        assertNull(Tracker.read("""{"items":[]}""", "http://tracker", 1))
    }

    @Test
    fun theDeviceThatReadAGameLastIsAnotherOne() {
        val stats = """{"devices":[
            {"id":"a","device":"Thor","days":[{"game":"ULJM05887","day":"2026-11-12","chars":900}]},
            {"id":"b","device":"Z13","days":[{"game":"ULJM05887","day":"2026-11-09","chars":50},{"game":"SLPM-1","day":"2026-11-01","chars":0}]},
            {"id":"c","device":"","app":"furoshiki","days":[{"game":"ULJM05887","day":"2026-11-10","chars":10}]}]}"""
        val elsewhere = Services.readElsewhere(stats, "thor")
        assertEquals("furoshiki", elsewhere.getJSONObject("ULJM05887").getString("device"))
        assertEquals("2026-11-10", elsewhere.getJSONObject("ULJM05887").getString("day"))
        // A day with nothing read is no reading.
        assertFalse(elsewhere.has("SLPM-1"))
    }

    @Test
    fun aNewerBuildOnTheFilesPageIsSeen() {
        val index = """{"items":[{"id":"furochibi","version":"3.3","url":"/x"},{"id":"furoshiki-android","version":"0.10.0","url":"/downloads/data/Furoshiki-android.apk.bin","file":"Furoshiki-android.apk"}]}"""
        val update = Services.readUpdate(index, "https://files")
        assertEquals("https://files/downloads/data/Furoshiki-android.apk.bin", update.getString("url"))
        assertTrue(Services.newer(update.getString("version"), "0.9.4"))
        assertFalse(Services.newer("0.3.0", "0.3.0"))
        assertFalse(Services.newer("0.2.9", "0.3"))
        assertEquals(0, Services.readUpdate("""{"items":[]}""", "https://files").length())
    }

    // ---- Buttons for the keys ----

    @Test
    fun theOldShortcutButtonsBecomeTheControllerScreensShortcuts() {
        // What the controller screen had: the backlog on R3, the pointer on L2 by itself.
        val values = HashMap<String, Any?>(mapOf("reader_button_action_107" to "reader_backlog"))
        val old = ActionKeys.parse("""{"pause":105,"card":104,"log":106,"ocr_now":103,"fast":100,"quick_menu":109}""")
        assertTrue(ActionKeys.migrateInto(values, old))
        val map = io.github.sommepo.furoshiki.reader.ButtonMap
        assertEquals(105, map.binding(values, "reader_pause_game", null).button)
        // A button given in the old list did its action before anything else: it takes L2 from the pointer.
        assertEquals(104, map.binding(values, "reader_sentence_card", null).button)
        assertEquals(-1, map.binding(values, "pointer", null).button)
        // An action is on one button: the backlog leaves R3 for the button the old list gave it.
        assertEquals(106, map.binding(values, "reader_backlog", null).button)
        assertNull(map.action(values, 107, null))
        // The actions only the old list had are the controller screen's now.
        assertEquals(103, map.binding(values, "furoshiki_ocr_read", null).button)
        assertEquals(100, map.binding(values, "furoshiki_fast_forward", null).button)
        assertEquals(109, map.binding(values, "reader_menu", null).button)
        // Every action the old list had has one there.
        assertEquals(13, ActionKeys.TO_MAP.values.toSet().size)
    }

    @Test
    fun onlyOurActionsAreReadFromTheFile() {
        assertEquals(mapOf("pause" to 105), ActionKeys.parse("""{"pause":105,"dance":3,"card":0}"""))
        assertEquals(emptyMap<String, Int>(), ActionKeys.parse("not json"))
    }

    @Test
    fun aButtonIsCalledWhatTheControllerScreenCallsIt() {
        // Never Android's raw name ("THUMBL", "DPAD_UP"): one form on every screen.
        assertEquals("R2", ActionKeys.plainName("KEYCODE_BUTTON_R2"))
        assertEquals("A", ActionKeys.plainName("KEYCODE_BUTTON_A"))
        assertEquals("Button 3", ActionKeys.plainName("KEYCODE_BUTTON_3"))
        assertEquals("F5", ActionKeys.plainName("KEYCODE_F5"))
        assertEquals("Volume mute", ActionKeys.plainName("KEYCODE_VOLUME_MUTE"))
    }

    // ---- Add game from another app: the apps Android calls games come first ----

    @Test
    fun anAppIsAGameByItsCategoryOrItsOlderFlag() {
        assertTrue(Device.isGame(android.content.pm.ApplicationInfo.CATEGORY_GAME, 0))
        @Suppress("DEPRECATION")
        assertTrue(Device.isGame(android.content.pm.ApplicationInfo.CATEGORY_UNDEFINED, android.content.pm.ApplicationInfo.FLAG_IS_GAME))
        assertFalse(Device.isGame(android.content.pm.ApplicationInfo.CATEGORY_PRODUCTIVITY, 0))
        assertFalse(Device.isGame(-1, 0))
    }

    // ---- A console is called by its name, never its key ----

    @Test
    fun aConsoleHasANameForATabAndOneForASentence() {
        assertEquals("PS Vita", Places.shortName("psvita"))
        assertEquals("Dreamcast", Places.shortName("dc"))
        assertEquals("DS", Places.shortName("nds"))
        assertEquals("Nintendo 3DS", Places.fullName("3ds"))
        assertEquals("PlayStation", Places.fullName("ps1"))
    }

    // ---- Explain's kept answers ----

    @Test
    fun anAnswerIsKeptByItsLineAndTheOldestDropped() {
        var kept = JSONObject()
        for (line in listOf("一", "二", "三")) kept = Explain.keep(kept, line, JSONObject().put("translation", line), most = 2)
        val answers = kept.getJSONObject("answers")
        assertFalse(answers.has(Explain.keyOf("一")))
        assertEquals("三", answers.getJSONObject(Explain.keyOf(" 三 ")).getString("translation"))
        assertEquals(2, kept.getJSONArray("order").length())
        // Asked again: the new answer in the old one's place.
        kept = Explain.keep(kept, "三", JSONObject().put("translation", "three"), most = 2)
        assertEquals(2, kept.getJSONArray("order").length())
        assertEquals("three", kept.getJSONObject("answers").getJSONObject(Explain.keyOf("三")).getString("translation"))
        // The desktop's key: the SHA-1 of the trimmed line.
        assertEquals("da39a3ee5e6b4b0d3255bfef95601890afd80709", Explain.keyOf("  "))
    }

    // ---- Widescreen ----

    private fun wide(key: String, vararg shapes: String) = PlaySetting(key, "Wide picture", PlaySetting.CHOICE, "off", choices = listOf("off", *shapes))

    @Test
    fun widescreenSetsEveryConsoleThatHasTheShape() {
        val settings = Wide.settings(mapOf("ps2" to listOf(wide("ps2.wide", "16:9", "21:9", "32:9")), "switch" to listOf(wide("switch.wide", "21:9", "32:9")),
            "psp" to listOf(PlaySetting("sound.volume", "Volume", PlaySetting.NUMBER, "100"))))
        assertEquals(setOf("ps2", "switch"), settings.keys)
        assertEquals(listOf("off", "16:9", "21:9", "32:9"), Wide.choices(settings))
        val play = PlaySettings(File(folder.root, "settings.json"))
        assertEquals("off", Wide.value(settings, play))
        Wide.set(settings, play, "16:9")
        assertEquals("16:9", play.raw("ps2.wide", "ps2"))
        // The Switch has no recipe for 16:9: off there.
        assertEquals("off", play.raw("switch.wide", "switch"))
        assertEquals("16:9", Wide.value(settings, play))
        assertTrue(Wide.choices(Wide.settings(mapOf("psp" to emptyList()))).isEmpty())
    }

    // ---- Game add-ons ----

    @Test
    fun anAddOnsScriptsAndSettingsGoInOnce() {
        val games = folder.newFolder("Games")
        val addon = File(games, "Addons/Kakuremino").apply { mkdirs() }
        File(addon, "addon.json").writeText("""{"format":1,"name":"Kakuremino","console":"ps2","games":["SLPM-67002"],"scripts":"profile","settings":{"picture.aspect":"stretch"}}""")
        File(addon, "profile").mkdirs()
        File(addon, "profile/SLPM-67002.lua").writeText("-- the add-on's")
        File(games, "Addons/Broken").mkdirs()
        File(games, "Addons/Broken/addon.json").writeText("{")
        File(games, "Addons/NoGames").mkdirs()
        File(games, "Addons/NoGames/addon.json").writeText("""{"name":"NoGames"}""")
        val found = Addons.find(listOf(games))
        assertEquals(listOf("Kakuremino"), found.map { it.name })

        val scripts = folder.newFolder("profiles")
        File(scripts, "SLPM-67002.lua").writeText("-- my own")
        val play = PlaySettings(File(folder.root, "settings.json"))
        val addons = Addons(File(folder.root, "addons.json"), scripts, play)
        assertEquals(emptyList<String>(), addons.apply(found, "ps2", "SLPM-00000"))
        assertEquals(listOf("Kakuremino"), addons.apply(found, "ps2", "SLPM-67002"))
        assertEquals("-- the add-on's", File(scripts, "SLPM-67002.lua").readText())
        assertEquals("-- my own", File(scripts, "SLPM-67002.lua.before-addon").readText())
        assertEquals("stretch", play.raw("picture.aspect", "ps2", "SLPM-67002"))

        // The user takes the setting out and changes the add-on's script: the setting stays out, the script is the add-on's again,
        // and the file kept aside is still the user's first own.
        play.set("picture.aspect", "", "ps2", "SLPM-67002")
        File(scripts, "SLPM-67002.lua").writeText("-- changed")
        addons.apply(found, "ps2", "SLPM-67002")
        assertEquals("", PlaySettings(File(folder.root, "settings.json")).raw("picture.aspect", "ps2", "SLPM-67002"))
        assertEquals("-- the add-on's", File(scripts, "SLPM-67002.lua").readText())
        assertEquals("-- my own", File(scripts, "SLPM-67002.lua.before-addon").readText())
    }
}
