// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.companion

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class AppTitlesTest {
    @Test
    fun anIdOfItsOwn() {
        assertEquals("APP-PARANORMASIG", AppTitles.idOf("PARANORMASIGHT", JSONArray()))
        assertEquals("APP-DOUKUTSU", AppTitles.idOf("Doukutsu", JSONArray()))
        // A title of no Latin letters is named by its hash, the same each time.
        assertEquals(AppTitles.idOf("洞窟物語", JSONArray()), AppTitles.idOf("洞窟物語", JSONArray()))
        // Taken already: the next free one.
        val taken = JSONArray().put(JSONObject().put("id", "APP-DOUKUTSU")).put(JSONObject().put("id", "APP-DOUKUTSU-2"))
        assertEquals("APP-DOUKUTSU-3", AppTitles.idOf("doukutsu", taken))
    }

    @Test
    fun theAppOfAPath() {
        assertEquals("app.gamenative" to "app.gamenative.MainActivityAliasDefault",
            AppTitles.appOf("app:app.gamenative/app.gamenative.MainActivityAliasDefault#APP-X"))
        assertNull(AppTitles.appOf("/storage/emulated/0/Games/PSP/game.iso"))
        assertNull(AppTitles.appOf("app:only-a-package#APP-X"))
    }
}
