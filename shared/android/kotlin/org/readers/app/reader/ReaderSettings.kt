// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.Context
import android.content.SharedPreferences
import androidx.preference.PreferenceManager

/** Typed access to the reader's preferences. Keys match the pages res/xml/reader_prefs_*.xml. */
class ReaderSettings(context: Context) {
    val preferences: SharedPreferences =
        PreferenceManager.getDefaultSharedPreferences(context.applicationContext)

    init {
        // An advance button kept by the old numbers is turned into the new one before the
        // settings page shows it (the getter does it).
        advanceButton
    }

    // ---- Text on screen ----
    /** Words in the game's own text can be tapped, where the game's profile says where it is. */
    val screenText get() = preferences.getBoolean(KEY_SCREEN_TEXT, true)

    /** Draw the outlines of what can be tapped, to check that they sit on the text. */
    val screenTextOutline get() = preferences.getBoolean(KEY_SCREEN_TEXT_OUTLINE, false)

    /** How the word being looked up is marked: "highlight" (a coloured box over it), "outline"
     *  (a frame around it) or "none". */
    val wordMark: String get() = when (val value = preferences.getString(KEY_WORD_MARK, WORD_MARK_OUTLINE)) {
        WORD_MARK_HIGHLIGHT, WORD_MARK_NONE -> value
        else -> WORD_MARK_OUTLINE
    }

    /** The corner of each headword's part of the popup where its card buttons stand:
     *  "bottom_left" (the first value), "bottom_right", "top_left" or "top_right". */
    val cardCorner: String get() = when (val value = preferences.getString(KEY_CARD_CORNER, CARD_CORNER_DEFAULT)) {
        "bottom_right", "top_left", "top_right" -> value
        else -> CARD_CORNER_DEFAULT
    }

    /** Text size of the dictionary popup, in percent. */
    val popupScale get() = preferences.getInt(KEY_POPUP_SCALE, 100).coerceIn(60, 200)

    /** How tall the dictionary popup may get, in percent of the screen's height; then it scrolls. */
    /** The popup goes away by itself after this long without being touched; 0: it stays. */
    val popupAutoHideSeconds get() = preferences.getInt(KEY_POPUP_AUTOHIDE, 15).coerceIn(0, 120)

    /** With the cursor: the popup goes away when the cursor leaves its word and the popup. */
    val popupHideOnCursorExit get() = preferences.getBoolean(KEY_POPUP_HIDE_ON_EXIT, false)

    /**
     * The popup's look: "console" (the first value: the popup page in the console's colours),
     * one of [POPUP_THEMES] ("furoshiki", "yomitan-dark", "paper"...), or "classic" (drawn as before the page).
     */
    val popupTheme: String get() = preferences.getString(KEY_POPUP_THEME, "console")
        .let { value -> if (value == "classic" || value in POPUP_THEMES) value!! else "console" }

    /** How solid the popup's ground is, 20 to 100 %; 0: as its look has it. */
    val popupOpacity: Int get() = preferences.getInt(KEY_POPUP_OPACITY, 0).coerceIn(0, 100).let { if (it == 0) 0 else maxOf(20, it) }

    /** The popup's corners, 0 to 28 px; -1: as its look has them. */
    val popupRadius: Int get() = preferences.getInt(KEY_POPUP_RADIUS, -1).coerceIn(-1, 28)

    /** How many senses of a word are shown before "more" (1 to 9); 0: all of them. */
    val popupCompact: Int get() = preferences.getInt(KEY_POPUP_COMPACT, 0).coerceIn(0, 9)

    /** Whether the senses' part-of-speech tags (n, v1, vt...) are left out. */
    val popupHideTags: Boolean get() = preferences.getBoolean(KEY_POPUP_HIDE_TAGS, false)

    /** A name's picture on its card: "small", "large" or "off". */
    val popupPortrait: String get() = preferences.getString(KEY_POPUP_PORTRAIT, "small").let { if (it == "large" || it == "off") it!! else "small" }

    /** Those five as the page takes them (Reader.show's "look"; shared/popup/README.md). */
    val popupLook: org.json.JSONObject get() = org.json.JSONObject()
        .put("opacity", popupOpacity).put("radius", popupRadius).put("compact", popupCompact)
        .put("hideTags", popupHideTags).put("portrait", popupPortrait)

