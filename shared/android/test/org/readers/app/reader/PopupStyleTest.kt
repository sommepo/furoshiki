// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.Context
import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.ui.PopupModel
import org.readers.app.reader.ui.PopupStyle
import org.robolectric.RobolectricTestRunner

/** A popup style as one file, the same as Furoshiki's (home/src/popup_style.h); the page's data. */
@RunWith(RobolectricTestRunner::class)
class PopupStyleTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)

    @Test
    fun aStyleGoesOutAndComesBackIn() {
        // A file Furoshiki wrote.
        val furoshiki = JSONObject(
            """{"format":1,"kind":"reader-popup-style","theme":"yomitan-dark","markUnknown":false,
               "lookups":"where","css":".popup { border-radius: 9px; }"}"""
        )
        assertNull(PopupStyle.import(furoshiki, preferences))
        val settings = ReaderSettings(context)
        assertEquals("yomitan-dark", settings.popupTheme)
        assertFalse(settings.popupMarkUnknown)
        assertEquals("where", settings.popupLookups)
        assertEquals(".popup { border-radius: 9px; }", settings.popupCss)
        val out = PopupStyle.export(settings)
        assertEquals("reader-popup-style", out.getString("kind"))
        assertEquals(furoshiki.getString("css"), out.getString("css"))
        assertEquals(false, out.getBoolean("markUnknown"))
        // Something else is refused, and a theme this app does not have is left as it was.
        assertNotNull(PopupStyle.import(JSONObject("""{"theme":"paper"}"""), preferences))
        assertNull(PopupStyle.import(JSONObject("""{"format":1,"kind":"reader-popup-style","theme":"neon"}"""), preferences))
        assertEquals("yomitan-dark", settings.popupTheme)
    }
}
