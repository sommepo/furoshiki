// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader

import com.github.stenzek.duckstation.reader.anki.SourceAddress
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** What is this console's own ([Console]); the other tests are shared by every reader fork. */
class ConsoleTest {
    @Test
    fun scriptsAreNamedAfterTheDiscsSerials() {
        assertEquals("SLPS-01794", ReaderCatalog.titleIdOf("SLPS-01794.lua"))
        assertEquals("SLPM-86023", ReaderCatalog.titleIdOf("SLPM-86023.lua"))
        assertNull(ReaderCatalog.titleIdOf("SLPS01794.lua"))
        assertNull(ReaderCatalog.titleIdOf("ULJM05887.lua"))
        assertNull(ReaderCatalog.titleIdOf("0100d7f00fb1a000.lua"))
    }

    @Test
    fun aCardsUrlIsTheGamesPageAtPlayStation() {
        assertEquals("https://www.jp.playstation.com/software/title/slps69002.html", SourceAddress.of("SLPS-69002"))
        assertEquals("https://www.jp.playstation.com/software/title/slps01794.html", SourceAddress.of(" slps-01794 "))
    }

    @Test
    fun theButtonsAreTheEmulatorsNumbersForThem() {
        // DigitalController::Button: Start 3, up 4, right 5, down 6, left 7, △ 12, ○ 13, × 14, □ 15.
        assertEquals(13, Console.BUTTON_YES)
        assertEquals(14, Console.BUTTON_NO)
        assertEquals(listOf(4, 6, 7, 5),
            listOf(Console.BUTTON_UP, Console.BUTTON_DOWN, Console.BUTTON_LEFT, Console.BUTTON_RIGHT))
        assertEquals(12, Console.TOUCH_BUTTONS.getValue("triangle").code)
        assertEquals(3, Console.TOUCH_BUTTONS.getValue("start").code)
        assertEquals(Console.TOUCH_ALL.toSet(), Console.TOUCH_BUTTONS.keys)
    }
}