    /** The user's own CSS for the popup page, applied after its theme (shared/popup/README.md). */
    val popupCss: String get() = preferences.getString(KEY_POPUP_CSS, "")!!

    /** A word that is not in Anki is marked in the popup (a soft tint behind it). */
    val popupMarkUnknown get() = preferences.getBoolean(KEY_POPUP_MARK_UNKNOWN, true)

    /**
     * What the popup says of how often a word was looked up: "off", "count" (the first value)
     * or "where" (with when and in which game first).
     */
    val popupLookups: String get() = when (val value = preferences.getString(KEY_POPUP_LOOKUPS, "count")) {
        "off", "where" -> value
        else -> "count"
    }

    val popupHeightPercent get() = preferences.getInt(KEY_POPUP_HEIGHT, 27).coerceIn(10, 80)

    // ---- Cursor ----
    /**
     * The button that, held, turns the second display, or else the game's screen, into a
     * trackpad for a cursor on the game's picture (by the numbers the app hears its buttons by:
     * [Console.CURSOR_BUTTON_DEFAULT]). -1 for none.
     */
    val cursorButton get() = preferences.getString(KEY_CURSOR_BUTTON, null)?.toIntOrNull() ?: Console.CURSOR_BUTTON_DEFAULT

    /**
     * On a device with two displays: the one the game is not on is the cursor's trackpad (and
     * holds the reader's panel) while a game runs.
     */
    val secondScreen get() = preferences.getBoolean(KEY_SECOND_SCREEN, true)

    /** The cursor's button switches it on and off with a press each, instead of being held. */
    val cursorToggle get() = preferences.getBoolean(KEY_CURSOR_TOGGLE, false)

    /** How fast the cursor moves, in percent. */
    val cursorSpeed get() = preferences.getInt(KEY_CURSOR_SPEED, 100).coerceIn(25, 300)

    /**
     * On a character of the game's text the cursor is a frame around that character. Off: it
     * stays a ring that moves freely over the text.
     */
    val cursorFrame get() = preferences.getBoolean(KEY_CURSOR_FRAME, true)

    /**
     * With a new line the cursor is put just before its first character. Off: it stays where
     * it was left.
     */
    val cursorHome get() = preferences.getBoolean(KEY_CURSOR_HOME, true)

    // ---- Panel ----
    val panelEnabled get() = preferences.getBoolean(KEY_PANEL_ENABLED, true)

    /** Keep the panel away while the line it would show can be tapped in the game's picture. */
    val panelAutoHide get() = preferences.getBoolean(KEY_PANEL_AUTO_HIDE, true)
    val panelDisplay get() = preferences.getString(KEY_PANEL_DISPLAY, DISPLAY_AUTO)!!
    val panelPosition get() = preferences.getString(KEY_PANEL_POSITION, POSITION_BOTTOM)!!
    val panelHeightPercent get() = preferences.getInt(KEY_PANEL_HEIGHT, 38).coerceIn(15, 100)
    val panelOpacityPercent get() = preferences.getInt(KEY_PANEL_OPACITY, 85).coerceIn(0, 100)
    val fontSizeSp get() = preferences.getInt(KEY_FONT_SIZE, 22).coerceIn(10, 64)
    val showFurigana get() = preferences.getBoolean(KEY_FURIGANA, true)
    val showChoices get() = preferences.getBoolean(KEY_SHOW_CHOICES, true)
    val rereadColour get() = preferences.getBoolean(KEY_REREAD_COLOUR, true)

    /**
     * The game's button pressed by the panel's advance control and by touch play, as the
     * console numbers it ([Console.advanceButton]). A value kept by an older numbering is turned
     * into the button it meant, and kept so.
     */
    val advanceButton: Int
        get() {
            val kept = preferences.getString(KEY_ADVANCE_BUTTON, null)
            val now = Console.advanceButton(kept)
            if (kept != null && kept != now.toString()) {
                preferences.edit().putString(KEY_ADVANCE_BUTTON, now.toString()).apply()
            }
            return now
        }

