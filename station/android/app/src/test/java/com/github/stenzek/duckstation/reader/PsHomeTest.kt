// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader

import android.content.Context
import android.os.Looper
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import com.github.stenzek.duckstation.reader.ui.PsHome
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.time.Duration

/** The PlayStation-style home (ui/PsHome.kt): this app's own, the desktop home's twin. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class PsHomeTest {
    private val context: Context = ApplicationProvider.getApplicationContext()
    private val asked = ArrayList<Pair<String, String?>>()

    private val host = object : PsHome.Host {
        override fun games() = listOf(
            PsHome.Game("/games/kamaitachi.cue", "SLPS-01794", "Kamaitachi no Yoru", "NTSC-J", 548_839_200, null),
            PsHome.Game("/games/made-up.cue", Console.Test.UNKNOWN_GAME, "The Made-Up Game", "NTSC-J", 300_000_000, null),
        )

        override fun gamesReady() = true

        override fun perform(action: String, path: String?) {
            asked += action to path
        }
    }

    @Before
    fun clean() {
        PreferenceManager.getDefaultSharedPreferences(context).edit().clear().commit()
    }

    // The sheet is 640x480, here scaled 1.5 times into 1280x720 with 160 pixels at either side.
    private fun tap(home: PsHome, x: Float, y: Float) {
        val sx = 160 + x * 1.5f
        val sy = y * 1.5f
        val now = SystemClock.uptimeMillis()
        home.dispatchTouchEvent(MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, sx, sy, 0))
        home.dispatchTouchEvent(MotionEvent.obtain(now, now + 50, MotionEvent.ACTION_UP, sx, sy, 0))
    }

    private fun later(home: PsHome, name: String) {
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(3))
        TestSupport.snapshot(home, 1280, 720, name)
        draw(home)
    }

    /** A frame of the home, which is when it learns where its rows and buttons are. */
    private fun draw(home: PsHome) {
        home.draw(android.graphics.Canvas(android.graphics.Bitmap.createBitmap(1280, 720, android.graphics.Bitmap.Config.ARGB_8888)))
    }


    @Test
    fun theTabsAndAControllerGoThroughThePages() {
        val home = PsHome(context, host)
        later(home, "ps-home-games")
        // The tabs: 132 wide, 10 apart, centred; Reader is the third.
        val x0 = 320 - (4 * 132f + 3 * 10f) / 2
        tap(home, x0 + 2 * 142 + 66, 44f + 15)
        later(home, "ps-home-reader")
        tap(home, x0 + 3 * 142 + 66, 44f + 15)
        later(home, "ps-home-settings")
        // Settings: the first row, the emulator's settings, is chosen already: a touch opens it.
        // A touch on another row chooses it, a second one opens it.
        tap(home, 256f + 100, 86f + 10 + 20)
        assertEquals(listOf("settings" to null), asked)
        tap(home, 256f + 100, 86f + 10 + 40 + 20)
        assertEquals(1, asked.size)
        TestSupport.snapshot(home, 1280, 720, "ps-home-settings-second")
        draw(home)
        tap(home, 256f + 100, 86f + 10 + 40 + 20)
        assertEquals("controllers" to null, asked.last())

        // A controller: right to Memory Card (from Settings round to Games, then on), A to open.
        home.onKeyDown(KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_RIGHT))
        home.onKeyDown(KeyEvent.KEYCODE_BUTTON_R1, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_R1))
        later(home, "ps-home-cards")
        home.onKeyDown(KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_LEFT))
        home.onKeyDown(KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_DOWN))
        home.onKeyDown(KeyEvent.KEYCODE_BUTTON_A, KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BUTTON_A))
        assertEquals("start" to "/games/made-up.cue", asked.last())
    }

}
