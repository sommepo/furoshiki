// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.junit.Assert.assertEquals
import org.junit.Test

class TitleTextTest {
    @Test
    fun aDumpsTagsGoAndAColonComesBack() {
        assertEquals("CHULIP", TitleText.clean("CHULIP (USA) (1)"))
        assertEquals("Sora no Kiseki FC Evolution", TitleText.clean("Sora no Kiseki FC Evolution (Japan) [!]"))
        assertEquals("Game", TitleText.clean("Game (En,Fr,De) (Rev 1) (v1.01)"))
        assertEquals("PARANORMASIGHT: THE SEVEN MYSTERIES OF HONJO", TitleText.clean("PARANORMASIGHT_ THE SEVEN MYSTERIES OF HONJO"))
    }

    @Test
    fun aNameInBracketsAndADiscStay() {
        // A game script's title with its English name, a disc's number, a subtitle: none is a tag.
        assertEquals("作り話 (A Made-up Game)", TitleText.clean("作り話 (A Made-up Game)"))
        assertEquals("Grisaia (Disc 2)", TitleText.clean("Grisaia (Disc 2)"))
        assertEquals("Tokimeki Memorial (PSP the Best)", TitleText.clean("Tokimeki Memorial (PSP the Best)"))
        assertEquals("ever 17", TitleText.clean("ever 17"))
        assertEquals("(USA)", TitleText.clean("(USA)"))
    }
}