    /**
     * Playing by touch alone, for the games whose profile says how ("-- reader: touch press"): a
     * tap on the game's picture where there is no text goes on (the advance button), a tap on an
     * answer of a choice picks it, and a held finger on an answer looks its word up. A game
     * made for a touch screen ("-- reader: touch game") gets those taps itself.
     */
    val touchPlay get() = preferences.getBoolean(KEY_TOUCH_PLAY, touchPlayAtFirst())

    /**
     * The controller's buttons drawn small over every game (ui/TouchButtons: the d-pad at the
     * left, the face buttons at the right, tucked away by their tabs), with the menu's button
     * in the corner: on at first where the device has no game controls of its own, and the
     * user's to switch on any device (a handheld's owner who wants them all the same).
     */
    val touchButtons get() = preferences.getBoolean(KEY_TOUCH_BUTTONS, touchPlayAtFirst())

    /**
     * Keeps Play by touch's first value, so that its switch shows it: on for a phone or tablet,
     * off for a handheld with a controller of its own (an AYN Thor) — the maintainer plays by
     * touch on the Fold only.
     */
    fun keepTouchPlayDefault() {
        if (!preferences.contains(KEY_TOUCH_PLAY)) {
            preferences.edit().putBoolean(KEY_TOUCH_PLAY, touchPlayAtFirst()).apply()
        }
        if (!preferences.contains(KEY_TOUCH_BUTTONS)) {
            preferences.edit().putBoolean(KEY_TOUCH_BUTTONS, touchPlayAtFirst()).apply()
        }
    }

    // ---- Statistics ----
    var statsPaused: Boolean
        get() = preferences.getBoolean(KEY_STATS_PAUSED, false)
        set(value) = preferences.edit().putBoolean(KEY_STATS_PAUSED, value).apply()
    val idleTimeoutMs get() = preferences.getInt(KEY_IDLE_MINUTES, 5).coerceIn(1, 60) * 60_000L

    /** Show the running session's characters, speed and time in a corner of the game. */
    val sessionHud get() = preferences.getBoolean(KEY_SESSION_HUD, true)

    /** How the session line is drawn: "paper" (the default), "paper-dark", "card" or "ink". */
    val statsStyle: String
        get() = when (val value = preferences.getString(KEY_STATS_STYLE, "paper")) {
            "paper", "paper-dark", "card", "ink" -> value
            else -> "paper"
        }

    // ---- Text output ----
    val textHookEnabled get() = preferences.getBoolean(KEY_HOOK_ENABLED, false)
    val textHookPort get() = preferences.getString(KEY_HOOK_PORT, "6677")!!.toIntOrNull() ?: 6677
    val textHookLan get() = preferences.getBoolean(KEY_HOOK_LAN, false)
    val textHookFormat get() = preferences.getString(KEY_HOOK_FORMAT, FORMAT_PLAIN)!!
    val textHookSpeaker get() = preferences.getBoolean(KEY_HOOK_SPEAKER, false)
    val clipboardAuto get() = preferences.getBoolean(KEY_CLIPBOARD_AUTO, false)


    // ---- Mining ----
    /** The controller button that pauses and resumes the statistics; -1 for none. */
    val statsButton get() = preferences.getString(KEY_STATS_BUTTON, "-1")!!.toIntOrNull() ?: -1

    /** The controller button that goes on to the next choice (and stops it); -1 for none. */
    val nextChoiceButton get() = preferences.getString(KEY_NEXT_CHOICE_BUTTON, "-1")!!.toIntOrNull() ?: -1

    val captureAudio get() = preferences.getBoolean(KEY_CAPTURE_AUDIO, true)
    val audioHistorySeconds get() = 90
    val audioClipMaxSeconds get() = preferences.getInt(KEY_AUDIO_MAX, 20).coerceIn(3, 60)
    val audioLeadMs get() = preferences.getInt(KEY_AUDIO_LEAD, 0).coerceIn(0, 2000)

    /** Whether a card's clip is cut to what is said in it ([org.readers.app.reader.mining.PcmTools.trimToVoice]). */
    val audioVoiceTrim get() = preferences.getBoolean(KEY_AUDIO_VOICE_TRIM, true)

