// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader

import android.app.Activity
import android.content.Context
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import org.citra.citra_emu.reader.anki.SourceAddress
import org.citra.citra_emu.reader.cursor.HoverCursor
import org.citra.citra_emu.reader.model.ScreenLayout
import org.citra.citra_emu.reader.model.TextLine
import org.citra.citra_emu.reader.screen.Area
import org.citra.citra_emu.reader.screen.PlacedText
import org.citra.citra_emu.reader.screen.ScreenGeometry
import org.citra.citra_emu.reader.stats.LineAccounting
import org.citra.citra_emu.reader.ui.ReaderOverlay
import org.citra.citra_emu.reader.ui.ReaderSettingsActivity
import org.citra.citra_emu.reader.ui.ScreenTextLayer
import org.citra.citra_emu.reader.ui.ThreeDsHome
import org.junit.After
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

/**
 * What is this console's own ([Console], [ReaderGlue]): the 3DS's two screens as the emulator
 * draws them, the lower one being the game's touch screen and the cursor's trackpad; its
 * buttons; this app's bundled games and its own settings page. The other tests are shared by
 * every reader fork.
 */
@RunWith(RobolectricTestRunner::class)
@Config(qualifiers = "w834dp-h469dp-369dpi") // an AYN Thor's upper display: 1920x1080
class ConsoleTest {
    private val context: Context get() = ApplicationProvider.getApplicationContext()

    // What the emulator reports on the Thor with its own layout: the upper display has the top
    // screen large with the bottom screen small beside it, the lower display the bottom screen.
    private val thorUpper = intArrayOf(1920, 1080, 1, 1, 0, 115, 1416, 965, 1, 1416, 115, 1920, 493, 0, 0, 0, 0, 0, 0)
    private val thorLower = intArrayOf(1240, 1080, 1, 0, 0, 0, 0, 0, 1, 0, 75, 1240, 1005, 0, 0, 0, 0, 0, 0)

    @Before
    fun setUp() {
        TestSupport.resetStores()
        ReaderCatalog.forget()
        HoverCursor.reset()
    }

    @After
    fun tearDown() {
        HoverCursor.reset()
        ReaderCatalog.forget()
        ReaderSession.screen.clear()
        TestSupport.resetStores()
    }

    private fun line(text: String) = PresentedLine(
        TextLine(
            sequence = 1, frame = 1, audioPosition = 0, channel = "dialogue", speaker = "",
            text = text, ruby = emptyList(), sourceId = "made-up", meta = emptyMap(), timestamp = 1, window = "w"
        ),
        LineAccounting(text.length, false, true, 1),
        1
    )

    private fun PlacedText.offsetAt(x: Float, y: Float): Int? =
        (hit(x, y) as? PlacedText.Hit.OnCharacter)?.character?.offset

    // ---- The games ----

    @Test
    fun scriptsAreNamedAfterTheTitleIdsAndTheBundledGameIsCovered() {
        assertEquals("0004000000088A00", ReaderCatalog.titleIdOf("0004000000088A00.lua"))
        assertNull(ReaderCatalog.titleIdOf("HDR-0164.lua"))
        assertEquals(setOf("0004000000088A00"), ReaderCatalog.games(context).map { it.titleId }.toSet())
        assertTrue("however the ID is written", ReaderCatalog.covers(context, "0004000000088a00"))
        assertEquals("https://ec.nintendo.com/apps/0004000000088a00/JP", SourceAddress.of("0004000000088A00"))
        assertEquals("0004000000088A00", ReaderGlue.gameId(0x0004000000088A00L))
        assertEquals("no game", "", ReaderGlue.gameId(0L))
    }

    // ---- The two screens ----

