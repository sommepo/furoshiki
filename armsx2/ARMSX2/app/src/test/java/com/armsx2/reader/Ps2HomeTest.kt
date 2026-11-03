// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

package com.armsx2.reader

import android.graphics.Bitmap
import android.view.KeyEvent
import android.graphics.Canvas
import androidx.activity.ComponentActivity
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import com.armsx2.reader.ui.Ps2Home
import java.io.File
import java.io.FileOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * The PS2-style menu, drawn off the device with made-up games: its three screens, what a
 * controller's keys and a touch do, and what starting a game hands back. With
 * READER_PREVIEW_DIR set, each screen is written there as a picture to look at.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34], qualifiers = "w960dp-h540dp-land-mdpi")
class Ps2HomeTest {
    @get:Rule
    val compose = createAndroidComposeRule<ComponentActivity>()

    private val games = (0 until 14).map { index ->
        val names = listOf("夏の夜の物語", "Made-up Adventure", "作り話 -another story-", "Sample Quest II", "ひぐらしの例", "Example Racing")
        Ps2Home.Game(
            key = "game-$index",
            name = names[index % names.size] + if (index >= names.size) " $index" else "",
            serial = "SLPM-${60000 + index * 37}",
            cover = null,
            readable = index % 2 == 0,
        )
    }
    private val launched = ArrayList<String>()
    private val chosenSettings = ArrayList<String>()
    private val settings = listOf("Reading Statistics", "Supported Games", "Dictionaries", "Anki", "Reader Settings",
        "Emulator Settings", "Memory Cards", "Game List").map { name ->
        Ps2Home.Setting(name.lowercase().replace(' ', '-'), name, value = { if (name == "Supported Games") "0" else "" }) {
            chosenSettings.add(name)
        }
    }

    @Before
    fun start() {
        Ps2Home.reset()
        // The menu's lights never rest: time moves only as far as a test moves it.
        compose.mainClock.autoAdvance = false
        compose.setContent {
            Ps2Home.Screen(games = games, settings = settings, onLaunch = { launched.add(it.key) }, onAddGames = {})
        }
        compose.mainClock.advanceTimeBy(100)
    }

    private fun key(code: Int) {
        Ps2Home.onKey(code, games, settings, columns = 5, onLaunch = { launched.add(it.key) }, onAddGames = {}, onGameOptions = {})
        compose.mainClock.advanceTimeBy(600)
    }

    private fun picture(name: String) {
        val folder = System.getProperty("reader.preview.dir").orEmpty()
        if (folder.isEmpty()) return
        compose.mainClock.advanceTimeBy(700)
        // The window's own views draw themselves into a picture: no screen is asked for one.
        val view = compose.activity.window.decorView
        val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
        compose.runOnUiThread { view.draw(Canvas(bitmap)) }
        File(folder).mkdirs()
        FileOutputStream(File(folder, "$name.png")).use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    @Test
    fun theMainMenuLeadsToTheBrowserAndToSystemConfiguration() {
        assertTrue(Ps2Home.describe().startsWith("screen=Menu menu=0"))
        picture("ps2-menu")
        key(KeyEvent.KEYCODE_DPAD_DOWN)
        assertTrue(Ps2Home.describe().startsWith("screen=Menu menu=1"))
        key(KeyEvent.KEYCODE_BUTTON_A)
        assertTrue(Ps2Home.describe().startsWith("screen=Configuration"))
        key(KeyEvent.KEYCODE_DPAD_DOWN)
        key(KeyEvent.KEYCODE_DPAD_DOWN)
        picture("ps2-configuration")
        key(KeyEvent.KEYCODE_BUTTON_A)
        assertEquals(listOf("Dictionaries"), chosenSettings)
        key(KeyEvent.KEYCODE_BUTTON_B)
        assertTrue(Ps2Home.describe().startsWith("screen=Menu"))
        key(KeyEvent.KEYCODE_DPAD_UP)
        key(KeyEvent.KEYCODE_BUTTON_A)
        assertTrue(Ps2Home.describe().startsWith("screen=Browser"))
    }

    @Test
    fun theBrowserChoosesWithTheKeysAndStartsWhatIsChosen() {
        key(KeyEvent.KEYCODE_BUTTON_A)
        key(KeyEvent.KEYCODE_DPAD_RIGHT)
        key(KeyEvent.KEYCODE_DPAD_RIGHT)
        key(KeyEvent.KEYCODE_DPAD_DOWN)
        assertTrue(Ps2Home.describe(), Ps2Home.describe().contains("game=7"))
        picture("ps2-browser")
        // The row's end holds; the last, short row is reached from above.
        key(KeyEvent.KEYCODE_DPAD_DOWN)
        key(KeyEvent.KEYCODE_DPAD_DOWN)
        assertTrue(Ps2Home.describe(), Ps2Home.describe().contains("game=12"))
        key(KeyEvent.KEYCODE_BUTTON_A)
        assertEquals(listOf("game-12"), launched)
        // Back on the main menu the system's Back is not the menu's.
        key(KeyEvent.KEYCODE_BUTTON_B)
        assertEquals(false, Ps2Home.onKey(KeyEvent.KEYCODE_BACK, games, settings, 5, {}, {}, {}))
    }

    @Test
    fun aTouchChoosesAGameAndASecondOneStartsIt() {
        compose.onNodeWithTag("ps2-menu-0").performClick()
        compose.mainClock.advanceTimeBy(600)
        compose.onNodeWithTag("ps2-game-1").performClick()
        compose.mainClock.advanceTimeBy(100)
        assertTrue(launched.isEmpty())
        assertTrue(Ps2Home.describe().contains("game=1"))
        compose.onNodeWithTag("ps2-game-1").performClick()
        compose.mainClock.advanceTimeBy(100)
        assertEquals(listOf("game-1"), launched)
    }
}
