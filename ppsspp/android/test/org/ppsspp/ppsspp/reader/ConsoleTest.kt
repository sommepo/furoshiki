// SPDX-FileCopyrightText: PPSSPP Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.ppsspp.ppsspp.reader

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.ppsspp.ppsspp.reader.anki.SourceAddress
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

/** What is this console's own ([Console]) and its bundled scripts; the other tests are shared by every reader fork. */
@RunWith(RobolectricTestRunner::class)
class ConsoleTest {
    private val context = RuntimeEnvironment.getApplication()

    @Test
    fun scriptsAreNamedAfterThePspsGameIds() {
        assertEquals("ULJS00219", ReaderCatalog.titleIdOf("ULJS00219.lua"))
        assertNull(ReaderCatalog.titleIdOf("HDR-0164.lua"))
        assertNull(ReaderCatalog.titleIdOf("0100d7f00fb1a000.lua"))
    }

    @Test
    fun aCardsUrlIsTheGamesPageAtPlayStation() {
        assertEquals("https://www.jp.playstation.com/software/title/uljm05000.html", SourceAddress.of("ULJM05000"))
        assertEquals("https://www.jp.playstation.com/software/title/uljs00219.html", SourceAddress.of(" uljs00219 "))
    }


    @Test
    fun theAdvanceButtonIsABitOfThePspsButtonWord() {
        assertEquals(Console.BUTTON_YES, Console.advanceButton(null))
        assertEquals(14, Console.advanceButton("14"))
        assertEquals(Console.BUTTON_YES, Console.advanceButton("nonsense"))
    }
}
