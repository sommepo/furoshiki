// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.content.SharedPreferences
import org.json.JSONObject
import org.readers.app.reader.ReaderSettings

/**
 * A popup style as one file, the same as Furoshiki's (home/src/popup_style.h): the popup's look,
 * whether words not in Anki are marked, how often a word was looked up, and the user's own CSS.
 *   { "format": 1, "kind": "reader-popup-style", "theme": "yomitan-dark",
 *     "markUnknown": true, "lookups": "where", "css": "..." }
 */
object PopupStyle {
    private val THEMES = listOf("console", "furoshiki", "yomitan-dark", "paper", "paper-dark", "classic")
    private val LOOKUPS = listOf("count", "where", "off")

    fun export(settings: ReaderSettings): JSONObject = JSONObject()
        .put("format", 1)
        .put("kind", "reader-popup-style")
        .put("theme", settings.popupTheme)
        .put("markUnknown", settings.popupMarkUnknown)
        .put("lookups", settings.popupLookups)
        .put("css", settings.popupCss)

    /** Takes [style] into the settings; what went wrong, or null. */
    fun import(style: JSONObject, preferences: SharedPreferences): String? {
        if (style.optString("kind") != "reader-popup-style" || style.optInt("format") != 1) {
            return "not a popup style file"
        }
        preferences.edit().apply {
            style.optString("theme").takeIf { it in THEMES }?.let { putString(ReaderSettings.KEY_POPUP_THEME, it) }
            if (style.has("markUnknown")) putBoolean(ReaderSettings.KEY_POPUP_MARK_UNKNOWN, style.optBoolean("markUnknown", true))
            style.optString("lookups").takeIf { it in LOOKUPS }?.let { putString(ReaderSettings.KEY_POPUP_LOOKUPS, it) }
            putString(ReaderSettings.KEY_POPUP_CSS, style.optString("css"))
        }.apply()
        return null
    }
}