    /** How many of the lines before a card's line its {context} holds. */
    val cardContextLines get() = preferences.getInt(KEY_CARD_CONTEXT_LINES, 2).coerceIn(0, 5)
    val screenshotCacheLines get() = preferences.getInt(KEY_SCREENSHOT_CACHE, 30).coerceIn(0, 200)
    val screenshotMaxWidth get() = preferences.getInt(KEY_SCREENSHOT_WIDTH, 1280).coerceIn(320, 3840)

    /**
     * What a card does with the audio of its line. That audio is everything the game played
     * while the line was up, so a game without voices still has some: music and effects.
     */
    enum class CardAudio(val value: String) {
        ALWAYS("always"),

        /** The user is asked for each card, and can listen first. */
        ASK("ask"),
        NEVER("never");

        companion object {
            fun of(value: String?): CardAudio? = values().firstOrNull { it.value == value }
        }
    }

    /** The general choice, for every game that has none of its own. */
    val cardAudioGeneral: CardAudio
        get() = CardAudio.of(preferences.getString(KEY_CARD_AUDIO, null)) ?: CardAudio.ASK

    /** The choice made for the game [gameKey] alone, or null while it follows the general one. */
    fun cardAudioOf(gameKey: String): CardAudio? =
        if (gameKey.isEmpty()) null else CardAudio.of(preferences.getString(KEY_CARD_AUDIO_GAME + gameKey, null))

    /** Sets the choice for the game [gameKey]; null makes it follow the general one again. */
    fun setCardAudioOf(gameKey: String, choice: CardAudio?) {
        if (gameKey.isEmpty()) return
        val editor = preferences.edit()
        if (choice == null) {
            editor.remove(KEY_CARD_AUDIO_GAME + gameKey)
        } else {
            editor.putString(KEY_CARD_AUDIO_GAME + gameKey, choice.value)
        }
        editor.apply()
    }

    /**
     * What a card of the game [gameKey] does with its line's audio: the user's choice for the
     * game, else what the game's profile says of it ([profileDefault]: a game without voices
     * says *never*), else the general choice.
     */
    fun cardAudioFor(gameKey: String, profileDefault: CardAudio? = null): CardAudio =
        cardAudioOf(gameKey) ?: profileDefault ?: cardAudioGeneral

    // ---- Anki ----
    val ankiTransport get() = preferences.getString(KEY_ANKI_TRANSPORT, ANKI_ANKIDROID)!!
    val ankiDeck get() = preferences.getString(KEY_ANKI_DECK, "")!!
    val ankiModel get() = preferences.getString(KEY_ANKI_MODEL, "")!!
    /**
     * The fields' templates. Once, a mapping made before {word-audio} existed gets it in the
     * fields that are for the word's sound and were left empty.
     */
    val ankiFieldMap: String
        get() {
            val stored = preferences.getString(KEY_ANKI_FIELDS, "")!!
            if (stored.isEmpty() || preferences.getBoolean(KEY_ANKI_WORD_AUDIO_OFFERED, false)) return stored
            val offered = try {
                org.readers.app.reader.anki.CardBuilder.run { mappingToJson(offerWordAudio(parseMapping(stored))) }
            } catch (_: Exception) {
                stored
            }
            preferences.edit().putString(KEY_ANKI_FIELDS, offered)
                .putBoolean(KEY_ANKI_WORD_AUDIO_OFFERED, true).apply()
            return offered
        }
    val ankiTags get() = preferences.getString(KEY_ANKI_TAGS, Console.APP_KEY)!!
    val ankiConnectUrl get() = preferences.getString(KEY_ANKICONNECT_URL, "")!!
    val ankiConnectKey get() = preferences.getString(KEY_ANKICONNECT_KEY, "")!!
    val ankiAllowDuplicates get() = preferences.getBoolean(KEY_ANKI_DUPLICATES, false)

    /** A word counts as known when any deck has it, not only the one cards go to. */
    val ankiDuplicatesAnywhere get() = preferences.getBoolean(KEY_ANKI_DUPLICATES_ANYWHERE, false)

    /**
     * The first field of note type [model], which is what Anki tells notes apart by, or null
     * when it was not recorded for this note type.
     */
    fun ankiKeyField(model: String): String? {
        val stored = preferences.getString(KEY_ANKI_KEY_FIELD, "")!!
        val at = stored.indexOf(SEPARATOR)
        return if (at > 0 && stored.substring(0, at) == model) stored.substring(at + 1) else null
    }

