// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the ARMSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import java.io.File
import java.nio.file.Files
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * Game scripts from the published list: reading the list, how a script compares with what the
 * app has, installing, updating and removing. The same cases as the desktop app's
 * reader/desktop/tests/script_index.cpp, with made-up scripts.
 */
@RunWith(RobolectricTestRunner::class)
class ScriptStoreTest {
    private val madeUp = "-- reader: no hooks\n-- reader: title 作り話\n-- reader: reads The message window\n" +
        "-- reader: version ${Console.Test.SCRIPT_GAME}\nlocal x = 1\n"
    private val older = madeUp.toByteArray()
    private val newer = (madeUp + "local y = 2\n").toByteArray()
    private val table = "あいうえお\n".toByteArray()

    private fun file(name: String, bytes: ByteArray) =
        JSONObject().put("file", name).put("sha256", ScriptStore.sha256(bytes)).put("size", bytes.size)

    private fun list(script: ByteArray, withTable: Boolean = false, api: Int = 1): ByteArray {
        val entry = file("${Console.Test.SCRIPT_GAME}.lua", script).put("id", Console.Test.SCRIPT_GAME).put("title", "作り話")
            .put("reads", "The message window").put("version", Console.Test.SCRIPT_GAME).put("api", api)
        if (withTable) entry.put("data", JSONArray().put(file("${Console.Test.SCRIPT_GAME}.charset", table)))
        return JSONObject().put("format", 1).put("system", "test").put("scripts", JSONArray().put(entry))
            .toString().toByteArray()
    }

    private fun entryOf(json: ByteArray) = ScriptStore.parseIndex(json).single()

    private fun folder() = ScriptStore.Folder(File(Files.createTempDirectory("scripts").toFile(), "profiles"))

    @Test
    fun thePublishedListIsRead() {
        val entry = entryOf(list(older, withTable = true))
        assertEquals(Console.Test.SCRIPT_GAME, entry.id)
        assertEquals("作り話", entry.title)
        assertEquals("${Console.Test.SCRIPT_GAME}.lua", entry.script.file)
        assertEquals(ScriptStore.sha256(older), entry.script.sha256)
        assertEquals(listOf("${Console.Test.SCRIPT_GAME}.charset"), entry.data.map { it.file })
        assertThrows(ScriptStore.ScriptException::class.java) { ScriptStore.parseIndex("not json".toByteArray()) }
        // A format from a later app is refused, not half read.
        assertThrows(ScriptStore.ScriptException::class.java) {
            ScriptStore.parseIndex("""{"format": 2, "scripts": []}""".toByteArray())
        }
        // An entry whose file is a path, or not its game's script, is passed over.
        val root = JSONObject(String(list(older)))
        val wrong = root.getJSONArray("scripts").getJSONObject(0)
        root.put("scripts", JSONArray()
            .put(JSONObject(wrong.toString()).put("file", "../${Console.Test.SCRIPT_GAME}.lua"))
            .put(JSONObject(wrong.toString()).put("id", Console.Test.OTHER_SCRIPT_GAME)))
        assertTrue(ScriptStore.parseIndex(root.toString().toByteArray()).isEmpty())
    }

    @Test
    fun whichFilesAScriptMayBeOrAskFor() {
        assertEquals(Console.Test.SCRIPT_GAME, ScriptStore.idOfFile("${Console.Test.SCRIPT_GAME}.lua"))
        assertEquals(Console.Test.SCRIPT_GAME, ScriptStore.idOfFile("${Console.Test.SCRIPT_GAME}.table.bin"))
        assertNull(ScriptStore.idOfFile(".installed.json"))
        assertNull(ScriptStore.idOfFile("${Console.Test.SCRIPT_GAME}..lua"))
        assertNull(ScriptStore.idOfFile("../${Console.Test.SCRIPT_GAME}.lua"))
        assertNull(ScriptStore.idOfFile("notes.txt"))
    }

    @Test
    fun theApiAScriptNeeds() {
        assertEquals(1, ScriptStore.apiOf(madeUp))
        assertEquals(3, ScriptStore.apiOf("-- reader: title x\n-- reader: api 3\nlocal x\n"))
        assertEquals(1, ScriptStore.apiOf("local x\n-- reader: api 3\n"))
    }

    @Test
    fun howTheListsScriptComparesWithWhatTheAppHas() {
        val entry = entryOf(list(newer))
        val oldSha = ScriptStore.sha256(older)
        val own = (madeUp + "-- mine\n").toByteArray()
        assertEquals(ScriptStore.State.NOT_INSTALLED, ScriptStore.stateOf(entry, null, null, ""))
        assertEquals(ScriptStore.State.INSTALLED, ScriptStore.stateOf(entry, newer, null, ""))
        assertEquals(ScriptStore.State.INSTALLED, ScriptStore.stateOf(entry, null, newer, ""))
        assertEquals(ScriptStore.State.UPDATE, ScriptStore.stateOf(entry, null, older, ""))
        assertEquals(ScriptStore.State.UPDATE, ScriptStore.stateOf(entry, older, null, oldSha))
        assertEquals(ScriptStore.State.OWN, ScriptStore.stateOf(entry, older, null, ""))
        assertEquals(ScriptStore.State.OWN, ScriptStore.stateOf(entry, own, null, oldSha))
        assertEquals(ScriptStore.State.OWN, ScriptStore.stateOf(entry, own, newer, ""))
        assertEquals(ScriptStore.State.TOO_NEW,
            ScriptStore.stateOf(entryOf(list(older, api = ScriptStore.API + 1)), null, null, ""))
    }

