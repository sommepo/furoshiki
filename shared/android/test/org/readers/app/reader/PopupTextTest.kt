// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.dict.TermEntry
import org.readers.app.reader.ui.PopupModel
import org.robolectric.RobolectricTestRunner

/**
 * The popup's model as text (PopupModel.entryText): glossaries go to the page as stored, checked
 * to be JSON; each picture once; the same entry as the objects' model otherwise.
 */
@RunWith(RobolectricTestRunner::class)
class PopupTextTest {
    private fun term(dictionary: String, glossary: String, id: Long, media: Map<String, String> = emptyMap()) = TermEntry(
        dictionaryId = 1, dictionary = dictionary, priority = 0, expression = "猫", reading = "ねこ",
        definitionTags = "n", rules = "", score = 0, glossaryJson = glossary, sequence = 0, termTags = "",
        media = media, id = id
    )

    private fun result(vararg entries: TermEntry) =
        LookupResult("猫", "ねこ", "猫", emptyList(), entries.toList(), emptyList(), emptyList())

    @Test
    fun onlyJsonArraysGoThrough() {
        assertEquals("[\"cat\"]", PopupModel.checkedArray("  [\"cat\"]\n"))
        assertEquals("[{\"tag\":\"div\",\"content\":[\"a\\u00e9\",1.5e3,true,null]}]",
            PopupModel.checkedArray("[{\"tag\":\"div\",\"content\":[\"a\\u00e9\",1.5e3,true,null]}]"))
        for (broken in listOf("", "{}", "[\"cat\"", "[\"cat\"]x", "[\"a\\q\"]", "[1,]", "[\"a\u0001\"]", "[tru]")) {
            assertEquals(broken, "[]", PopupModel.checkedArray(broken))
        }
    }

    @Test
    fun theTextIsTheObjectsModelWithTheGlossaryAsStored() {
        PopupModel.forgetPrepared()
        val one = result(term("JMdict", "[\"cat\",\"feline\"]", 7), term("Broken", "[\"not closed\"", 8))
        val sent = HashSet<String>()
        val pictures = JSONObject()
        val text = JSONObject(PopupModel.entryText(one, "Looked up once", emptyMap(), sent, pictures))
        assertEquals("猫", text.getString("expression"))
        assertEquals("Looked up once", text.getString("history"))
        val senses = text.getJSONArray("senses")
        // The broken glossary is left out, as the objects' model leaves out an empty one.
        assertEquals(1, senses.length())
        assertEquals("cat", senses.getJSONObject(0).getJSONArray("content").getString(0))
        assertEquals("JMdict", senses.getJSONObject(0).getString("dictionary"))
        assertEquals(0, pictures.length())
    }

    @Test
    fun eachPictureGoesOnce() {
        PopupModel.forgetPrepared()
        val glossary = "[{\"type\":\"structured-content\",\"content\":{\"tag\":\"img\",\"path\":\"a.png\"}}]"
        val one = result(term("Pictures", glossary, 9, mapOf("a.png" to "data:image/png;base64,AAAA")))
        val sent = HashSet<String>()
        val first = JSONObject()
        val text = JSONObject(PopupModel.entryText(one, null, emptyMap(), sent, first))
        val media = text.getJSONArray("senses").getJSONObject(0).getJSONObject("media")
        assertEquals("pic:Pictures\na.png", media.getString("a.png"))
        assertEquals("data:image/png;base64,AAAA", first.getString("Pictures\na.png"))
        val second = JSONObject()
        PopupModel.entryText(one, null, emptyMap(), sent, second)
        assertEquals(0, second.length())
        assertTrue(sent.contains("Pictures\na.png"))
        assertFalse(text.has("card"))
    }
}
