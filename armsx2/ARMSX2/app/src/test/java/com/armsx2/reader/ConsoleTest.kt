// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

package com.armsx2.reader

import com.armsx2.reader.anki.SourceAddress
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** What is this console's own ([Console]) and its bundled scripts; the other tests are shared by every reader fork. */
@RunWith(RobolectricTestRunner::class)
class ConsoleTest {
    private val context = RuntimeEnvironment.getApplication()

    @Test
    fun scriptsAreNamedAfterThePs2sDiscSerials() {
        assertEquals("SLPM-66302", ReaderCatalog.titleIdOf("SLPM-66302.lua"))
        assertNull(ReaderCatalog.titleIdOf("SLPM66302.lua"))
        assertNull(ReaderCatalog.titleIdOf("slpm-66302.lua"))
        assertNull(ReaderCatalog.titleIdOf("ULJS00219.lua"))
    }

    @Test
    fun aCardsUrlIsTheGamesPageAtPlayStationWithoutTheHyphen() {
        assertEquals("https://www.jp.playstation.com/software/title/slpm66302.html", SourceAddress.of("SLPM-66302"))
        assertEquals("https://www.jp.playstation.com/software/title/slpm66302.html", SourceAddress.of(" slpm-66302 "))
    }

    @Test
    fun theGamesOfThePublishedListAreBundled() {
        ReaderCatalog.forget()
        val ids = ReaderCatalog.games(context).map { it.titleId }.toSet()
        // ひぐらしのなく頃に祭 カケラ遊び, Remember11, Ever17, Never7, ドラゴンクエストV, サイレントヒル2,
        // アマガミ: a checkout of one's own has more, those whose scripts need a table that is not published.
        for (id in listOf("SLPM-66913", "SLPM-65550", "SLPM-65421", "SLPS-25256", "SLPM-65555", "SLPM-65051", "SLPS-25918")) {
            assertTrue("$id is not bundled: $ids", id in ids)
        }
    }

    @Test
    fun anAdvanceButtonKeptByThePspsNumbersIsTheButtonItMeant() {
        assertEquals(Console.BUTTON_YES, Console.advanceButton(null))
        assertEquals(Console.BUTTON_YES, Console.advanceButton("13"))
        assertEquals(Console.BUTTON_NO, Console.advanceButton("14"))
        assertEquals(12, Console.advanceButton("9"))
        assertEquals(12, Console.advanceButton("12"))
    }
}