    @Test
    fun theArrangementOfTheScreensIsReadFromTheEmulator() {
        val upper = ReaderGlue.geometry(thorUpper)!!
        assertEquals(listOf(Area(0f, 115f, 1416f, 965f)), upper.areasOf(ScreenLayout.SCREEN_GAME))
        assertEquals(listOf(Area(1416f, 115f, 1920f, 493f)), upper.areasOf(ScreenLayout.SCREEN_LOWER))
        assertEquals("the lower screen is the trackpad", upper.lower, upper.pad)
        assertFalse("it shows a screen of the game: no bare pad", upper.padOnly)

        val lower = ReaderGlue.geometry(thorLower)!!
        assertTrue(lower.areasOf(ScreenLayout.SCREEN_GAME).isEmpty())
        assertEquals(listOf(Area(0f, 75f, 1240f, 1005f)), lower.areasOf(ScreenLayout.SCREEN_LOWER))
        assertFalse(lower.padOnly)

        // A hybrid layout shows one screen twice.
        val hybrid = ReaderGlue.geometry(
            intArrayOf(2000, 900, 1, 1, 0, 0, 1500, 900, 1, 1500, 525, 2000, 900, 1, 0, 1500, 0, 2000, 300)
        )!!
        assertEquals(2, hybrid.areasOf(ScreenLayout.SCREEN_GAME).size)
        assertEquals(1, hybrid.areasOf(ScreenLayout.SCREEN_LOWER).size)

        // Nothing to map: no surface, a short answer, the upright orientation, an empty area.
        assertNull(ReaderGlue.geometry(null))
        assertNull(ReaderGlue.geometry(intArrayOf(1, 2, 3)))
        assertNull(ReaderGlue.geometry(thorUpper.copyOf().also { it[2] = 0 }))
        assertTrue(ReaderGlue.geometry(thorUpper.copyOf().also { it[6] = 0 })!!.areasOf(ScreenLayout.SCREEN_GAME).isEmpty())
    }

    @Test
    fun theLowerScreensTextIsPlacedWhereverThatScreenIsShown() {
        // A choice on the lower screen, as the engine reports it: "screen":1 in its 320 by 240.
        val choices = "はい\nいいえ"
        val layout = ScreenLayout.fromJson(
            """{"window":"w","seq":1,"screen":1,"space":[320,240],"glyphs":[[65,70,14,18],[79,70,14,18],null,""" +
                """[65,97,14,18],[79,97,14,18],[93,97,14,18]]}"""
        )!!
        val small = PlacedText.place(line(choices), layout, ReaderGlue.geometry(thorUpper)!!).single()
        val large = PlacedText.place(line(choices), layout, ReaderGlue.geometry(thorLower)!!).single()
        // い of いいえ (offset 3): 65 to 79 across, 97 to 115 down, in the lower screen's pixels.
        assertEquals(3, small.offsetAt(1416 + 72 * 1.575f, 115 + 106 * 1.575f))
        assertEquals(3, large.offsetAt(72 * 3.875f, 75 + 106 * 3.875f))
        assertEquals(5, large.offsetAt(100 * 3.875f, 75 + 106 * 3.875f))
        // Nothing reaches beyond the lower screen: beside it on the upper display is the top one.
        assertTrue(small.box.left >= 1416f)

        // The top screen's text (400 by 240) is not on the lower display at all.
        val top = ScreenLayout.fromJson("""{"window":"w","seq":1,"space":[400,240],"glyphs":[[40,191,16,21]]}""")!!
        assertTrue(PlacedText.place(line("あ"), top, ReaderGlue.geometry(thorLower)!!).isEmpty())
        assertEquals(1, PlacedText.place(line("あ"), top, ReaderGlue.geometry(thorUpper)!!).size)
    }

    // ---- The overlay over the two screens ----

    private lateinit var activity: Activity
    private lateinit var root: FrameLayout
    private var gameTouches = 0

    private val host = object : ReaderOverlay.Host {
        override fun openBacklog() {}
        override fun openStatistics() {}
        override fun openSettings() {}
        override fun openDictionaries() {}
        override fun openAnkiSetup() {}
        override fun chooseCardAudio() {}
        override fun hasSecondaryScreen() = false
    }