    @Test
    fun installingFromTheListUpdatingRemoving() {
        val folder = folder()
        val entry = entryOf(list(older, withTable = true))
        // A file that is not the one the list names is refused, and nothing is written.
        assertThrows(ScriptStore.ScriptException::class.java) {
            folder.install(entry, mapOf("${Console.Test.SCRIPT_GAME}.lua" to newer, "${Console.Test.SCRIPT_GAME}.charset" to table))
        }
        assertNull(folder.script(Console.Test.SCRIPT_GAME))
        assertThrows(ScriptStore.ScriptException::class.java) { folder.install(entry, mapOf("${Console.Test.SCRIPT_GAME}.lua" to older)) }

        folder.install(entry, mapOf("${Console.Test.SCRIPT_GAME}.lua" to older, "${Console.Test.SCRIPT_GAME}.charset" to table))
        assertTrue(folder.script(Console.Test.SCRIPT_GAME)!!.contentEquals(older))
        assertTrue(File(folder.dir, "${Console.Test.SCRIPT_GAME}.charset").isFile)
        assertEquals(ScriptStore.sha256(older), folder.recorded(Console.Test.SCRIPT_GAME))

        val update = entryOf(list(newer))
        assertEquals(ScriptStore.State.UPDATE,
            ScriptStore.stateOf(update, folder.script(update.id), null, folder.recorded(update.id)))
        folder.install(update, mapOf("${Console.Test.SCRIPT_GAME}.lua" to newer))
        assertTrue(folder.script(Console.Test.SCRIPT_GAME)!!.contentEquals(newer))

        folder.install(entry, mapOf("${Console.Test.SCRIPT_GAME}.lua" to older, "${Console.Test.SCRIPT_GAME}.charset" to table))
        assertTrue(folder.remove(Console.Test.SCRIPT_GAME))
        assertNull(folder.script(Console.Test.SCRIPT_GAME))
        assertFalse(File(folder.dir, "${Console.Test.SCRIPT_GAME}.charset").exists())
        assertEquals("", folder.recorded(Console.Test.SCRIPT_GAME))
        assertFalse(folder.remove(Console.Test.SCRIPT_GAME))
    }

    @Test
    fun aGamesNamesDictionaryComesWithItsScript() {
        val names = "PK made-up names".toByteArray()
        fun withNames(name: String): ByteArray {
            val root = JSONObject(String(list(older)))
            root.getJSONArray("scripts").getJSONObject(0).put("names", file(name, names))
            return root.toString().toByteArray()
        }
        val folder = folder()
        val entry = entryOf(withNames("names-made-up.zip"))
        assertEquals("names-made-up.zip", entry.names?.file)
        assertTrue(folder.needsNames(entry))
        // Without the names fetched, the script goes in all the same.
        folder.install(entry, mapOf("${Console.Test.SCRIPT_GAME}.lua" to older))
        assertTrue(folder.needsNames(entry))
        // Names that are not the list's are refused; the list's are written.
        assertThrows(ScriptStore.ScriptException::class.java) { folder.installNames(entry, "other".toByteArray()) }
        folder.installNames(entry, names)
        assertFalse(folder.needsNames(entry))
        // A names file that is not one is passed over; the script is still listed.
        assertNull(entryOf(withNames("../names.zip")).names)
        assertTrue(ScriptStore.isNamesFile("names-steins-gate.zip"))
        assertFalse(ScriptStore.isNamesFile("names-Steins.zip"))
    }

    @Test
    fun installingFilesTheUserChose() {
        val folder = folder()
        folder.install(entryOf(list(newer)), mapOf("${Console.Test.SCRIPT_GAME}.lua" to newer))
        val (ids, problems) = folder.installFiles(listOf(
            "${Console.Test.SCRIPT_GAME}.lua" to older,
            "${Console.Test.SCRIPT_GAME}.charset" to table,
            "my script.lua" to older,
            "${Console.Test.OTHER_SCRIPT_GAME}.lua" to byteArrayOf(0xff.toByte(), 0xfe.toByte(), 0, 1),
        ))
        assertEquals(listOf(Console.Test.SCRIPT_GAME), ids)
        assertTrue(problems.joinToString(), problems.any { it.startsWith("my script.lua") })
        assertTrue(problems.joinToString(), problems.any { it.startsWith("${Console.Test.OTHER_SCRIPT_GAME}.lua") })
        assertTrue(folder.script(Console.Test.SCRIPT_GAME)!!.contentEquals(older))
        assertTrue(File(folder.dir, "${Console.Test.SCRIPT_GAME}.charset").isFile)
        assertFalse(File(folder.dir, "${Console.Test.OTHER_SCRIPT_GAME}.lua").exists())
        assertEquals("", folder.recorded(Console.Test.SCRIPT_GAME))
    }

    @Test
    fun aFileOfTheListIsFetchedFromBesideIt() {
        assertEquals(
            "https://example.invalid/repo/reader/profiles/${Console.Test.SCRIPT_GAME}.lua",
            ScriptStore.urlOf("${Console.Test.SCRIPT_GAME}.lua", "https://example.invalid/repo/reader/profiles/index.json")
        )
    }
}
