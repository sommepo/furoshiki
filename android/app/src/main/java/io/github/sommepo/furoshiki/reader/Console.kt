// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.reader

import io.github.sommepo.furoshiki.reader.ui.TouchButtons

/**
 * What the shared reader layer knows of the console: the twin of the desktop program's
 * engine/reader/core/console.h. A reader app's Console.kt says it at compile time for its one
 * console; this one holds a table, and GameActivity says which console this process plays
 * ([use]) before anything of the reader starts. The names are a fork's Console.kt's.
 *
 * Buttons are a core's: libretro's joypad bits (a south button B is 0, east A is 8).
 */
object Console {
    class Info(
        val key: String,
        val appName: String,
        val appKey: String,
        val mediaPrefix: String,
        val gameId: Regex,
        val scriptExample: String,
        val gamePage: (String) -> String,
        val scriptIndexUrl: String,
        val yes: Int,
        val no: Int,
        val up: Int,
        val down: Int,
        val left: Int,
        val right: Int,
        val pressMs: Long,
        val pickStepMs: Long,
        val crtFirstLook: String?,
        val touchAll: List<String>,
        val touchLeft: Set<String>,
        val touchFace: List<String>,
        val touchButtons: Map<String, TouchButtons.Button>,
        /** A libretro bit's button in the core's own numbers, where they are not libretro's. */
        val pad: IntArray? = null,
    )

    private val PSP = Info(
        key = "psp",
        appName = "PPSSPP Reader",
        appKey = "ppsspp-reader",
        mediaPrefix = "ppsspp_reader_",
        gameId = Regex("[A-Z]{4}[0-9]{5}"),
        scriptExample = "ULJS00219.lua",
        gamePage = { id -> "https://www.jp.playstation.com/software/title/${id.lowercase()}.html" },
        scriptIndexUrl = "https://raw.githubusercontent.com/sommepo/furoshiki/main/ppsspp/assets/reader/profiles/index.json",
        yes = 8, no = 0, up = 4, down = 5, left = 6, right = 7,
        pressMs = 80L,
        pickStepMs = 220L,
        crtFirstLook = "enabled=1;lines=0;scanlines=0.40;beam=0.60;sharpness=0.75;mask=1;mask_strength=0.20;" +
            "warp=0.20;corners=0.15;vignette=0.20;glass=0;glow=0.15;halation=0.10;bloom=0.30;" +
            "brightness=0.95;warmth=0.20;colour=1.00;contrast=1.00;convergence=0.05;grain=0.05;flicker=0",
        touchAll = listOf("up", "down", "left", "right", "select", "l", "triangle", "circle", "cross", "square", "start", "r"),
        touchLeft = setOf("up", "down", "left", "right", "select", "l"),
        touchFace = listOf("triangle", "circle", "cross", "square"),
        touchButtons = mapOf(
            "up" to TouchButtons.Button(4, "▲", 0xFFE8ECF2.toInt()),
            "down" to TouchButtons.Button(5, "▼", 0xFFE8ECF2.toInt()),
            "left" to TouchButtons.Button(6, "◀", 0xFFE8ECF2.toInt()),
            "right" to TouchButtons.Button(7, "▶", 0xFFE8ECF2.toInt()),
            "circle" to TouchButtons.Button(8, "○", 0xFFFF6B6B.toInt()),
            "cross" to TouchButtons.Button(0, "×", 0xFF7DA7FF.toInt()),
            "square" to TouchButtons.Button(1, "□", 0xFFFF8FD8.toInt()),
            "triangle" to TouchButtons.Button(9, "△", 0xFF6EE7A8.toInt()),
            "start" to TouchButtons.Button(3, "START", 0xFFE8ECF2.toInt()),
            "select" to TouchButtons.Button(2, "SELECT", 0xFFE8ECF2.toInt()),
            "l" to TouchButtons.Button(10, "L", 0xFFE8ECF2.toInt()),
            "r" to TouchButtons.Button(11, "R", 0xFFE8ECF2.toInt()),
        ),
    )

