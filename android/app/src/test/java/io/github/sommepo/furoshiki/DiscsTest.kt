// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import java.io.File
import java.nio.file.Files
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** A game on several discs is one title, its discs found beside the one chosen. */
@RunWith(RobolectricTestRunner::class)
class DiscsTest {
    @Test
    fun theDiscsNumberAndTheNameWithoutIt() {
        assertEquals(2, Discs.number("Final Fantasy VII (Japan) (Disc 2).cue"))
        assertEquals(1, Discs.number("Game (Disc 1 of 2).chd"))
        assertNull(Discs.number("Kamaitachi (Japan).cue"))
        assertEquals("Aconcagua", Discs.withoutDisc("Aconcagua (Disc 1)"))
        assertEquals(
            Discs.groupKey("/g/Final Fantasy VII (Japan) (Disc 1).cue"),
            Discs.groupKey("/g/Final Fantasy VII (Japan) (Disc 3).cue"),
        )
    }

    @Test
    fun aGamesDiscsAreFoundBesideItInOrder() {
        val folder = Files.createTempDirectory("discs").toFile()
        try {
            for (n in listOf(3, 1, 2)) File(folder, "FF7 (Japan) (Disc $n).cue").writeText("")
            File(folder, "FF7 (Japan) (Disc 1).bin").writeText("")
            File(folder, "Other (Japan) (Disc 1).cue").writeText("")
            val discs = Discs.of(File(folder, "FF7 (Japan) (Disc 2).cue").path).map { File(it).name }
            assertEquals(listOf("FF7 (Japan) (Disc 1).cue", "FF7 (Japan) (Disc 2).cue", "FF7 (Japan) (Disc 3).cue"), discs)
            assertEquals(listOf("/x/One (Japan).iso"), Discs.of("/x/One (Japan).iso"))
        } finally {
            folder.deleteRecursively()
        }
    }

    @Test
    fun theScansListHasOneEntryAGame() {
        fun entry(name: String, title: String) = JSONObject().put("path", "/g/$name").put("title", title).put("id", "X")
        val found = JSONArray()
            .put(entry("Aconcagua (Japan) (Disc 2).cue", "Aconcagua (Disc 2)"))
            .put(entry("Aconcagua (Japan) (Disc 1).cue", "Aconcagua (Disc 1)"))
            .put(entry("Kamaitachi (Japan).cue", "Kamaitachi"))
        val one = ScanService.oneEntryAGame(found)
        assertEquals(2, one.length())
        val aconcagua = one.getJSONObject(0)
        assertEquals("Aconcagua", aconcagua.getString("title"))
        assertEquals("/g/Aconcagua (Japan) (Disc 1).cue", aconcagua.getString("path"))
        assertEquals(2, aconcagua.getInt("discs"))
    }
}