    /**
     * The field VN Library's media add-on wanted a web address in when a card went through
     * it at [address], or null. Remembered so that the next card has it at once
     * ([org.readers.app.reader.anki.SourceAddress]).
     */
    fun ankiAddressField(address: String): String? {
        val stored = preferences.getString(KEY_ANKI_ADDRESS_FIELD, "")!!
        val at = stored.indexOf(SEPARATOR)
        return if (at > 0 && stored.substring(0, at) == address.trim()) stored.substring(at + 1) else null
    }

    fun rememberAnkiAddressField(address: String, field: String) {
        preferences.edit().putString(KEY_ANKI_ADDRESS_FIELD, address.trim() + SEPARATOR + field).apply()
    }

    companion object {
        /**
         * Play by touch's first value: on unless the device has game controls of its own. A
         * controller that is plugged in or paired (external) does not count; on Android before 10,
         * which cannot tell, any controller does. AYN's handhelds by their maker, in case theirs
         * reports itself as external.
         */
        fun touchPlayAtFirst(): Boolean {
            if (android.os.Build.MANUFACTURER.equals("AYN", ignoreCase = true)) return false
            val controls = android.view.InputDevice.SOURCE_GAMEPAD or android.view.InputDevice.SOURCE_JOYSTICK
            val builtIn = android.view.InputDevice.getDeviceIds().any { id ->
                val device = android.view.InputDevice.getDevice(id) ?: return@any false
                val isControls = device.sources and controls != 0 && !device.isVirtual
                isControls && (android.os.Build.VERSION.SDK_INT < 29 || !device.isExternal)
            }
            return !builtIn
        }

        const val KEY_TOUCH_BUTTONS = "reader_touch_buttons"
        const val KEY_SCREEN_TEXT = "reader_screen_text"
        const val KEY_SCREEN_TEXT_OUTLINE = "reader_screen_text_outline"
        const val KEY_CARD_CORNER = "reader_card_corner"
        const val CARD_CORNER_DEFAULT = "bottom_left"
        const val KEY_WORD_MARK = "reader_word_mark"
        const val WORD_MARK_HIGHLIGHT = "highlight"
        const val WORD_MARK_OUTLINE = "outline"
        const val WORD_MARK_NONE = "none"
        const val KEY_POPUP_SCALE = "reader_popup_scale"
        const val KEY_POPUP_HEIGHT = "reader_popup_height"
        const val KEY_POPUP_AUTOHIDE = "reader_popup_autohide"
        const val KEY_POPUP_HIDE_ON_EXIT = "reader_popup_hide_on_exit"
        const val KEY_POPUP_MARK_UNKNOWN = "reader_popup_mark_unknown"
        const val KEY_POPUP_THEME = "reader_popup_theme"
        const val KEY_POPUP_CSS = "reader_popup_css"
        const val KEY_POPUP_OPACITY = "reader_popup_opacity"
        const val KEY_POPUP_RADIUS = "reader_popup_radius"
        const val KEY_POPUP_COMPACT = "reader_popup_compact"
        const val KEY_POPUP_HIDE_TAGS = "reader_popup_hide_tags"
        const val KEY_POPUP_PORTRAIT = "reader_popup_portrait"
        /** The looks the page has besides the console's own colours (shared/popup/themes). */
        val POPUP_THEMES = listOf("furoshiki", "yomitan-dark", "paper", "paper-dark", "solarized-dark", "solarized-light",
            "glass", "dense", "kuro", "shoin", "ookii", "kumihan", "kon")
        const val KEY_POPUP_LOOKUPS = "reader_popup_lookups"
        const val KEY_CURSOR_BUTTON = "reader_cursor_button"
        const val KEY_CURSOR_TOGGLE = "reader_cursor_toggle"
        const val KEY_CURSOR_SPEED = "reader_cursor_speed"
        const val KEY_CURSOR_FRAME = "reader_cursor_frame"
        const val KEY_CURSOR_HOME = "reader_cursor_home"
        const val KEY_SECOND_SCREEN = "reader_second_screen"
        const val KEY_PANEL_ENABLED = "reader_panel_enabled"
        const val KEY_PANEL_AUTO_HIDE = "reader_panel_auto_hide"
        const val KEY_PANEL_DISPLAY = "reader_panel_display"
        const val KEY_PANEL_POSITION = "reader_panel_position"
        const val KEY_PANEL_HEIGHT = "reader_panel_height"
        const val KEY_PANEL_OPACITY = "reader_panel_opacity"
        const val KEY_FONT_SIZE = "reader_font_size"
        const val KEY_FURIGANA = "reader_furigana"
        const val KEY_SHOW_CHOICES = "reader_show_choices"
        const val KEY_REREAD_COLOUR = "reader_reread_colour"
        const val KEY_ADVANCE_BUTTON = "reader_advance_button"
        const val KEY_TOUCH_PLAY = "reader_touch_play"
        const val KEY_STATS_PAUSED = "reader_stats_paused"
        const val KEY_STATS_BUTTON = "reader_stats_button"
        const val KEY_NEXT_CHOICE_BUTTON = "reader_next_choice_button"
        const val KEY_IDLE_MINUTES = "reader_idle_minutes"
        const val KEY_SESSION_HUD = "reader_session_hud"
        const val KEY_STATS_STYLE = "reader_stats_style"
        const val KEY_HOOK_ENABLED = "reader_texthook_enabled"
        const val KEY_HOOK_PORT = "reader_texthook_port"
        const val KEY_HOOK_LAN = "reader_texthook_lan"
        const val KEY_HOOK_FORMAT = "reader_texthook_format"
        const val KEY_HOOK_SPEAKER = "reader_texthook_speaker"
        const val KEY_CLIPBOARD_AUTO = "reader_clipboard_auto"
        const val KEY_CAPTURE_AUDIO = "reader_capture_audio"
        const val KEY_AUDIO_MAX = "reader_audio_max_seconds"
        const val KEY_AUDIO_LEAD = "reader_audio_lead_ms"
        const val KEY_AUDIO_VOICE_TRIM = "reader_audio_voice_trim"
        const val KEY_CARD_CONTEXT_LINES = "reader_card_context_lines"
        const val KEY_SCREENSHOT_CACHE = "reader_screenshot_cache"
        const val KEY_SCREENSHOT_WIDTH = "reader_screenshot_width"
        const val KEY_CARD_AUDIO = "reader_card_audio"

        /** Followed by a game's key: that game's own choice. */
        const val KEY_CARD_AUDIO_GAME = "reader_card_audio_game_"
        const val KEY_ANKI_TRANSPORT = "reader_anki_transport"
        const val KEY_ANKI_DECK = "reader_anki_deck"
        const val KEY_ANKI_MODEL = "reader_anki_model"
        const val KEY_ANKI_FIELDS = "reader_anki_fields"
        const val KEY_ANKI_WORD_AUDIO_OFFERED = "reader_anki_word_audio_offered"
        const val KEY_ANKI_TAGS = "reader_anki_tags"
        const val KEY_ANKICONNECT_URL = "reader_ankiconnect_url"
        const val KEY_ANKICONNECT_KEY = "reader_ankiconnect_key"
        const val KEY_ANKI_DUPLICATES = "reader_anki_duplicates"
        const val KEY_ANKI_DUPLICATES_ANYWHERE = "reader_anki_duplicates_anywhere"
        const val KEY_ANKI_KEY_FIELD = "reader_anki_key_field"
        const val KEY_ANKI_ADDRESS_FIELD = "reader_anki_address_field"

        // A line break: fine in a preferences file, and in no note type's name.
        private const val SEPARATOR = '\n'

        /** What [KEY_ANKI_KEY_FIELD] holds: the note type the field belongs to, and the field. */
        fun keyFieldValue(model: String, field: String) = model + SEPARATOR + field

        const val DISPLAY_AUTO = "auto"
        const val DISPLAY_PRIMARY = "primary"
        const val DISPLAY_SECONDARY = "secondary"
        const val POSITION_TOP = "top"
        const val POSITION_BOTTOM = "bottom"
        const val FORMAT_PLAIN = "plain"
        const val FORMAT_SENTENCE = "sentence"
        const val FORMAT_JSON = "json"
        const val ANKI_NONE = "none"
        const val ANKI_ANKIDROID = "ankidroid"
        const val ANKI_ANKICONNECT = "ankiconnect"
    }
}
