// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.dict.NamesDictionaries
import org.readers.app.reader.dict.TermEntry
import org.readers.app.reader.ui.PopupModel
import org.robolectric.RobolectricTestRunner

/** What the popup page draws for a name in the running game (the desktop's popup_model test). */
@RunWith(RobolectricTestRunner::class)
class PopupModelTest {
    private fun sense(dictionary: String, tags: String, glossary: String, scoped: Boolean = false) = TermEntry(
        dictionaryId = 1, dictionary = dictionary, priority = 0, expression = "紅莉栖", reading = "くりす",
        definitionTags = tags, rules = "", score = 0, glossaryJson = glossary, sequence = 0, termTags = "",
        scoped = scoped
    )

    private fun result(entries: List<TermEntry>) =
        LookupResult("紅莉栖", "くりす", "紅莉栖", emptyList(), entries, emptyList(), emptyList())

    @Test
    fun aNameInTheRunningGameIsTaggedOnce() {
        val name = sense("名前 · STEINS;GATE", "名前", """["牧瀬 紅莉栖（まきせ くりす）"]""", scoped = true)
        val other = sense("JMdict", "n", """["Chris"]""")
        val entry = PopupModel.entry(result(listOf(name, other)), null)
        assertTrue(entry.getBoolean("name"))
        val senses = entry.getJSONArray("senses")
        assertTrue(senses.getJSONObject(0).getBoolean("name"))
        assertEquals(0, senses.getJSONObject(0).getJSONArray("tags").length())
        assertFalse(senses.getJSONObject(1).has("name"))

        // The same entries without the game's dictionary first: no tag.
        assertFalse(PopupModel.entry(result(listOf(other)), null).has("name"))
    }

    /** A names dictionary's entry says who it is (tools/titles.py, card_gloss): the page gets a card. */
    @Test
    fun aNameWithACardIsDrawnAsOne() {
        val glossary = """[{"type":"structured-content","content":{"tag":"div","data":{"furoshiki":"name","id":"c53392",
            "kind":"person","name":"真宮寺 是清","reading":"しんぐうじ これきよ","romaji":"Shinguuji Korekiyo",
            "role":"Main character","also":"超高校級の「民俗学者」、是清","about":"He studies folklore.","image":"img/c53392.jpg"},
            "content":[{"tag":"div","content":"真宮寺 是清（しんぐうじ これきよ）"},{"tag":"div","content":"Main character"}]}}]"""
        // The dictionary's zip, with the picture: remembered as NamesDictionaries.ensure does.
        val zip = java.io.File.createTempFile("names-test", ".zip")
        java.util.zip.ZipOutputStream(zip.outputStream()).use { out ->
            out.putNextEntry(java.util.zip.ZipEntry("img/c53392.jpg"))
            out.write(byteArrayOf(1, 2, 3))
            out.closeEntry()
        }
        NamesDictionaries.remember(NamesDictionaries.Pack(zip, "test", "名前 · Test", "r1", listOf("TEST00001")))
        val name = sense("名前 · Test", "名前", glossary, scoped = true)
        val card = PopupModel.entry(result(listOf(name)), null).getJSONObject("card")
        assertEquals("真宮寺 是清", card.getString("name"))
        assertEquals("しんぐうじ これきよ", card.getString("reading"))
        assertEquals("Main character", card.getString("role"))
        assertEquals(2, card.getJSONArray("also").length())
        assertEquals("He studies folklore.", card.getString("about"))
        assertEquals("data:image/jpeg;base64,AQID", card.getString("image"))
        // Only a picture of the zip's own img/ folder; another dictionary's has none.
        assertEquals("", NamesDictionaries.picture("名前 · Test", "../index.json"))
        assertEquals("", NamesDictionaries.picture("名前 · Other", "img/c53392.jpg"))
        // A names dictionary from before the cards: the tag, no card.
        val old = sense("名前 · Test", "名前", """["真宮寺 是清（しんぐうじ これきよ）"]""", scoped = true)
        assertFalse(PopupModel.entry(result(listOf(old)), null).has("card"))
        zip.delete()
    }
}
