// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.app.Dialog
import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.KeyEvent
import android.view.View
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import org.robolectric.shadows.ShadowLooper

/** The one menu over a game ([ShelfMenu]): a controller's keys and a finger both work it. */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(qualifiers = "w768dp-h432dp-400dpi")
class ShelfMenuTest {
    private val activity: Activity get() = Robolectric.buildActivity(Activity::class.java).setup().get()

    private fun press(dialog: Dialog, code: Int) {
        dialog.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, code))
        dialog.dispatchKeyEvent(KeyEvent(KeyEvent.ACTION_UP, code))
        ShadowLooper.idleMainLooper()
    }

    private fun picture(dialog: Dialog, name: String) {
        val dir = System.getenv("READER_PREVIEW_DIR") ?: return
        val root = dialog.findViewById<android.view.ViewGroup>(android.R.id.content).getChildAt(0)
        root.measure(View.MeasureSpec.makeMeasureSpec(1920, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY))
        root.layout(0, 0, 1920, 1080)
        val bitmap = Bitmap.createBitmap(1920, 1080, Bitmap.Config.ARGB_8888)
        root.draw(Canvas(bitmap))
        File(dir).mkdirs()
        File(dir, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun theDpadWalksTheRowsAndADoesTheOneInHand() {
        val done = ArrayList<String>()
        var closed = 0
        val dialog = ShelfMenu(activity).title("Game menu")
            .row("Resume") { done += "resume" }
            .row("Quick menu") { done += "quick" }
            .row("Save state", "2026/11/19 22:14") { done += "save" }
            .row("Quit game") { done += "quit" }
            .onClosed { picked -> closed++; picked?.invoke() }
            .show()
        picture(dialog, "menu-game")
        press(dialog, KeyEvent.KEYCODE_DPAD_DOWN)
        press(dialog, KeyEvent.KEYCODE_DPAD_DOWN)
        press(dialog, KeyEvent.KEYCODE_DPAD_UP)
        press(dialog, KeyEvent.KEYCODE_BUTTON_A)
        assertEquals(listOf("quick"), done)
        assertEquals(1, closed)
        assertFalse(dialog.isShowing)
    }

    @Test
    fun bShutsItAndNothingIsDone() {
        val done = ArrayList<String>()
        var closed = 0
        // "Quit game?": Cancel is the row in hand, so A there quits nothing either.
        val dialog = ShelfMenu(activity).title("Quit game?").message("Unsaved progress will be lost")
            .row("Quit") { done += "quit" }
            .row("Cancel") { done += "cancel" }
            .onClosed { picked -> closed++; picked?.invoke() }
            .show(at = 1)
        picture(dialog, "menu-quit")
        press(dialog, KeyEvent.KEYCODE_BUTTON_B)
        assertTrue(done.isEmpty())
        assertEquals(1, closed)
        val again = ShelfMenu(activity).title("Quit game?").row("Quit") { done += "quit" }.row("Cancel") { done += "cancel" }.show(at = 1)
        press(again, KeyEvent.KEYCODE_BUTTON_A)
        assertEquals(listOf("cancel"), done)
    }

    @Test
    fun aSwitchRowStaysAndSaysItsValue() {
        var on = false
        val dialog = ShelfMenu(activity).title("Patches")
            .keep("Widescreen", { if (on) "On" else "Off" }) { on = !on }
            .row("Save") {}
            .show()
        press(dialog, KeyEvent.KEYCODE_BUTTON_A)
        assertTrue(on)
        assertTrue(dialog.isShowing)
        picture(dialog, "menu-patches")
        press(dialog, KeyEvent.KEYCODE_BUTTON_A)
        assertFalse(on)
    }

    @Test
    fun aThemesFileIsReadAsItsColours() {
        val paper = ShelfLook.read("paper", """{"ground":"#e8e3d6","raised":"#dcd6c6","ink":"#121212","muted":"#6d6a61","accent":"#1f3a8a","rule":"rgba(18,18,18,0.3)"}""")
        assertEquals(0xFFE8E3D6.toInt(), paper.ground)
        assertEquals(0xFF1F3A8A.toInt(), paper.accent)
        assertEquals(76, android.graphics.Color.alpha(paper.rule))
        assertTrue(paper.light)
        // A file that cannot be read is Kon.
        assertEquals(ShelfLook.KON.ground, ShelfLook.read("x", "not json").ground)
        assertFalse(ShelfLook.KON.light)
    }
}
