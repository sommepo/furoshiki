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
 *     "markUnknown": true, "lookups": "where", "css": "...",
 *     "look": { "opacity": 85, "radius": -1, "compact": 2, "hideTags": false, "portrait": "small" } }
 */
object PopupStyle {
    private val THEMES = listOf("console") + ReaderSettings.POPUP_THEMES + "classic"
    private val LOOKUPS = listOf("count", "where", "off")

    fun export(settings: ReaderSettings): JSONObject = JSONObject()
        .put("format", 1)
        .put("kind", "reader-popup-style")
        .put("theme", settings.popupTheme)
        .put("markUnknown", settings.popupMarkUnknown)
        .put("lookups", settings.popupLookups)
        .put("css", settings.popupCss)
        // Since the looks' own settings: a reader from before them passes these by.
        .put("look", settings.popupLook)

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
            style.optJSONObject("look")?.let { look ->
                if (look.has("opacity")) putInt(ReaderSettings.KEY_POPUP_OPACITY, look.optInt("opacity").coerceIn(0, 100))
                if (look.has("radius")) putInt(ReaderSettings.KEY_POPUP_RADIUS, look.optInt("radius", -1).coerceIn(-1, 28))
                if (look.has("compact")) putInt(ReaderSettings.KEY_POPUP_COMPACT, look.optInt("compact").coerceIn(0, 9))
                if (look.has("hideTags")) putBoolean(ReaderSettings.KEY_POPUP_HIDE_TAGS, look.optBoolean("hideTags"))
                look.optString("portrait").takeIf { it in listOf("small", "large", "off") }?.let { putString(ReaderSettings.KEY_POPUP_PORTRAIT, it) }
            }
        }.apply()
        return null
    }
}