    private val DREAMCAST = Info(
        key = "dc",
        appName = "Flycast Reader",
        appKey = "flycast-reader",
        mediaPrefix = "flycast_reader_",
        gameId = Regex("(?=[A-Z-]*[0-9])[A-Z0-9][A-Z0-9-]{3,9}"),
        scriptExample = "HDR-0164.lua",
        gamePage = { id -> "https://sega.jp/history/hard/dreamcast/software.html#${id.uppercase()}" },
        scriptIndexUrl = "https://raw.githubusercontent.com/sommepo/furoshiki/main/flycast/reader/profiles/index.json",
        yes = 0, no = 8, up = 4, down = 5, left = 6, right = 7,
        pressMs = 80L,
        pickStepMs = 220L,
        crtFirstLook = PSP.crtFirstLook,
        touchAll = listOf("up", "down", "left", "right", "y", "a", "b", "x", "start"),
        touchLeft = setOf("up", "down", "left", "right"),
        touchFace = listOf("y", "b", "a", "x"),
        touchButtons = mapOf(
            "up" to TouchButtons.Button(4, "▲", 0xFFE8ECF2.toInt()),
            "down" to TouchButtons.Button(5, "▼", 0xFFE8ECF2.toInt()),
            "left" to TouchButtons.Button(6, "◀", 0xFFE8ECF2.toInt()),
            "right" to TouchButtons.Button(7, "▶", 0xFFE8ECF2.toInt()),
            "a" to TouchButtons.Button(0, "A", 0xFFFF6B6B.toInt()),
            "b" to TouchButtons.Button(8, "B", 0xFF7DA7FF.toInt()),
            "x" to TouchButtons.Button(1, "X", 0xFFFFD54F.toInt()),
            "y" to TouchButtons.Button(9, "Y", 0xFF6EE7A8.toInt()),
            "start" to TouchButtons.Button(3, "START", 0xFFE8ECF2.toInt()),
        ),
    )

    private val THREE_DS = Info(
        key = "3ds",
        appName = "Azahar Reader",
        appKey = "azahar-reader",
        mediaPrefix = "azahar_reader_",
        gameId = Regex("[0-9A-F]{16}"),
        scriptExample = "0004000000088A00.lua",
        gamePage = { "" },
        scriptIndexUrl = "https://raw.githubusercontent.com/sommepo/furoshiki/main/azahar/src/android/app/src/main/assets/reader/profiles/index.json",
        yes = 8, no = 0, up = 4, down = 5, left = 6, right = 7,
        pressMs = 80L,
        pickStepMs = 220L,
        crtFirstLook = null,
        touchAll = listOf("up", "down", "left", "right", "l", "r", "y", "a", "b", "x", "select", "start"),
        touchLeft = setOf("up", "down", "left", "right", "l"),
        touchFace = listOf("x", "a", "b", "y"),
        touchButtons = mapOf(
            "up" to TouchButtons.Button(4, "▲", 0xFFE8ECF2.toInt()),
            "down" to TouchButtons.Button(5, "▼", 0xFFE8ECF2.toInt()),
            "left" to TouchButtons.Button(6, "◀", 0xFFE8ECF2.toInt()),
            "right" to TouchButtons.Button(7, "▶", 0xFFE8ECF2.toInt()),
            "l" to TouchButtons.Button(10, "L", 0xFFE8ECF2.toInt()),
            "r" to TouchButtons.Button(11, "R", 0xFFE8ECF2.toInt()),
            "a" to TouchButtons.Button(8, "A", 0xFFE8ECF2.toInt()),
            "b" to TouchButtons.Button(0, "B", 0xFFE8ECF2.toInt()),
            "x" to TouchButtons.Button(9, "X", 0xFFE8ECF2.toInt()),
            "y" to TouchButtons.Button(1, "Y", 0xFFE8ECF2.toInt()),
            "start" to TouchButtons.Button(3, "START", 0xFFE8ECF2.toInt()),
            "select" to TouchButtons.Button(2, "SELECT", 0xFFE8ECF2.toInt()),
        ),
    )

