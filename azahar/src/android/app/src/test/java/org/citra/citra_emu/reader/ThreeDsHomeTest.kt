// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Color
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.RelativeLayout
import android.widget.TextView
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.citra.citra_emu.databinding.FragmentGamesBinding
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.reader.ui.ThreeDsHome
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * The 3DS-style menu of two screens, in the games screen's own layout with made-up games, on a
 * device with one display (the two screens one above the other): what each screen shows, that
 * a touch chooses and a second one starts, the controller's keys, and that the games screen's
 * own list is back when the look is switched off. The lower screen on a display of its own is
 * not something these tests have.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(qualifiers = "w768dp-h432dp-400dpi")
class ThreeDsHomeTest {
    private lateinit var activity: Activity
    private lateinit var screen: FragmentGamesBinding
    private lateinit var home: ThreeDsHome
    private val started = ArrayList<String>()
    private val detailed = ArrayList<String>()
    private val names = listOf(
        "作り話の冒険", "Made-up Kart", "ふたりの旅", "Nothing Hunter", "空の物語", "Puzzle of Nowhere",
        "夜の図書館", "Invented Tactics", "星の手紙", "Sample Quest", "風の町", "Another Story"
    )
    private val games = names.mapIndexed { index, name ->
        Game(title = name, path = "/made/up/$index.3ds", titleId = 0x0004000000100000L + index, company = "Nobody $index", filename = "$index.3ds")
    }

    @Before
    fun setUp() {
        activity = Robolectric.buildActivity(Activity::class.java).setup().get()
        activity.setTheme(com.google.android.material.R.style.Theme_Material3_DayNight_NoActionBar)
        screen = FragmentGamesBinding.inflate(LayoutInflater.from(activity))
        activity.setContentView(screen.root, ViewGroup.LayoutParams(WIDTH, HEIGHT))
        screen.gridGames.layoutManager = GridLayoutManager(activity, 1)
        home = ThreeDsHome(
            screen.gridGames.parent as RelativeLayout,
            screen.gridGames,
            ThreeDsHome.Host(
                loadIcon = { game, picture ->
                    val icon = Bitmap.createBitmap(48, 48, Bitmap.Config.ARGB_8888)
                    icon.eraseColor(Color.HSVToColor(floatArrayOf(games.indexOf(game) * 29f % 360f, 0.55f, 0.85f)))
                    picture.setImageBitmap(icon)
                },
                launch = { started += it.title },
                details = { detailed += it.title },
                readable = { it === games[2] }
            )
        )
        started.clear()
        detailed.clear()
    }

    private fun show(name: String) {
        // More than once: the tiles are sized from the grid's width once that is known.
        repeat(3) { TestSupport.snapshot(screen.root, WIDTH, HEIGHT, name) }
    }

    private fun <T : View> part(tag: String): T = screen.root.findViewWithTag(tag)
    private fun grid() = part<RecyclerView>(ThreeDsHome.TAG_GRID)
    private fun tiles(): List<View> = (0 until grid().childCount).map {
        grid().getChildAt(it).findViewWithTag<View>(ThreeDsHome.TAG_TILE)
    }
    private fun title() = part<TextView>(ThreeDsHome.TAG_TITLE).text.toString()
    private fun bubble() = part<TextView>(ThreeDsHome.TAG_BUBBLE)

    private fun on() {
        home.set(true)
        home.submit(games)
        show("games-3ds")
    }

    @Test
    fun thereAreTwoScreensOneAboveTheOther() {
        on()
        val upper = home.upperScreen!!
        val lower = home.lowerScreen!!
        assertFalse("one display: both are in the games screen's place", home.onTwoDisplays)
        assertTrue(upper.height > HEIGHT / 3)
        assertTrue(lower.height > upper.height)
        assertTrue("the upper one is above the lower one", upper.bottom <= lower.top)
        // The screen's own list is away.
        assertEquals(View.GONE, screen.gridGames.visibility)
        // The upper screen: the strip along its top, and no game chosen yet.
        assertTrue(part<View>(ThreeDsHome.TAG_STRIP).height > 0)
        assertEquals("Choose a game", title())
        assertEquals(View.INVISIBLE, bubble().visibility)
        // The lower screen: the games as square tiles, several to a row, all in view.
        val columns = (grid().layoutManager as GridLayoutManager).spanCount
        assertTrue("several to a row: $columns", columns >= 4)
        assertEquals(names.size, tiles().size)
        for (tile in tiles()) {
            assertEquals(tile.width, tile.height)
            assertTrue("a tile of a size to touch: ${tile.width}", tile.width >= 150)
        }
        assertTrue(grid().bottom <= part<View>(ThreeDsHome.TAG_OPEN).top)
    }

