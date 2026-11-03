// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package com.flycast.emulator.reader

import com.flycast.emulator.reader.ui.TouchButtons

/**
 * What the reader layer, which every reader fork shares (the repository's shared/), needs to
 * know of this app and its console: the twin of reader/core/console.h. This file is the
 * fork's own: each fork has one, with the same names.
 */
object Console {
    /** The app's name, and its key: the statistics record, the Anki tag, file names. */
    const val APP_NAME = "Flycast Reader"
    const val APP_KEY = "flycast-reader"
    /** What the files the app gives Anki begin with. */
    const val MEDIA_PREFIX = "flycast_reader_"
    /** The reader's lines in Android's log. */
    const val LOG_TAG = "FlycastReader"

    /**
     * Writes one of the reader's lines to the emulator's own log, where the app has one the
     * reader may write to; false when it has none (the line goes to Android's log under
     * [LOG_TAG]).
     */
    fun log(priority: Int, message: String): Boolean = false

    /** The button that holds the reader's cursor at first: L2, as Android numbers a controller's buttons (KeyEvent.KEYCODE_BUTTON_L2). */
    const val CURSOR_BUTTON_DEFAULT = 104

    /**
     * A game's ID as its disc gives it, and as a game script is named: a Dreamcast disc's
     * product number, capitals, digits and hyphens with a digit among them ("HDR-0164",
     * "T-1214M", "MK-51058").
     */
    val GAME_ID = Regex("(?=[A-Z-]*[0-9])[A-Z0-9][A-Z0-9-]{3,9}")
    /** Whether an ID is written in capitals (true) or small letters (false) where it is compared. */
    const val GAME_ID_UPPER = true
    /** A script's file name, as the messages give an example of one. */
    const val SCRIPT_EXAMPLE = "HDR-0164.lua"

    /**
     * The game's page for a card's {url}. A Dreamcast game has no page of its own that its
     * product number leads to: the address is Sega's list of the console's software, with the
     * number after it.
     */
    fun gamePage(id: String): String = "https://sega.jp/history/hard/dreamcast/software.html#${id.uppercase()}"

    /** The published list of game scripts. */
    const val SCRIPT_INDEX_URL =
        "https://raw.githubusercontent.com/sommepo/flycast-reader/main/reader/profiles/index.json"

    /** The console's buttons the reader presses, by their bit in the first controller's button word. */
    const val BUTTON_YES = 2
    const val BUTTON_NO = 1
    const val BUTTON_UP = 4
    const val BUTTON_DOWN = 5
    const val BUTTON_LEFT = 6
    const val BUTTON_RIGHT = 7

    /** How long a button is held when the reader presses it, and the time between the presses that move the game's cursor to an answer (ms). */
    const val BUTTON_PRESS_MS = 80L
    const val PICK_STEP_MS = 220L

    /** Whether the emulator can run a game on to its next choice ([NextChoice]). */
    const val HAS_NEXT_CHOICE = false

    /**
     * Whether a second display is left to the reader (ui/SecondScreen: its trackpad and panel).
     * False where the emulator itself shows a screen of the console there.
     */
    const val SECOND_DISPLAY_FREE = true

    /** Whether Reader settings has a page of this app's own (Section.CONSOLE, reader_prefs_console.xml). */
    const val CONSOLE_PAGE = false

    /**
     * The CRT picture's first look on this app (kept once as the user's own when nothing is kept
     * yet), or null for the shared table's: the table's first values are a strong tube for a
     * television, too much on a handheld's small screen. On, as the console was played on a tube.
     */
    val CRT_FIRST_LOOK: String? =
        "enabled=1;lines=0;scanlines=0.40;beam=0.60;sharpness=0.75;mask=1;mask_strength=0.20;" +
            "warp=0.20;corners=0.15;vignette=0.20;glass=0;glow=0.15;halation=0.10;bloom=0.30;" +
            "brightness=0.95;warmth=0.20;colour=1.00;contrast=1.00;convergence=0.05;grain=0.05;flicker=0"

    /** The advance button's setting as saved (reader_advance_button), and the button it means. */
    const val ADVANCE_DEFAULT = "2"

    /**
     * The button a saved advance setting means: a bit of the Dreamcast's button word (A 2, B 1,
     * X 10, Y 9, Start 3). The first builds offered the PSP's bits (circle 13, cross 14), which
     * here are the second d-pad: those are taken as A and B.
     */
    fun advanceButton(saved: String?): Int = when (val bit = saved?.toIntOrNull()) {
        13, null -> BUTTON_YES
        14 -> BUTTON_NO
        else -> bit
    }

    /** Touch play's column of buttons: every button in order, those of the left column, and each by the name scripts use. */
    val TOUCH_ALL: List<String> = listOf("up", "down", "left", "right", "y", "a", "b", "x", "start")
    val TOUCH_LEFT: Set<String> = setOf("up", "down", "left", "right")

    /** The face buttons where the controller has them, top, right, bottom and left (the Dreamcast's: Y at the top, B right, A at the bottom, X left). */
    val TOUCH_FACE: List<String> = listOf("y", "b", "a", "x")

    /**
     * The Dreamcast controller's buttons by the names scripts use, each with its bit in the
     * console's button word (what [ReaderNative.pressButton] takes) and the colour of its letter.
     */
    val TOUCH_BUTTONS: Map<String, TouchButtons.Button> = mapOf(
        "up" to TouchButtons.Button(4, "▲", 0xFFE8ECF2.toInt()),
        "down" to TouchButtons.Button(5, "▼", 0xFFE8ECF2.toInt()),
        "left" to TouchButtons.Button(6, "◀", 0xFFE8ECF2.toInt()),
        "right" to TouchButtons.Button(7, "▶", 0xFFE8ECF2.toInt()),
        "a" to TouchButtons.Button(2, "A", 0xFFFF6B6B.toInt()),
        "b" to TouchButtons.Button(1, "B", 0xFF7DA7FF.toInt()),
        "x" to TouchButtons.Button(10, "X", 0xFFFFD54F.toInt()),
        "y" to TouchButtons.Button(9, "Y", 0xFF6EE7A8.toInt()),
        "start" to TouchButtons.Button(3, "START", 0xFFE8ECF2.toInt()),
    )

    /** Made-up game IDs the tests use: of the console's form, none of a real game. */
    object Test {
        const val GAME = "T-99001"
        const val OTHER_GAME = "T-99002"
        const val UNKNOWN_GAME = "T-99999"
        const val SCRIPT_GAME = "HDR-9999"
        const val OTHER_SCRIPT_GAME = "HDR-9998"
        /** An ID of another console's form, which this one's is not. */
        const val FOREIGN_ID = "0100d7f00fb1a000"
    }
}
