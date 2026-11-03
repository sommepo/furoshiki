// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package com.flycast.emulator.reader

import com.flycast.emulator.reader.anki.SourceAddress
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** What is this console's own ([Console]); the other tests are shared by every reader fork. */
class ConsoleTest {
    @Test
    fun scriptsAreNamedAfterTheDreamcastsProductNumbers() {
        assertEquals("HDR-0164", ReaderCatalog.titleIdOf("HDR-0164.lua"))
        assertEquals("T-1214M", ReaderCatalog.titleIdOf("T-1214M.lua"))
        assertEquals("MK-51058", ReaderCatalog.titleIdOf("MK-51058.lua"))
        assertNull(ReaderCatalog.titleIdOf("0100d7f00fb1a000.lua"))
    }

    @Test
    fun aCardsUrlIsSegasListOfTheConsolesSoftware() {
        assertEquals("https://sega.jp/history/hard/dreamcast/software.html#T-99002", SourceAddress.of("T-99002"))
        assertEquals("https://sega.jp/history/hard/dreamcast/software.html#HDR-0164", SourceAddress.of(" hdr-0164 "))
    }
}