    @Test
    fun aTouchChoosesAndASecondOneStarts() {
        on()
        tiles()[2].performClick()
        show("games-3ds-chosen")
        assertTrue("nothing starts at the first touch", started.isEmpty())
        assertEquals(2, home.chosenIndex)
        // Both screens say which: the upper one with its maker and that it can be read.
        assertEquals(names[2], title())
        assertEquals(names[2], bubble().text.toString())
        assertEquals(View.VISIBLE, bubble().visibility)
        assertTrue(TestSupport.allText(home.upperScreen!!).contains("Nobody 2"))
        assertTrue(TestSupport.allText(home.upperScreen!!).contains("Text can be read"))

        tiles()[5].performClick()
        assertTrue(started.isEmpty())
        assertEquals(names[5], title())
        assertFalse(TestSupport.allText(home.upperScreen!!).contains("Text can be read"))

        tiles()[5].performClick()
        assertEquals(listOf(names[5]), started)
    }

    @Test
    fun theOpenButtonStartsTheChosenGame() {
        on()
        val open = part<TextView>(ThreeDsHome.TAG_OPEN)
        assertEquals("Open", open.text.toString())
        open.performClick()
        assertTrue("nothing is chosen, nothing starts", started.isEmpty())
        tiles()[4].performClick()
        show("games-3ds-open")
        assertTrue(open.width >= 500)
        assertTrue(open.bottom <= home.lowerScreen!!.height)
        open.performClick()
        assertEquals(listOf(names[4]), started)
    }

    @Test
    fun aControllerMovesTheChoiceAndStartsIt() {
        on()
        val columns = (grid().layoutManager as GridLayoutManager).spanCount
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_RIGHT))
        assertEquals("the first press chooses the first game", 0, home.chosenIndex)
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_RIGHT))
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_RIGHT))
        assertEquals(2, home.chosenIndex)
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_DOWN))
        assertEquals(2 + columns, home.chosenIndex)
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_UP))
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_LEFT))
        assertEquals(1, home.chosenIndex)
        // At the grid's edges the key is not this menu's: the focus may go to the app's own bar.
        assertTrue(home.key(KeyEvent.KEYCODE_DPAD_LEFT))
        assertFalse(home.key(KeyEvent.KEYCODE_DPAD_LEFT))
        assertFalse(home.key(KeyEvent.KEYCODE_DPAD_UP))
        assertEquals(0, home.chosenIndex)
        assertTrue(home.key(KeyEvent.KEYCODE_BUTTON_X))
        assertEquals(listOf(names[0]), detailed)
        assertTrue(home.key(KeyEvent.KEYCODE_BUTTON_A))
        assertEquals(listOf(names[0]), started)
    }

    @Test
    fun aLongTouchOpensTheGamesOwnPage() {
        on()
        tiles()[3].performLongClick()
        assertEquals(listOf(names[3]), detailed)
        assertTrue(started.isEmpty())
    }

    @Test
    fun theChoiceIsKeptWhenTheGamesAreListedAgain() {
        on()
        tiles()[1].performClick()
        home.submit(games.reversed())
        show("games-3ds-again")
        assertEquals(names[1], title())
        assertEquals(games.size - 2, home.chosenIndex)
        home.start()
        assertEquals(listOf(names[1]), started)
    }

    @Test
    fun withoutTheLookTheGamesScreenIsAsItWas() {
        show("games-plain")
        assertEquals(View.VISIBLE, screen.gridGames.visibility)
        on()
        home.set(false)
        show("games-plain-again")
        assertEquals(View.VISIBLE, screen.gridGames.visibility)
        assertNull(home.upperScreen)
        assertNull(screen.root.findViewWithTag<View>(ThreeDsHome.TAG_GRID))
        assertFalse(home.key(KeyEvent.KEYCODE_DPAD_RIGHT))
    }

    private companion object {
        const val WIDTH = 1920
        const val HEIGHT = 1080
    }
}
