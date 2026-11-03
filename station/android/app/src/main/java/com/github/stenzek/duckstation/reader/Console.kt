// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader

import com.github.stenzek.duckstation.reader.ui.TouchButtons

/**
 * What the reader layer, which every reader fork shares (the Flycast Reader fork's
 * the repository's shared/), needs to know of this app and its console: the twin of
 * reader/core/console.h. This file is the fork's own: each fork has one, with the same names.
 */
object Console {
    /** The app's name, and its key: the statistics record, the Anki tag, file names. */
    const val APP_NAME = "Station Reader"
    const val APP_KEY = "station-reader"
    /** What the files the app gives Anki begin with. */
    const val MEDIA_PREFIX = "station_reader_"
    /** The reader's lines in Android's log. */
    const val LOG_TAG = "StationReader"

    /**
     * Writes one of the reader's lines to the emulator's own log, where the app has one the
     * reader may write to; false when it has none (the line goes to Android's log under
     * [LOG_TAG]).
     */
    fun log(priority: Int, message: String): Boolean = false

    /** The button that holds the reader's cursor at first: L2, as Android numbers a controller's buttons (KeyEvent.KEYCODE_BUTTON_L2). */
    const val CURSOR_BUTTON_DEFAULT = 104

    /**
     * A game's ID as its disc gives it, and as a game script is named: the disc's serial as its
     * boot file names it, four capitals, a hyphen and five digits ("SLPS-01794").
     */
    val GAME_ID = Regex("[A-Z]{4}-[0-9]{5}")
    /** Whether an ID is written in capitals (true) or small letters (false) where it is compared. */
    const val GAME_ID_UPPER = true
    /** A script's file name, as the messages give an example of one. */
    const val SCRIPT_EXAMPLE = "SLPS-01794.lua"

    /**
     * The game's page for a card's {url}: its page at PlayStation, which names it by its serial
     * in small letters without the hyphen ("slps01794").
     */
    fun gamePage(id: String): String =
        "https://www.jp.playstation.com/software/title/${id.lowercase().replace("-", "")}.html"

    /** The published list of game scripts: in Furoshiki's repository (nothing there until it is published). */
    const val SCRIPT_INDEX_URL =
        "https://raw.githubusercontent.com/sommepo/furoshiki/main/station/reader/profiles/index.json"

    /**
     * The console's buttons the reader presses, by the emulator's numbers for them
     * (DigitalController::Button, what [ReaderNative.pressButton] takes).
     */
    const val BUTTON_YES = 13 // circle: yes on a Japanese console
    const val BUTTON_NO = 14 // cross
    const val BUTTON_UP = 4
    const val BUTTON_DOWN = 6
    const val BUTTON_LEFT = 7
    const val BUTTON_RIGHT = 5

    /**
     * How long a button is held when the reader presses it (a PlayStation game reads its pad once
     * a frame), and the time between the presses that move the game's cursor to an answer (ms).
     */
    const val BUTTON_PRESS_MS = 100L
    const val PICK_STEP_MS = 250L

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

    /** The advance button's setting as saved (reader_advance_button) at first. */
    const val ADVANCE_DEFAULT = "13"

    /** The button a saved advance setting means: one of the controller's by the emulator's number (circle 13, cross 14, Start 3). */
    fun advanceButton(saved: String?): Int = saved?.toIntOrNull()?.takeIf { it in 0..15 } ?: BUTTON_YES

    /** Touch play's column of buttons: every button in order, those of the left column, and each by the name scripts use. */
    val TOUCH_ALL: List<String> =
        listOf("up", "down", "left", "right", "select", "l1", "l2",
            "triangle", "circle", "cross", "square", "start", "r1", "r2")
    val TOUCH_LEFT: Set<String> = setOf("up", "down", "left", "right", "select", "l1", "l2")

    /** The face buttons where the controller has them, top, right, bottom and left (△ at the top, ○ right, × at the bottom, □ left). */
    val TOUCH_FACE: List<String> = listOf("triangle", "circle", "cross", "square")

    /**
     * The controller's buttons by the names scripts use, each with the emulator's number for it
     * (what [ReaderNative.pressButton] takes) and the colour of its sign.
     */
    val TOUCH_BUTTONS: Map<String, TouchButtons.Button> = mapOf(
        "up" to TouchButtons.Button(4, "▲", 0xFFE8ECF2.toInt()),
        "down" to TouchButtons.Button(6, "▼", 0xFFE8ECF2.toInt()),
        "left" to TouchButtons.Button(7, "◀", 0xFFE8ECF2.toInt()),
        "right" to TouchButtons.Button(5, "▶", 0xFFE8ECF2.toInt()),
        "circle" to TouchButtons.Button(13, "○", 0xFFFF6B6B.toInt()),
        "cross" to TouchButtons.Button(14, "×", 0xFF7DA7FF.toInt()),
        "square" to TouchButtons.Button(15, "□", 0xFFFF8FD8.toInt()),
        "triangle" to TouchButtons.Button(12, "△", 0xFF6EE7A8.toInt()),
        "start" to TouchButtons.Button(3, "START", 0xFFE8ECF2.toInt()),
        "select" to TouchButtons.Button(0, "SELECT", 0xFFE8ECF2.toInt()),
        "l1" to TouchButtons.Button(10, "L1", 0xFFE8ECF2.toInt()),
        "l2" to TouchButtons.Button(8, "L2", 0xFFE8ECF2.toInt()),
        "r1" to TouchButtons.Button(11, "R1", 0xFFE8ECF2.toInt()),
        "r2" to TouchButtons.Button(9, "R2", 0xFFE8ECF2.toInt()),
    )

    /** Made-up game IDs the tests use: of the console's form, none of a real game. */
    object Test {
        const val GAME = "SLPS-69001"
        const val OTHER_GAME = "SLPS-69002"
        const val UNKNOWN_GAME = "SLPS-69999"
        const val SCRIPT_GAME = "SLPS-99999"
        const val OTHER_SCRIPT_GAME = "SLPS-99998"
        /** An ID of another console's form, which this one's is not. */
        const val FOREIGN_ID = "0100d7f00fb1a000"
    }
}
