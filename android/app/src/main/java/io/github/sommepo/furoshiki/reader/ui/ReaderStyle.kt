// SPDX-FileCopyrightText: PPSSPP Reader fork contributors (from the PPSSPP Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.reader.ui

import io.github.sommepo.furoshiki.reader.Console

/** Colours of the reader's in-game surfaces. They sit over game pictures, so they are fixed
 * rather than following the app theme. */
object ReaderStyle {
    /** A colour by console, the PSP's first. */
    private fun pick(
        psp: Long,
        dreamcast: Long = psp,
        threeDs: Long = psp,
        playStation: Long = psp,
        ps2: Long = psp,
        switch: Long = psp,
    ) = when (Console.KEY) {
        "dc" -> dreamcast
        "3ds" -> threeDs
        "ps1", "psvita" -> playStation
        "ps2" -> ps2
        "switch" -> switch
        else -> psp
    }.toInt()

    const val PANEL_BACKGROUND = 0xFF12161C.toInt() // alpha comes from the opacity setting
    const val POPUP_BACKGROUND = 0xF21A1F27.toInt()
    const val OUTLINE = 0x55FFFFFF
    const val TEXT = 0xFFF4F4F2.toInt()
    const val TEXT_DIM = 0xFFAEB4BC.toInt()
    const val TEXT_REREAD = 0xFFFF9E8A.toInt()
    const val ACCENT = 0xFFFFD54F.toInt()
    const val HIGHLIGHT = 0x66FFD54F

    /** The button boxes of the popup: a shade lighter than the popup, and opaque. */
    const val BOX = 0xFF2A313B.toInt()
    const val BOX_PRESSED = 0xFF55606E.toInt()

    /**
     * The word popup and the question about a card's audio are the console's own, as in the
     * desktop app ([ConsoleLook] draws their ground and the pointer).
     */
    val POPUP_TEXT get() = pick(0xFFFFFFFF, 0xFF1F3F8F, 0xFFF3F5F7, 0xFF23242B)
    val POPUP_DIM get() = pick(0xFFD3E4F8, 0xFF6B82B5, 0xFFC2C8CF, 0xFF6B6C74, ps2 = 0xFF7FA6D6, switch = 0xFFB4B4B4)
    val POPUP_ACCENT get() = pick(0xFFFFFFFF, 0xFFD8620A, 0xFF9FD8FF, 0xFF2B3A8C, ps2 = 0xFF9FD0FF, switch = 0xFF1FE0C8)
    val POPUP_SIGN get() = pick(0xFFFFFFFF, 0xFFFFFFFF, 0xFF2A2F35, ps2 = 0xFF9FD0FF, switch = 0xFF1FE0C8)
    val POPUP_BOOK_SIGN get() = pick(0xFFFFFFFF, 0xFFFFFFFF, 0xFF2A2F35)
    val POPUP_BOX get() = pick(0xFF1E4696, 0xFFF27C1E, 0xFFE4E8EC, 0xFF2B3A8C, ps2 = 0xFF0B122E, switch = 0xFF3B3B3B)
    val POPUP_BOX_PRESSED get() = pick(0xFF3C78D2, 0xFFD8620A, 0xFFB8BEC5, 0xFF1A245C, ps2 = 0xFF1F336B, switch = 0xFF585858)
    val POPUP_EDGE get() = pick(0x99FFFFFF, 0xFFC9D6EC, 0xFF1D2024, 0xFF9C9B95, ps2 = 0x8C7FC4FF, switch = 0xFF555555)
    val POPUP_BUTTON_TEXT get() = pick(0xFFFFFFFF, 0xFFFFFFFF, 0xFF2A2F35)
    val POPUP_BUTTON_KEEP get() = pick(0xFFFFFFFF, 0xFFFFFFFF, 0xFF2A2F35, ps2 = 0xFF9FD0FF, switch = 0xFF1FE0C8)

    /** Over a part of the game's display while it is the cursor's trackpad. */
    const val TRACKPAD_DIM = 0x99000000.toInt()

    /**
     * The second display, which is black while it has nothing to do, as the cursor's trackpad:
     * a faint surface with an edge, so that it is plain where the finger goes.
     */
    const val PAD_SURFACE = 0xFF1B2028.toInt()

    /** What lies over the console's ground on the second display's trackpad, to quieten it. */
    const val PAD_VEIL = 0x66000000
    // The trackpad's edge and its word are in the console's own accent (POPUP_ACCENT).
    val PAD_EDGE get() = pick(0x8CFFFFFF, 0x8CD8620A, 0x8C9FD8FF, 0x8CC8C7C1, ps2 = 0x8C9FD0FF, switch = 0x8C1FE0C8)
    const val PAD_HINT = 0x66FFFFFF
    val PAD_HINT_ACTIVE get() = pick(0xCCFFFFFF, 0xCCD8620A, 0xCC9FD8FF, 0xCCC8C7C1, ps2 = 0xCC9FD0FF, switch = 0xCC1FE0C8)
    const val CURSOR = ACCENT
    const val CURSOR_EDGE = 0xD0000000.toInt()

    /** The session line in the corner: there when looked for, not when playing. */
    const val SESSION_TEXT = 0xB8FFFFFF.toInt()
    const val SESSION_SHADOW = 0xE6000000.toInt()

    /** Touch play's column of buttons: their ground and edge. */
    const val TOUCH_FILL = 0xCC1A1F27.toInt()
    const val TOUCH_EDGE = 0x99FFFFFF.toInt()
}