    private val PLAYSTATION = Info(
        key = "ps1",
        appName = "Station Reader",
        appKey = "station-reader",
        mediaPrefix = "station_reader_",
        gameId = Regex("[A-Z]{4}-[0-9]{5}"),
        scriptExample = "SLPS-01794.lua",
        gamePage = { id -> "https://www.jp.playstation.com/software/title/${id.lowercase().replace("-", "")}.html" },
        scriptIndexUrl = "https://raw.githubusercontent.com/sommepo/furoshiki/main/station/reader/profiles/index.json",
        yes = 13, no = 14, up = 4, down = 6, left = 7, right = 5,
        pressMs = 100L,
        pickStepMs = 250L,
        crtFirstLook = PSP.crtFirstLook,
        touchAll = listOf(
            "up", "down", "left", "right", "select", "l1", "l2",
            "triangle", "circle", "cross", "square", "start", "r1", "r2",
        ),
        touchLeft = setOf("up", "down", "left", "right", "select", "l1", "l2"),
        touchFace = listOf("triangle", "circle", "cross", "square"),
        touchButtons = mapOf(
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
        ),
        // DuckStation's DigitalController: south, west, select, start, up, down, left, right,
        // east, north, L1, R1, L2, R2, L3, R3 by libretro's bits.
        pad = intArrayOf(14, 15, 0, 3, 4, 6, 7, 5, 13, 12, 10, 11, 8, 9, 1, 2),
    )

    private val PS2 = Info(
        key = "ps2",
        appName = "ARMSX2 Reader",
        appKey = "armsx2-reader",
        mediaPrefix = "armsx2_reader_",
        gameId = Regex("[A-Z]{4}-[0-9]{5}"),
        scriptExample = "SLPM-65555.lua",
        gamePage = PLAYSTATION.gamePage,
        scriptIndexUrl = "https://raw.githubusercontent.com/sommepo/furoshiki/main/armsx2/ARMSX2/app/src/main/assets/reader/profiles/index.json",
        yes = 5, no = 6, up = 0, down = 2, left = 3, right = 1,
        pressMs = 100L,
        pickStepMs = 250L,
        crtFirstLook = PSP.crtFirstLook,
        touchAll = PLAYSTATION.touchAll,
        touchLeft = PLAYSTATION.touchLeft,
        touchFace = PLAYSTATION.touchFace,
        touchButtons = mapOf(
            "up" to TouchButtons.Button(0, "▲", 0xFFE8ECF2.toInt()),
            "right" to TouchButtons.Button(1, "▶", 0xFFE8ECF2.toInt()),
            "down" to TouchButtons.Button(2, "▼", 0xFFE8ECF2.toInt()),
            "left" to TouchButtons.Button(3, "◀", 0xFFE8ECF2.toInt()),
            "triangle" to TouchButtons.Button(4, "△", 0xFF6EE7A8.toInt()),
            "circle" to TouchButtons.Button(5, "○", 0xFFFF6B6B.toInt()),
            "cross" to TouchButtons.Button(6, "×", 0xFF7DA7FF.toInt()),
            "square" to TouchButtons.Button(7, "□", 0xFFFF8FD8.toInt()),
            "select" to TouchButtons.Button(8, "SELECT", 0xFFE8ECF2.toInt()),
            "start" to TouchButtons.Button(9, "START", 0xFFE8ECF2.toInt()),
            "l1" to TouchButtons.Button(10, "L1", 0xFFE8ECF2.toInt()),
            "l2" to TouchButtons.Button(11, "L2", 0xFFE8ECF2.toInt()),
            "r1" to TouchButtons.Button(12, "R1", 0xFFE8ECF2.toInt()),
            "r2" to TouchButtons.Button(13, "R2", 0xFFE8ECF2.toInt()),
        ),
        // PCSX2's PadDualshock2::Inputs, by libretro's bits as above.
        pad = intArrayOf(6, 7, 8, 9, 0, 2, 3, 1, 5, 4, 10, 12, 11, 13, 14, 15),
    )

