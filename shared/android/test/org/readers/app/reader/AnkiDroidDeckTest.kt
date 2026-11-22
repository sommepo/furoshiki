// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Context
import android.content.pm.ProviderInfo
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.anki.AnkiDroidClient
import org.readers.app.reader.anki.AnkiException
import org.readers.app.reader.anki.Duplicates
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

/**
 * A card goes to the deck the user set, never to another (a user's report, 2026-11-22: cards
 * went to "Mining" while "Mining deck" was set). The stand-in for AnkiDroid below adds a note as
 * AnkiDroid's provider does: to its note type's own deck, whatever deck the address names. The
 * note type here was last used with "Mining", the maintainer's own deck name, so a test that
 * only ever used "Mining" would pass with the fault in place.
 */
@RunWith(RobolectricTestRunner::class)
class AnkiDroidDeckTest {
    private val context: Context = ApplicationProvider.getApplicationContext()
    private lateinit var client: AnkiDroidClient

    @Before
    fun setUp() {
        FakeAnkiDroid.reset()
        Robolectric.buildContentProvider(FakeAnkiDroid::class.java)
            .create(ProviderInfo().apply { authority = "com.ichi2.anki.flashcards" })
        client = AnkiDroidClient(context)
    }

    private fun add(deck: String): Long = client.addNote(
        deck, "Lapis", mapOf("Expression" to "食べる", "Meaning" to "to eat"), listOf("furoshiki"), Duplicates.ALLOW
    )

    @Test
    fun cardsGoToTheDeckThatWasSet() {
        for ((deck, id) in listOf("Mining deck" to 3L, "日本語::Mining deck" to 4L, "Default" to 1L, "Mining" to 2L)) {
            val note = add(deck)
            val decks = FakeAnkiDroid.cards.getValue(note)
            assertEquals(deck, 2, decks.size)
            assertTrue("$deck: $decks", decks.all { it == id })
        }
    }

    @Test
    fun noDeckOrNoteTypeIsSetBeforeTheUserChoosesOne() {
        androidx.preference.PreferenceManager.getDefaultSharedPreferences(context).edit().clear().commit()
        val settings = ReaderSettings(context)
        assertEquals("", settings.ankiDeck)
        assertEquals("", settings.ankiModel)
    }

    @Test
    fun aMissingDeckAddsNothing() {
        try {
            add("Mining deck 2")
            fail("a card was added to a deck that does not exist")
        } catch (e: AnkiException) {
            assertTrue(e.message!!, e.message!!.contains("Mining deck 2"))
        }
        assertTrue(FakeAnkiDroid.cards.isEmpty())
    }

    @Test
    fun aCardLeftInAnotherDeckIsAnError() {
        FakeAnkiDroid.refuseMoves = true
        try {
            add("Mining deck")
            fail("a card left in the note type's deck was taken as added")
        } catch (e: AnkiException) {
            assertTrue(e.message!!, e.message!!.contains("Mining deck"))
        }
    }

    /** AnkiDroid's provider, as far as adding a note goes (CardContentProvider, 2026). */
    class FakeAnkiDroid : ContentProvider() {
        companion object {
            val decks = linkedMapOf("Default" to 1L, "Mining" to 2L, "Mining deck" to 3L, "日本語::Mining deck" to 4L)
            /** Each note's cards, by ordinal: the deck each is in. */
            val cards = LinkedHashMap<Long, MutableList<Long>>()
            var refuseMoves = false
            private const val MODEL_ID = 10L
            /** The deck the note type was last used with, where AnkiDroid puts a new note. */
            private const val MODEL_DECK = 2L
            private var nextNote = 100L

            fun reset() {
                cards.clear()
                refuseMoves = false
                nextNote = 100L
            }
        }

        override fun onCreate() = true
        override fun getType(uri: Uri): String? = null
        override fun delete(uri: Uri, selection: String?, args: Array<out String>?) = 0

        override fun query(
            uri: Uri, projection: Array<out String>?, selection: String?, args: Array<out String>?, sort: String?
        ): Cursor {
            val path = uri.pathSegments
            return when {
                path == listOf("decks") -> MatrixCursor(arrayOf("deck_name", "deck_id")).apply {
                    decks.forEach { (name, id) -> addRow(arrayOf<Any>(name, id)) }
                }
                path == listOf("models") -> MatrixCursor(arrayOf("_id", "name", "field_names")).apply {
                    addRow(arrayOf<Any>(MODEL_ID, "Lapis", "Expression\u001fMeaning"))
                }
                path == listOf("notes") -> MatrixCursor(arrayOf("_id"))
                path.size == 3 && path[0] == "notes" && path[2] == "cards" -> {
                    val out = MatrixCursor(projection ?: arrayOf("ord", "deck_id"))
                    cards[path[1].toLong()]?.forEachIndexed { ord, did ->
                        out.addRow(out.columnNames.map { if (it == "ord") ord.toString() else did }.toTypedArray<Any>())
                    }
                    out
                }
                else -> throw IllegalArgumentException("not in the stand-in: $uri")
            }
        }

        override fun insert(uri: Uri, values: ContentValues?): Uri {
            require(uri.pathSegments == listOf("notes")) { "not in the stand-in: $uri" }
            require(values!!.getAsLong("mid") == MODEL_ID)
            // As AnkiDroid: col.addNote(newNote, newNote.notetype.did). A deckId on the address
            // is not read here.
            val id = nextNote++
            cards[id] = mutableListOf(MODEL_DECK, MODEL_DECK)
            return Uri.withAppendedPath(uri, id.toString())
        }

        override fun update(uri: Uri, values: ContentValues?, selection: String?, args: Array<out String>?): Int {
            val path = uri.pathSegments
            require(path.size == 4 && path[0] == "notes" && path[2] == "cards") { "not in the stand-in: $uri" }
            if (refuseMoves) return 0
            val did = values!!.getAsLong("deck_id")
            require(did in decks.values) { "no deck $did" }
            cards.getValue(path[1].toLong())[path[3].toInt()] = did
            return 1
        }
    }
}
