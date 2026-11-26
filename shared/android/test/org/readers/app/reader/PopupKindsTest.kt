// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.view.KeyEvent
import androidx.test.core.app.ApplicationProvider
import org.json.JSONArray
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.dict.DictionaryKinds
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.dict.TermEntry
import org.readers.app.reader.ui.PopupKeys
import org.readers.app.reader.ui.PopupModel
import org.robolectric.RobolectricTestRunner

/**
 * The popup's tabs by kind (words, grammar, kanji, names): what kind each sense is, what a grammar
 * dictionary's sense carries for the page to lay out, and the controller's L1 and R1.
 */
@RunWith(RobolectricTestRunner::class)
class PopupKindsTest {
    private fun term(dictionary: String, glossary: String, expression: String = "くらい", scoped: Boolean = false) = TermEntry(
        dictionaryId = 1, dictionary = dictionary, priority = 0, expression = expression, reading = "くらい",
        definitionTags = "", rules = "", score = 0, glossaryJson = glossary, sequence = 0, termTags = "",
        scoped = scoped
    )

    private fun result(entries: List<TermEntry>) =
        LookupResult(entries.first().expression, "くらい", entries.first().expression, emptyList(), entries, emptyList(), emptyList())

    @Test
    fun aDictionarysKindIsSaidByItsTitle() {
        assertEquals(DictionaryKinds.GRAMMAR, DictionaryKinds.of(term("日本語文法辞典(全集)", "[]")))
        assertEquals(DictionaryKinds.GRAMMAR, DictionaryKinds.of(term("Dictionary of Japanese Grammar", "[]")))
        assertEquals(DictionaryKinds.GRAMMAR, DictionaryKinds.of(term("Bunpro N4", "[]")))
        assertEquals(DictionaryKinds.NAME, DictionaryKinds.of(term("JMnedict", "[]")))
        assertEquals(DictionaryKinds.WORD, DictionaryKinds.of(term("JMdict", "[]")))
        assertEquals(DictionaryKinds.WORD, DictionaryKinds.of(term("大辞林", "[]")))
        // Titles the rule missed (2026-11-26).
        assertEquals(null, DictionaryKinds.ofTitle("Kanjium Pitch Accents"))
        assertEquals(DictionaryKinds.KANJI, DictionaryKinds.ofTitle("角川新字源 改訂新版"))
        assertEquals(DictionaryKinds.KANJI, DictionaryKinds.ofTitle("旺文社漢字典 第四版"))
        assertEquals(DictionaryKinds.GRAMMAR, DictionaryKinds.ofTitle("[画像付き] 絵でわかる日本語"))
        assertEquals(DictionaryKinds.GRAMMAR, DictionaryKinds.ofTitle("毎日のんびり日本語教師"))
        // A character dictionary's characters are kanji; a compound in it is a word.
        assertEquals(DictionaryKinds.KANJI, DictionaryKinds.of(term("漢字辞典", "[]", expression = "位")))
        assertEquals(DictionaryKinds.WORD, DictionaryKinds.of(term("漢字辞典", "[]", expression = "位置")))
        // The running game's names dictionary is names, whatever its title.
        assertEquals(DictionaryKinds.NAME, DictionaryKinds.of(term("名前 · Danganronpa V3", "[]", scoped = true)))
    }

    @Test
    fun theUsersKindWins() {
        val chosen = DictionaryKinds.parse(DictionaryKinds.with(emptyMap(), "大辞林", DictionaryKinds.GRAMMAR))
        assertEquals(DictionaryKinds.GRAMMAR, DictionaryKinds.of(term("大辞林", "[]"), chosen))
        val back = DictionaryKinds.parse(DictionaryKinds.with(chosen, "大辞林", null))
        assertNull(back["大辞林"])
        assertTrue(DictionaryKinds.parse("""{"JMdict":"nonsense"}""").isEmpty())
        assertTrue(DictionaryKinds.parse("not json").isEmpty())
    }

    @Test
    fun eachSenseSaysItsKindAndGrammarKeepsItsLayout() {
        val structured = """[{"type":"structured-content","content":[{"tag":"div","data":{"content":"formation"},"content":"Number + くらい"}]}]"""
        val senses = PopupModel.senses(
            result(listOf(term("JMdict", """["approximately","about"]"""), term("Bunpro", structured)))
        )
        assertEquals(2, senses.length())
        val word = senses.getJSONObject(0)
        assertEquals("word", word.getString("kind"))
        // A word's glossary too, for the full layout (the compact one is the page's).
        assertEquals("approximately", word.getJSONArray("content").getString(0))
        val grammar = senses.getJSONObject(1)
        assertEquals("grammar", grammar.getString("kind"))
        assertEquals("Bunpro", grammar.getString("heading"))
        // The glossary whole, as the dictionary laid it out: the page builds it element by element.
        val content = grammar.getJSONArray("content")
        assertEquals(JSONArray(structured).toString(), content.toString())
    }

    @Test
    fun aNameIsNamesAndKindsGoIntoTheEntry() {
        val entry = PopupModel.entry(
            result(listOf(term("名前 · Game", """["牧瀬 紅莉栖"]""", scoped = true), term("JMdict", """["Chris"]"""))), null
        )
        val senses = entry.getJSONArray("senses")
        assertEquals("name", senses.getJSONObject(0).getString("kind"))
        assertEquals("word", senses.getJSONObject(1).getString("kind"))
        // The user's kinds reach the entry.
        val chosen = mapOf("JMdict" to DictionaryKinds.GRAMMAR)
        val again = PopupModel.entry(result(listOf(term("JMdict", """["Chris"]"""))), null, chosen)
        assertEquals("grammar", again.getJSONArray("senses").getJSONObject(0).getString("kind"))
    }

    @Test
    fun thePopupThemeFollowsTheHomeThemeUnlessChosen() {
        val settings = ReaderSettings(ApplicationProvider.getApplicationContext())
        settings.preferences.edit().clear().commit()
        assertEquals("console", settings.popupTheme) // no home page: the console's colours
        for ((shelf, popup) in listOf("kon" to "kon", "solarized" to "solarized-dark", "paper" to "paper", "mono" to "kumihan")) {
            settings.preferences.edit().putString(ReaderSettings.KEY_SHELF_THEME, shelf).commit()
            assertEquals(popup, settings.popupTheme)
        }
        settings.preferences.edit().putString(ReaderSettings.KEY_POPUP_THEME, "auto").commit()
        assertEquals("kumihan", settings.popupTheme)
        settings.preferences.edit().putString(ReaderSettings.KEY_POPUP_THEME, "glass").commit()
        assertEquals("glass", settings.popupTheme)
        settings.preferences.edit().putString(ReaderSettings.KEY_POPUP_THEME, "console").commit()
        assertEquals("console", settings.popupTheme)
    }

    @Test
    fun withNoPopupOpenTheShouldersAreNotThePopups() {
        assertFalse(PopupKeys.isOpen)
        assertFalse(PopupKeys.onKey(KeyEvent.KEYCODE_BUTTON_L1, true))
        assertFalse(PopupKeys.onKey(KeyEvent.KEYCODE_BUTTON_R1, true))
    }

    @Test
    fun theInputIsAControllerOrAFinger() {
        PopupKeys.inputFrom(pad = true)
        assertTrue(PopupKeys.padInUse)
        PopupKeys.inputFrom(pad = false)
        assertFalse(PopupKeys.padInUse)
    }
}