    private val VITA = Info(
        key = "psvita",
        appName = "Vita3K Reader",
        appKey = "vita3k-reader",
        mediaPrefix = "vita3k_reader_",
        gameId = Regex("[A-Z]{4}[0-9]{5}"),
        scriptExample = "PCSG00488.lua",
        gamePage = { id -> "https://github.com/Vita3K/compatibility/issues?q=$id" },
        scriptIndexUrl = "https://raw.githubusercontent.com/sommepo/furoshiki/main/vita3k/reader/profiles/index.json",
        yes = 13, no = 14, up = 4, down = 6, left = 7, right = 5,
        pressMs = 80L,
        pickStepMs = 220L,
        crtFirstLook = null,
        touchAll = listOf(
            "up", "down", "left", "right", "select", "l1",
            "triangle", "circle", "cross", "square", "start", "r1",
        ),
        touchLeft = setOf("up", "down", "left", "right", "select", "l1"),
        touchFace = listOf("triangle", "circle", "cross", "square"),
        touchButtons = PLAYSTATION.touchButtons.filterKeys { it != "l2" && it != "r2" } + mapOf(
            "l1" to TouchButtons.Button(8, "L", 0xFFE8ECF2.toInt()),
            "r1" to TouchButtons.Button(9, "R", 0xFFE8ECF2.toInt()),
        ),
        // Vita3K Reader's core has DuckStation's numbers but for L and R; the Vita has no more.
        pad = intArrayOf(14, 15, 0, 3, 4, 6, 7, 5, 13, 12, 8, 9, 10, 11, 1, 2),
    )


    private val TABLE = listOf(
        PSP, DREAMCAST, THREE_DS, PLAYSTATION, PS2, VITA,
    ).associateBy { it.key }

    /** Before a game: any console's ID, so that the reader's screens list every game's script. */
    private val ANY_GAME_ID = Regex(TABLE.values.joinToString("|") { "(?:${it.gameId.pattern})" })

    @Volatile
    private var chosen: Info? = null

    private val current: Info get() = chosen ?: PSP

    /** The console of the game this process plays, by its core's key. */
    fun use(key: String) {
        chosen = TABLE[key] ?: PSP
        ReaderCatalog.forget()
    }

    val KEY get() = current.key

    val APP_NAME get() = current.appName
    val APP_KEY get() = current.appKey
    val MEDIA_PREFIX get() = current.mediaPrefix
    const val LOG_TAG = "Furoshiki"

    fun log(priority: Int, message: String): Boolean = false

    /** The button that holds the reader's cursor at first: L2, by Android's number for it. */
    const val CURSOR_BUTTON_DEFAULT = 104

    val GAME_ID get() = chosen?.gameId ?: ANY_GAME_ID
    const val GAME_ID_UPPER = true
    val SCRIPT_EXAMPLE get() = current.scriptExample
    fun gamePage(id: String): String = current.gamePage(id)
    val SCRIPT_INDEX_URL get() = current.scriptIndexUrl

    val BUTTON_YES get() = current.yes
    val BUTTON_NO get() = current.no
    val BUTTON_UP get() = current.up
    val BUTTON_DOWN get() = current.down
    val BUTTON_LEFT get() = current.left
    val BUTTON_RIGHT get() = current.right
    val BUTTON_PRESS_MS get() = current.pressMs
    val PICK_STEP_MS get() = current.pickStepMs

    const val HAS_NEXT_CHOICE = false
    /** A second display is the reader's unless the console's lower screen goes there. */
    val SECOND_DISPLAY_FREE get() = !TWO_SCREENS
    val TWO_SCREENS get() = current.key == "3ds"
    const val CONSOLE_PAGE = false

    val CRT_FIRST_LOOK get() = current.crtFirstLook
    /** The advance button at first: -1, whichever is yes on the console being played. */
    const val ADVANCE_DEFAULT = "-1"
    fun advanceButton(saved: String?): Int = saved?.toIntOrNull()?.takeIf { it >= 0 }?.let { padBit(it) } ?: BUTTON_YES

    /** The core's number for a button given by libretro's bit, which is how this app names them by place. */
    fun padBit(bit: Int): Int = current.pad?.getOrNull(bit) ?: bit

    val TOUCH_ALL get() = current.touchAll
    val TOUCH_LEFT get() = current.touchLeft
    val TOUCH_FACE get() = current.touchFace
    val TOUCH_BUTTONS get() = current.touchButtons

    /** Made-up game IDs the tests use: of the PSP's form, none of a real game. */
    object Test {
        const val GAME = "ULJM99001"
        const val OTHER_GAME = "ULJM99002"
        const val UNKNOWN_GAME = "ULJM99997"
        const val SCRIPT_GAME = "ULJM99999"
        const val OTHER_SCRIPT_GAME = "ULJM99998"
        const val FOREIGN_ID = "0100d7f00fb1a000"
    }
}
