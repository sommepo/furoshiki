// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.companion

import io.github.sommepo.furoshiki.companion.FrontendExports.Export
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FrontendExportsTest {
    @Test
    fun gameNativesFilesAreANumberEach() {
        // As GameNative's FrontendSyncManager writes them: the number alone, no newline.
        assertEquals(Export.GameNative("Broforce", "STEAM", 274190), FrontendExports.read("Broforce.steam", "/x/Broforce.steam", "274190"))
        assertEquals(Export.GameNative("Half-Life 2", "STEAM", 220), FrontendExports.read("Half-Life 2.steam", "/x", "220\n"))
        assertEquals("EPIC", (FrontendExports.read("A.epic", "/x", "12") as Export.GameNative).store)
        assertEquals("GOG", (FrontendExports.read("A.GOG", "/x", "12") as Export.GameNative).store)
        assertEquals("AMAZON", (FrontendExports.read("A.amazon", "/x", "12") as Export.GameNative).store)
        assertEquals("CUSTOM_GAME", (FrontendExports.read("洞窟物語.pcgame", "/x", "3") as Export.GameNative).store)
        // Not a number, nothing, a GOG game GameNative could not number (0), another kind of file.
        assertNull(FrontendExports.read("A.steam", "/x", "Half-Life"))
        assertNull(FrontendExports.read("A.steam", "/x", ""))
        assertNull(FrontendExports.read("A.gog", "/x", "0"))
        assertNull(FrontendExports.read("A.iso", "/x", "12"))
        assertNull(FrontendExports.read(".steam", "/x", "12"))
    }

    @Test
    fun winlatorsShortcuts() {
        val shortcut = "[Desktop Entry]\nName=Cave Story\nExec=env WINEPREFIX=\"/data/.wine\" wine \"C:\\\\Doukutsu.exe\"\nType=Application\n"
        assertEquals(Export.Winlator("Cave Story", "/s/Cave.desktop"), FrontendExports.read("Cave.desktop", "/s/Cave.desktop", shortcut))
        // No name: the file's.
        assertEquals("Cave", FrontendExports.read("Cave.desktop", "/s", "[Desktop Entry]\nExec=wine x.exe")?.title)
        // A desktop file of anything else (no wine, no entry) is none.
        assertNull(FrontendExports.read("a.desktop", "/s", "[Desktop Entry]\nName=Files\nExec=nautilus"))
        assertNull(FrontendExports.read("a.desktop", "/s", "Name=x\nExec=wine x"))
    }

    @Test
    fun whatStartsTheGame() {
        val game = Export.GameNative("Broforce", "STEAM", 274190)
        val launch = FrontendExports.launchOf(game, setOf("app.gamenative"))!!
        assertEquals("app.gamenative", launch.app)
        assertEquals("app.gamenative.MainActivity", launch.activity)
        assertEquals("app.gamenative.LAUNCH_GAME", launch.action)
        assertEquals(mapOf("app_id" to 274190), launch.ints)
        assertEquals(mapOf("game_source" to "STEAM"), launch.strings)
        // The gold build alone; none yet (the release's, for when it is installed).
        assertEquals("app.gamenative.gold", FrontendExports.launchOf(game, setOf("app.gamenative.gold"))!!.app)
        assertEquals("app.gamenative", FrontendExports.launchOf(game, emptySet())!!.app)

        val shortcut = Export.Winlator("Cave Story", "/sdcard/WinNative/Shorcuts/Cave.desktop")
        assertNull(FrontendExports.launchOf(shortcut, emptySet()))
        val winlator = FrontendExports.launchOf(Export.Winlator("A", "/s/A.desktop"), setOf("com.winlator.cmod", "com.winnative.cmod"))!!
        assertEquals("com.winlator.cmod" to "com.winlator.cmod.XServerDisplayActivity", winlator.app to winlator.activity)
        assertEquals(mapOf("shortcut_path" to "/s/A.desktop"), winlator.strings)
        assertNull(winlator.action)
        // WinNative's own folder: WinNative, where it is installed.
        assertEquals("com.winnative.cmod", FrontendExports.launchOf(shortcut, setOf("com.winlator.cmod", "com.winnative.cmod"))!!.app)
    }

    @Test
    fun aLaunchKeptAndReadBack() {
        val launch = FrontendExports.launchOf(Export.GameNative("A", "GOG", 1207658924), setOf("app.gamenative"))!!
        assertEquals(launch, FrontendExports.Launch.of(JSONObject(launch.toJson().toString())))
        val winlator = FrontendExports.Launch("com.winlator", "com.winlator.XServerDisplayActivity", null, strings = mapOf("shortcut_path" to "/a"), clearTask = true)
        assertEquals(winlator, FrontendExports.Launch.of(JSONObject(winlator.toJson().toString())))
        assertNull(FrontendExports.Launch.of(null))
        assertNull(FrontendExports.Launch.of(JSONObject()))
    }

    @Test
    fun anIdTheSameOnEveryLook() {
        assertEquals("APP-STEAM-274190", FrontendExports.idOf(Export.GameNative("Broforce", "STEAM", 274190)))
        assertEquals("APP-CUSTOMGAME-3", FrontendExports.idOf(Export.GameNative("x", "CUSTOM_GAME", 3)))
        assertEquals("APP-W-CAVESTORY", FrontendExports.idOf(Export.Winlator("Cave Story", "/a")))
    }

    @Test
    fun theTitlesAFolderMakes() {
        val files = listOf(
            AppTitles.FoundFile("Broforce.steam", "/g/steam/Broforce.steam", "274190"),
            // The same game in another folder: once.
            AppTitles.FoundFile("Broforce.steam", "/g/windows/Broforce.steam", "274190"),
            AppTitles.FoundFile("Hidden.steam", "/g/steam/Hidden.steam", "10"),
            AppTitles.FoundFile("notes.desktop", "/g/notes.desktop", "[Desktop Entry]\nExec=gedit"),
            AppTitles.FoundFile("Cave.desktop", "/g/Cave.desktop", "[Desktop Entry]\nName=Cave Story\nExec=wine a.exe"),
        )
        val before = JSONArray().put(JSONObject().put("id", "APP-STEAM-274190").put("added", 5L))
        val list = AppTitles.exportedEntries(
            files, setOf("app.gamenative"), { if (it == "app.gamenative") "app.gamenative.MainActivityAliasDefault" else null },
            hidden = setOf("APP-STEAM-10"), before = before, now = 9L,
        )
        // No Winlator installed: its shortcut waits.
        assertEquals(1, list.length())
        val one = list.getJSONObject(0)
        assertEquals("APP-STEAM-274190", one.getString("id"))
        assertEquals("Broforce", one.getString("title"))
        assertEquals("app", one.getString("console"))
        assertEquals("GameNative", one.getString("via"))
        assertEquals(5L, one.getLong("added"))
        // Its path names the launcher's activity, which brings GameNative back to the front ...
        assertEquals("app.gamenative" to "app.gamenative.MainActivityAliasDefault", AppTitles.appOf(one.getString("path")))
        // ... and its launch starts the game.
        assertEquals(mapOf("app_id" to 274190), FrontendExports.Launch.of(one.getJSONObject("launch"))!!.ints)

        val withWinlator = AppTitles.exportedEntries(files, setOf("app.gamenative", "com.winlator"), { null }, emptySet(), now = 9L)
        assertEquals(listOf("APP-STEAM-274190", "APP-STEAM-10", "APP-W-CAVESTORY"),
            (0 until withWinlator.length()).map { withWinlator.getJSONObject(it).getString("id") })
        // No launcher activity known: the one the game is started by.
        assertEquals("app.gamenative" to "app.gamenative.MainActivity", AppTitles.appOf(withWinlator.getJSONObject(0).getString("path")))
        assertEquals(9L, withWinlator.getJSONObject(0).getLong("added"))
    }
}
