// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader.ui

/** Colours of the reader's in-game surfaces. They sit over game pictures, so they are fixed
 * rather than following the app theme. */
object ReaderStyle {
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
    const val POPUP_TEXT = 0xFFF3F5F7.toInt()
    const val POPUP_DIM = 0xFFC2C8CF.toInt()
    const val POPUP_ACCENT = 0xFF9FD8FF.toInt()
    const val POPUP_SIGN = 0xFF2A2F35.toInt()
    const val POPUP_BOOK_SIGN = 0xFF2A2F35.toInt()
    const val POPUP_BOX = 0xFFE4E8EC.toInt()
    const val POPUP_BOX_PRESSED = 0xFFB8BEC5.toInt()
    const val POPUP_EDGE = 0xFF1D2024.toInt()
    const val POPUP_BUTTON_TEXT = 0xFF2A2F35.toInt()
    const val POPUP_BUTTON_KEEP = 0xFF2A2F35.toInt()

    /** Over the bottom screen while it is the cursor's trackpad. */
    const val TRACKPAD_DIM = 0x99000000.toInt()
    const val CURSOR = ACCENT
    const val CURSOR_EDGE = 0xD0000000.toInt()

    /** The session line in the corner: there when looked for, not when playing. */
    const val SESSION_TEXT = 0xB8FFFFFF.toInt()
    const val SESSION_SHADOW = 0xE6000000.toInt()

    /**
     * A bare trackpad on a second display of the reader's own (not in this app, whose second
     * display shows the lower screen): its veil, its edge and its word.
     */
    const val PAD_VEIL = 0x66000000
    const val PAD_EDGE = 0x8C2E9BE8.toInt()
    const val PAD_HINT = 0x66FFFFFF
    const val PAD_HINT_ACTIVE = 0xCC2E9BE8.toInt()

    /** Touch play's buttons: their ground and edge (the 3DS's dark grey, a thin white rim). */
    const val TOUCH_FILL = 0xCC3A3A3C.toInt()
    const val TOUCH_EDGE = 0x99FFFFFF.toInt()
}
