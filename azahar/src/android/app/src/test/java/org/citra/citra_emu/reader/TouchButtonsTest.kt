// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader

import android.content.Context
import android.graphics.Color
import android.view.View
import android.widget.FrameLayout
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.citra.citra_emu.reader.ui.TouchButtons
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * The buttons of a game played by touch, laid out as on the console's controller (the layout
 * test of the shared TouchButtonsTest of the other reader forks; this console's games name no
 * buttons of their own yet).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class TouchButtonsTest {
    private val context: Context = ApplicationProvider.getApplicationContext()

    @Test
    fun everyButtonAndATabThatTucksThemAwayForGood() {
        androidx.preference.PreferenceManager.getDefaultSharedPreferences(context).edit()
            .remove(TouchButtons.KEY_TUCKED).commit()
        val buttons = TouchButtons(context) { _, _ -> }
        buttons.show(TouchButtons.ALL)
        assertEquals(TouchButtons.ALL, buttons.names)
        assertTrue(buttons.buttonsShown)
        val root = FrameLayout(context).apply { setBackgroundColor(Color.rgb(40, 70, 110)) }
        root.addView(
            buttons,
            FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        )
        TestSupport.snapshot(root, 1280, 720, "touch-buttons-all")

        // As on the controller: the d-pad a cross at the left edge, the face buttons the
        // console's diamond at the right, START under them; the middle is the game's.
        fun centre(name: String): Pair<Float, Float> {
            val view = buttons.findViewWithTag<View>(name)
            var x = view.width / 2f
            var y = view.height / 2f
            var step: View = view
            while (step !== root) {
                x += step.left
                y += step.top
                step = step.parent as View
            }
            return x to y
        }
        val (up, right, down, left) = listOf("up", "right", "down", "left").map(::centre)
        for (way in listOf(up, right, down, left)) assertTrue(way.first < 1280 / 4f)
        assertTrue(up.second < left.second && left.second < down.second)
        assertTrue(left.first < up.first && up.first < right.first)
        assertEquals(up.first, down.first, 1f)
        assertEquals(left.second, right.second, 1f)
        val face = Console.TOUCH_FACE.map(::centre)
        for (button in face) assertTrue(button.first > 1280 * 3 / 4f)
        val (top, east, bottom, west) = face
        assertTrue(top.second < west.second && west.second < bottom.second)
        assertTrue(west.first < top.first && top.first < east.first)
        for (name in TouchButtons.ALL - Console.TOUCH_FACE.toSet() - Console.TOUCH_LEFT) {
            assertTrue(centre(name).first > 1280 / 2f)
        }
        assertTrue(!buttons.isClickable) // a touch between the two sides goes on to the game

        buttons.findViewWithTag<View>(TouchButtons.TAB).performClick()
        assertTrue(!buttons.buttonsShown) // either side's tab tucks both away
        assertEquals(View.VISIBLE, buttons.visibility) // the tabs stay, to bring them back
        assertEquals(View.VISIBLE, buttons.findViewWithTag<View>(TouchButtons.TAB_LEFT).visibility)
        TestSupport.snapshot(root, 1280, 720, "touch-buttons-tucked")
        buttons.findViewWithTag<View>(TouchButtons.TAB_LEFT).performClick()
        assertTrue(buttons.buttonsShown)
        buttons.findViewWithTag<View>(TouchButtons.TAB_LEFT).performClick()
        assertTrue(!buttons.buttonsShown)
        // Kept: the next game starts with them tucked away.
        val next = TouchButtons(context) { _, _ -> }
        next.show(TouchButtons.ALL)
        assertTrue(!next.buttonsShown)
        next.findViewWithTag<View>(TouchButtons.TAB).performClick()
        assertTrue(next.buttonsShown)
        assertTrue(!TouchButtons(context) { _, _ -> }.tucked)
    }
}