    private fun screens(): ReaderOverlay {
        HoverCursor.armed = { true }
        HoverCursor.holdSticks = {}
        activity = Robolectric.buildActivity(Activity::class.java).setup().get()
        activity.setTheme(com.google.android.material.R.style.Theme_Material3_DayNight_NoActionBar)
        root = FrameLayout(activity)
        val game = View(activity).apply {
            setOnTouchListener { _, event ->
                if (event.actionMasked == MotionEvent.ACTION_DOWN) gameTouches++
                true
            }
        }
        root.addView(game, FrameLayout.LayoutParams(-1, -1))
        val overlay = ReaderOverlay(activity, host, secondary = false).also { it.gameSurface = game }
        root.addView(overlay)
        activity.setContentView(root, ViewGroup.LayoutParams(1920, 1080))
        TestSupport.find(overlay, ScreenTextLayer::class.java)!!.geometrySource = { ReaderGlue.geometry(thorUpper) }
        layout()
        return overlay
    }

    private fun layout() {
        repeat(2) {
            TestSupport.idle()
            root.measure(
                View.MeasureSpec.makeMeasureSpec(1920, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY)
            )
            root.layout(0, 0, 1920, 1080)
        }
    }

    private fun tap(x: Float, y: Float) {
        val start = SystemClock.uptimeMillis()
        for ((action, at) in listOf(MotionEvent.ACTION_DOWN to start, MotionEvent.ACTION_UP to start + 40)) {
            val event = MotionEvent.obtain(start, at, action, x, y, 0)
            root.dispatchTouchEvent(event)
            event.recycle()
            TestSupport.idle()
        }
    }

    @Test
    fun onTheThorTheLowerDisplayIsTheTrackpadOnlyWhileTheButtonIsHeld() {
        val overlay = screens()
        // The lower display's layer (laid over the first here: only its own areas matter).
        val lower = ReaderOverlay(activity, host, secondary = true)
        root.addView(lower)
        TestSupport.find(lower, ScreenTextLayer::class.java)!!.geometrySource = { ReaderGlue.geometry(thorLower) }
        layout()
        HoverCursor.refresh()
        // The lower screen is the game's touch screen: never a trackpad all the time.
        assertFalse(lower.isSecondDisplay())
        assertFalse(HoverCursor.always)
        assertTrue(lower.trackpad.isEmpty())

        assertTrue("the button is the reader's", ReaderGlue.onButton(HoverCursor.BUTTON_DEFAULT, true, HoverCursor.SOURCE_KEY))
        assertFalse(HoverCursor.everywhere)
        assertEquals(listOf(Area(0f, 75f, 1240f, 1005f)), lower.trackpad)
        assertEquals("and the lower screen beside the top one", listOf(Area(1416f, 115f, 1920f, 493f)), overlay.trackpad)
        assertTrue(ReaderGlue.onButton(HoverCursor.BUTTON_DEFAULT, false, HoverCursor.SOURCE_KEY))
        assertTrue(lower.trackpad.isEmpty())
        assertEquals(0, gameTouches)
    }

    @Test
    fun aTouchOnTheLowerScreenBesideTheTextIsTheGamesAlsoWhenPlayingByTouch() {
        screens()
        val layer = TestSupport.find(root, ScreenTextLayer::class.java)!!
        // A line on the top screen, so that the reader's layer has text to read.
        val shown = line("今日は良い天気だ")
        val overlay = TestSupport.find(root, ReaderOverlay::class.java)!!
        overlay.onLine(shown)
        ReaderSession.screen.adoptLine(shown)
        ReaderSession.screen.applyLayout(
            ScreenLayout.fromJson(
                """{"window":"w","seq":1,"space":[400,240],"box":[0,179,400,61],"glyphs":[""" +
                    (0 until 8).joinToString(",") { "[${40 + 16 * it},191,16,21]" } + "]}"
            )!!
        )
        overlay.onScreenTextChanged()
        layout()
        layer.touchPlay = true
        layer.touchMode = { ReaderCatalog.TOUCH_PRESS }
        // A tap on the lower screen beside the small copy: the game's touch screen.
        tap(1800f, 450f)
        assertEquals(1, gameTouches)
        // A tap on the top screen goes on in the text, as in the other apps: not the game's.
        tap(700f, 400f)
        assertEquals(1, gameTouches)
    }

