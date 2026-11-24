// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.json.JSONArray
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.dict.KanjiEntry
import org.readers.app.reader.dict.TermMeta
import org.readers.app.reader.ui.PopupModel
import org.robolectric.RobolectricTestRunner

/** The Kanji tab's entries, as the desktop's PopupModel::KanjiText makes them. */
@RunWith(RobolectricTestRunner::class)
class KanjiPopupTest {
    @Test
    fun theFirstCharacterIsAskedOnlyWhenItIsAKanji() {
        assertEquals("食", PopupModel.kanjiOf("食べる"))
        assertEquals("々", PopupModel.kanjiOf("々"))
        assertEquals("𠮟", PopupModel.kanjiOf("𠮟る"))
        assertNull(PopupModel.kanjiOf("たべる"))
        assertNull(PopupModel.kanjiOf(""))
    }

    @Test
    fun eachDictionaryIsAnEntryAndTheFirstHasTheFrequencies() {
        val kanji = listOf(
            KanjiEntry(1, "KANJIDIC", "食", listOf("ショク", "ジキ"), listOf("く.う", "た.べる"), "jouyou",
                listOf("eat", "food"), """{"strokes":"9","grade":"2"}"""),
            KanjiEntry(2, "Other", "食", emptyList(), emptyList(), "", listOf("meal"), "{}")
        )
        val frequencies = listOf(
            TermMeta("Kanji freq", "", "食", "freq", "321"),
            TermMeta("Shown", "", "食", "freq", """{"value":5,"displayValue":"5㋕"}"""),
            TermMeta("Pitch", "", "食", "pitch", "{}")
        )
        val entries = JSONArray("[" + PopupModel.kanjiText(kanji, frequencies) + "]")
        assertEquals(2, entries.length())
        val first = entries.getJSONObject(0)
        assertEquals("食", first.getString("expression"))
        val character = first.getJSONObject("kanji")
        assertEquals("KANJIDIC", character.getString("dictionary"))
        assertEquals("た.べる", character.getJSONArray("kunyomi").getString(1))
        assertEquals("9", character.getJSONObject("stats").getString("strokes"))
        assertEquals(2, first.getJSONArray("frequencies").length())
        assertEquals("5㋕", first.getJSONArray("frequencies").getJSONObject(1).getString("value"))
        assertEquals(0, entries.getJSONObject(1).getJSONArray("frequencies").length())
        assertEquals("", PopupModel.kanjiText(emptyList(), frequencies))
    }
}
