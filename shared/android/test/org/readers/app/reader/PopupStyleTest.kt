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
import org.junit.Assert.assertTrue
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

    /** The look's own settings (how solid, the corners, how many senses...) travel in the file too. */
    @Test
    fun theLooksSettingsTravelWithTheStyle() {
        val settings = ReaderSettings(context)
        // As they are before anything was chosen: each look as it is.
        assertEquals(0, settings.popupOpacity)
        assertEquals(-1, settings.popupRadius)
        assertEquals("small", settings.popupPortrait)
        val style = JSONObject(
            """{"format":1,"kind":"reader-popup-style","theme":"glass","css":"",
               "look":{"opacity":70,"radius":12,"compact":2,"hideTags":true,"portrait":"large"}}"""
        )
        assertNull(PopupStyle.import(style, preferences))
        assertEquals("glass", settings.popupTheme)
        assertEquals(70, settings.popupOpacity)
        assertEquals(12, settings.popupRadius)
        assertEquals(2, settings.popupCompact)
        assertTrue(settings.popupHideTags)
        assertEquals("large", settings.popupPortrait)
        val look = PopupStyle.export(settings).getJSONObject("look")
        assertEquals(70, look.getInt("opacity"))
        assertEquals("large", look.getString("portrait"))
        // What is out of range is brought in; a portrait this app does not know is left.
        assertNull(PopupStyle.import(JSONObject(
            """{"format":1,"kind":"reader-popup-style","look":{"opacity":7,"radius":90,"portrait":"huge"}}"""), preferences))
        assertEquals(20, settings.popupOpacity)
        assertEquals(28, settings.popupRadius)
        assertEquals("large", settings.popupPortrait)
        // A file from before these settings changes none of them.
        assertNull(PopupStyle.import(JSONObject("""{"format":1,"kind":"reader-popup-style","theme":"paper"}"""), preferences))
        assertEquals(2, settings.popupCompact)
    }
}