    // ---- The buttons ----

    @Test
    fun theStatisticsButtonIsTakenFromTheGameAndTheCursorsButtonWins() {
        var presses = 0
        ReaderGlue.reading = { true }
        ReaderGlue.onStatsButton = { presses++ }
        try {
            // None chosen: every button is the game's.
            assertFalse(ReaderGlue.onButton(708, true, HoverCursor.SOURCE_KEY))
            assertFalse(ReaderGlue.onButton(708, false, HoverCursor.SOURCE_KEY))
            ReaderInput.statsButton = 708
            assertTrue("the press is the reader's", ReaderGlue.onButton(708, true, HoverCursor.SOURCE_KEY))
            assertTrue("a repeat too", ReaderGlue.onButton(708, true, HoverCursor.SOURCE_KEY))
            assertTrue("and the release", ReaderGlue.onButton(708, false, HoverCursor.SOURCE_KEY))
            assertEquals("one press, one toggle", 1, presses)
            assertFalse("another button is the game's", ReaderGlue.onButton(773, true, HoverCursor.SOURCE_KEY))
            // While no game is read it is the game's.
            ReaderGlue.reading = { false }
            assertFalse(ReaderGlue.onButton(708, true, HoverCursor.SOURCE_KEY))
            assertFalse(ReaderGlue.onButton(708, false, HoverCursor.SOURCE_KEY))
            ReaderGlue.reading = { true }
            // Set to the cursor's own button, it is the cursor's.
            HoverCursor.armed = { true }
            HoverCursor.configure(708, 100, false)
            assertTrue(ReaderGlue.onButton(708, true, HoverCursor.SOURCE_KEY))
            assertTrue(HoverCursor.active)
            assertTrue(ReaderGlue.onButton(708, false, HoverCursor.SOURCE_KEY))
            assertEquals(1, presses)
        } finally {
            ReaderInput.statsButton = -1
            ReaderGlue.reading = { ReaderSession.isActive }
            ReaderGlue.onStatsButton = { ReaderSession.toggleStatsPaused() }
        }
    }

    @Test
    fun theButtonListsAreTheThreeDssAndNoneIsMinusOne() {
        val values = context.resources.getStringArray(org.citra.citra_emu.R.array.reader_cursor_button_values)
        assertEquals("-1", values.first())
        assertTrue(Console.CURSOR_BUTTON_DEFAULT.toString() in values)
        assertEquals(HoverCursor.BUTTON_DEFAULT, ReaderSettings(context).cursorButton)
        // Touch play's buttons are pressed by the emulator's numbers, which it knows.
        for (name in Console.TOUCH_ALL) assertTrue(name, Console.TOUCH_BUTTONS.containsKey(name))
        // A "none" kept from before (0) is -1 after the application's start.
        PreferenceManager.getDefaultSharedPreferences(context).edit()
            .putString(ReaderSettings.KEY_STATS_BUTTON, "0").commit()
        ReaderGlue.onApplication(context)
        assertEquals(-1, ReaderSettings(context).statsButton)
    }

    // ---- This app's own settings ----

    @Test
    fun theGameListsPageIsThisAppsOwnAndTheSecondDisplayIsTheEmulators() {
        val activity = Robolectric.buildActivity(ReaderSettingsActivity::class.java).setup().get()
        activity.open(ReaderSettingsActivity.Section.CONSOLE)
        TestSupport.idle()
        val page = activity.supportFragmentManager.fragments.last() as androidx.preference.PreferenceFragmentCompat
        val screen = page.preferenceScreen
        val keys = (0 until screen.preferenceCount).map { screen.getPreference(it).key }
        assertEquals(listOf(ThreeDsHome.KEY_HOME_3DS, ThreeDsHome.KEY_READABLE_ONLY), keys)
        assertTrue(ThreeDsHome.shown(context))
        assertTrue(ThreeDsHome.readableOnly(context))
        assertFalse("the emulator shows the lower screen on a second display", Console.SECOND_DISPLAY_FREE)
    }
}
