// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader

/**
 * What the reader layer, which every reader fork shares (the Flycast Reader fork's
 * the repository's shared/), needs to know of this app and its console: the twin of
 * src/core/reader/console.h. This file is the fork's own: each fork has one, with the same
 * names.
 */
object Console {
    /** The app's name, and its key: the statistics record, the Anki tag, file names. */
    const val APP_NAME = "Azahar Reader"
    const val APP_KEY = "azahar-reader"
    /** What the files the app gives Anki begin with. */
    const val MEDIA_PREFIX = "azahar_reader_"
    /** The reader's lines in Android's log, where the emulator's own log is not there. */
    const val LOG_TAG = "AzaharReader"

    /**
     * Writes one of the reader's lines to the emulator's own log, where the app has one the
     * reader may write to; false when it has none (the line goes to Android's log under
     * [LOG_TAG]).
     */
    fun log(priority: Int, message: String): Boolean = false

    /**
     * The button that holds the reader's cursor at first: ZL, by the emulator's numbers
     * (NativeLibrary.ButtonType.BUTTON_ZL): every emulated button, the on-screen controller's
     * too, passes the reader on its way to the game (ReaderGlue.onButton).
     */
    const val CURSOR_BUTTON_DEFAULT = 707

    /** A game's ID, and how a game script is named: the program's title ID ("0004000000088A00"). */
    val GAME_ID = Regex("[0-9A-F]{16}")
    /** Whether an ID is written in capitals (true) or small letters (false) where it is compared. */
    const val GAME_ID_UPPER = true
    /** A script's file name, as the messages give an example of one. */
    const val SCRIPT_EXAMPLE = "0004000000088A00.lua"

    /** The game's page for a card's {url}: none, a 3DS title ID leads to no page of its own. */
    fun gamePage(@Suppress("UNUSED_PARAMETER") id: String): String = ""

    /** The published list of game scripts: in Furoshiki's repository (nothing there until it is published). */
    const val SCRIPT_INDEX_URL =
        "https://raw.githubusercontent.com/sommepo/furoshiki/main/azahar/src/android/app/src/main/assets/reader/profiles/index.json"

    /** The console's buttons the reader presses, by the emulator's numbers (NativeLibrary.ButtonType). */
    const val BUTTON_YES = 700 // A
    const val BUTTON_NO = 701 // B
    const val BUTTON_UP = 709
    const val BUTTON_DOWN = 710
    const val BUTTON_LEFT = 711
    const val BUTTON_RIGHT = 712

    /** How long a button is held when the reader presses it, and the time between the presses that move the game's cursor to an answer (ms). */
    const val BUTTON_PRESS_MS = 80L
    const val PICK_STEP_MS = 220L

    /** Whether the emulator can run a game on to its next choice ([NextChoice]). */
    const val HAS_NEXT_CHOICE = false

    /**
     * Whether a second display is left to the reader (ui/SecondScreen: its trackpad and panel).
     * Not here: the emulator shows the lower screen there (display/SecondaryDisplay), and the
     * reader's layer lies over it (ReaderGlue.geometry).
     */
    const val SECOND_DISPLAY_FREE = false

    /** Whether Reader settings has a page of this app's own (Section.CONSOLE, reader_prefs_console.xml): the game list's. */
    const val CONSOLE_PAGE = true

    /** The CRT picture's first look on this app, or null for the shared table's (off until switched on here). */
    val CRT_FIRST_LOOK: String? = null

    /** The advance button's setting as saved (reader_advance_button) at first. */
    const val ADVANCE_DEFAULT = "700"

    /** The button a saved advance setting means: a NativeLibrary.ButtonType number. */
    fun advanceButton(saved: String?): Int = saved?.toIntOrNull() ?: BUTTON_YES

    /** Touch play's column of buttons: every button in order, those of the left column, and each by the name scripts use. */
    val TOUCH_ALL: List<String> = listOf("up", "down", "left", "right", "l", "r", "y", "a", "b", "x", "select", "start")
    val TOUCH_LEFT: Set<String> = setOf("up", "down", "left", "right", "l")

    /** The face buttons where the controller has them, top, right, bottom and left (the 3DS's: X at the top, A right, B at the bottom, Y left). */
    val TOUCH_FACE: List<String> = listOf("x", "a", "b", "y")

    /**
     * The 3DS's buttons by the names scripts use, each with the emulator's number for it
     * (NativeLibrary.ButtonType: what [ReaderNative.pressButton] takes) and the colour of its
     * letter.
     */
    val TOUCH_BUTTONS: Map<String, org.citra.citra_emu.reader.ui.TouchButtons.Button> = mapOf(
        "up" to org.citra.citra_emu.reader.ui.TouchButtons.Button(BUTTON_UP, "▲", 0xFFE8ECF2.toInt()),
        "down" to org.citra.citra_emu.reader.ui.TouchButtons.Button(BUTTON_DOWN, "▼", 0xFFE8ECF2.toInt()),
        "left" to org.citra.citra_emu.reader.ui.TouchButtons.Button(BUTTON_LEFT, "◀", 0xFFE8ECF2.toInt()),
        "right" to org.citra.citra_emu.reader.ui.TouchButtons.Button(BUTTON_RIGHT, "▶", 0xFFE8ECF2.toInt()),
        "l" to org.citra.citra_emu.reader.ui.TouchButtons.Button(773, "L", 0xFFE8ECF2.toInt()),
        "r" to org.citra.citra_emu.reader.ui.TouchButtons.Button(774, "R", 0xFFE8ECF2.toInt()),
        "a" to org.citra.citra_emu.reader.ui.TouchButtons.Button(BUTTON_YES, "A", 0xFFE8ECF2.toInt()),
        "b" to org.citra.citra_emu.reader.ui.TouchButtons.Button(BUTTON_NO, "B", 0xFFE8ECF2.toInt()),
        "x" to org.citra.citra_emu.reader.ui.TouchButtons.Button(702, "X", 0xFFE8ECF2.toInt()),
        "y" to org.citra.citra_emu.reader.ui.TouchButtons.Button(703, "Y", 0xFFE8ECF2.toInt()),
        "start" to org.citra.citra_emu.reader.ui.TouchButtons.Button(704, "START", 0xFFE8ECF2.toInt()),
        "select" to org.citra.citra_emu.reader.ui.TouchButtons.Button(705, "SELECT", 0xFFE8ECF2.toInt()),
    )

    /** Made-up game IDs the tests use: of the console's form, none of a real game. */
    object Test {
        const val GAME = "0004000000099001"
        const val OTHER_GAME = "0004000000099002"
        const val UNKNOWN_GAME = "0004000000099999"
        const val SCRIPT_GAME = "0004000000098999"
        const val OTHER_SCRIPT_GAME = "0004000000098998"
        /** An ID of another console's form, which this one's is not. */
        const val FOREIGN_ID = "HDR-0164"
    }
}
