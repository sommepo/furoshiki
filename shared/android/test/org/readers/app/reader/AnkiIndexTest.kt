// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import java.io.File
import org.readers.app.reader.anki.AnkiConnectClient
import org.readers.app.reader.anki.AnkiIndex
import org.readers.app.reader.anki.AnkiSearch
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * The copy of the deck kept on the device, read through [AnkiConnectStub]: what it answers at
 * once, how it is kept up to date, its file. No real Anki is involved.
 */
class AnkiIndexTest {
    @get:Rule
    val folder = TemporaryFolder()

    private lateinit var stub: AnkiConnectStub

    /** The notes of the pretend collection: id to deck and word. */
    private val notes = LinkedHashMap<Long, Pair<String, String>>()
    private val edited = HashSet<Long>()

    private val source get() = AnkiIndex.Source("ankiconnect\u001f" + stub.url, DECK, FIELD)

    @Before
    fun setUp() {
        stub = AnkiConnectStub()
        stub.find = { query -> find(query) }
    }

    @After
    fun tearDown() = stub.close()

    private fun set(id: Long, deck: String, word: String) {
        notes[id] = deck to word
        stub.noteFields = notes.mapValues { mapOf(FIELD to it.value.second) }
    }

    /** The searches AnkiSearch makes: `"deck:D" "Word:value"`, `"deck:D" "Word:_*"` (edited:1). */
    private fun find(query: String): List<Long> {
        val deck = Regex("^\"deck:([^\"]*)\"").find(query)?.groupValues?.get(1)
        val value = Regex("\"$FIELD:((?:\\\\.|[^\"\\\\])*)\"").find(query)!!.groupValues[1]
        val any = value == "_*"
        val plain = value.replace("\\", "")
        return notes.filter { (id, note) ->
            (deck == null || note.first == deck) &&
                (!query.contains("edited:1") || id in edited) &&
                (if (any) note.second.isNotEmpty() else note.second == plain)
        }.keys.toList()
    }

    @Test
    fun readsTheDeckOnceInPagesAndAnswersAtOnce() {
        for (i in 1L..450L) set(i, DECK, "word$i")
        set(1000, DECK, "<b>死ぬほど</b>&nbsp;")
        set(1001, "Other", "食べる")
        set(1002, DECK, "")
        val index = AnkiIndex()
        assertEquals(AnkiIndex.Answer.NOT_READY, index.find(source, "死ぬほど").answer)
        assertEquals(AnkiIndex.Due.FILL, index.whatIsDue(source, 1000))

        index.fill(AnkiConnectClient(stub.url, ""), source, 1000)
        assertEquals(451, index.size)
        assertEquals(1, stub.received("findNotes").size)
        assertEquals("451 notes, 200 at a time", 3, stub.received("notesInfo").size)

        assertEquals(AnkiIndex.Found(AnkiIndex.Answer.PRESENT, listOf(1000L)), index.find(source, "死ぬほど"))
        assertEquals(AnkiIndex.Answer.PRESENT, index.find(source, "WORD7").answer)
        assertEquals(AnkiIndex.Answer.ABSENT, index.find(source, "食べる").answer)
        assertEquals(AnkiIndex.Answer.NOT_READY, index.find(source.copy(scope = null), "食べる").answer)

        assertEquals(AnkiIndex.Due.NOTHING, index.whatIsDue(source, 2000))
        assertEquals(AnkiIndex.Due.REFRESH, index.whatIsDue(source, 1000 + AnkiIndex.REFRESH_AFTER_MS))
        assertEquals(AnkiIndex.Due.FILL, index.whatIsDue(source, 1000 + AnkiIndex.REFILL_AFTER_MS))
    }

    @Test
    fun readsWhatChangedAndKeepsWhatAnkiSays() {
        set(1, DECK, "猫")
        set(2, DECK, "犬")
        val index = AnkiIndex()
        val client = AnkiConnectClient(stub.url, "")
        index.fill(client, source, 0)

        set(3, DECK, "鳥")
        edited += 3
        set(2, DECK, "狼")
        edited += 2
        val before = stub.received("notesInfo").size
        index.refresh(client, source, AnkiIndex.REFRESH_AFTER_MS)
        assertEquals(AnkiIndex.Answer.PRESENT, index.find(source, "鳥").answer)
        assertEquals(listOf(2L), index.find(source, "狼").notes)
        assertEquals(AnkiIndex.Answer.ABSENT, index.find(source, "犬").answer)
        assertEquals(before + 1, stub.received("notesInfo").size)

        // A deleted note is not seen by a refresh; Anki's answer about the word takes it out.
        notes.remove(1)
        assertEquals(AnkiIndex.Answer.PRESENT, index.find(source, "猫").answer)
        index.learn(source, "猫", emptyList())
        assertEquals(AnkiIndex.Answer.ABSENT, index.find(source, "猫").answer)

        index.add(source, "魚", 77)
        assertEquals(listOf(77L), index.find(source, "魚").notes)
        index.learn(source, "虫", listOf(88L, 89L))
        assertEquals(listOf(88L, 89L), index.find(source, "虫").notes)
    }

    @Test
    fun keptInAFileAndStoppedPartWay() {
        set(1, DECK, "猫")
        val file = File(folder.root, "reader/anki_index.json")
        val first = AnkiIndex()
        assertFalse("nothing read yet: nothing to keep", first.save(file))
        first.fill(AnkiConnectClient(stub.url, ""), source, 5000)
        assertTrue(first.save(file))

        val again = AnkiIndex()
        assertTrue(again.load(file))
        assertEquals(listOf(1L), again.find(source, "猫").notes)
        assertEquals(AnkiIndex.Due.NOTHING, again.whatIsDue(source, 5001))
        assertFalse(again.load(File(folder.root, "missing.json")))

        set(2, DECK, "犬")
        again.fill(AnkiConnectClient(stub.url, ""), source, 9000) { true }
        assertEquals(AnkiIndex.Answer.ABSENT, again.find(source, "犬").answer)
        assertEquals(AnkiIndex.Answer.PRESENT, again.find(source, "猫").answer)
    }

    @Test
    fun asksForSeveralWordsInOneRequest() {
        set(1, DECK, "死ぬほど")
        set(2, DECK, "死ぬ")
        set(3, "Other", "死")
        val client = AnkiConnectClient(stub.url, "")
        assertEquals(
            listOf(listOf(1L), emptyList(), listOf(2L)),
            client.findNotesEach(DECK, FIELD, listOf("死ぬほど", "死", "死ぬ"))
        )
        assertEquals(1, stub.received("multi").size)
        assertEquals(emptyList<List<Long>>(), client.findNotesEach(DECK, FIELD, emptyList()))
        assertEquals("\"deck:Mining\" \"My\\:Word:_*\"", AnkiSearch.hasField(DECK, "My:Word"))
    }

    private companion object {
        const val DECK = "Mining"
        const val FIELD = "Word"
    }
}
