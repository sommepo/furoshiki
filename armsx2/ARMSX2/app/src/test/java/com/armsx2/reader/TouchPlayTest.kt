// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

package com.armsx2.reader

import android.content.Context
import android.os.Looper
import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import java.time.Duration
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

/**
 * Touch play's side of the game (the taps themselves are in ScreenTextOverlayTest): which visual
 * novels say they are played by touch, the buttons pressed by the emulator's numbers, and an
 * answer picked with the d-pad and ○ against a made-up game whose cursor moves as a KID game's
 * does (on no answer until the d-pad is first pressed).
 */
@RunWith(RobolectricTestRunner::class)
class TouchPlayTest {
    private val context: Context get() = ApplicationProvider.getApplicationContext()

    /** A made-up game: a choice of [answers], the cursor where [at] says (-1: on none). */
    private class Game(
        val answers: Int,
        var at: Int = -1,
        val sideways: Boolean = false,
        val wraps: Boolean = false,
        /** A lesson over the choice: the d-pad does nothing until ○ has put it away. */
        var lesson: Boolean = false,
        /** Nothing the reader presses changes anything (a game that is not listening). */
        val frozen: Boolean = false
    ) {
        val presses = mutableListOf<Int>()
        var picked = -1
        var choiceUp = true
        private val down = mutableSetOf<Int>()

        fun cursor(): IntArray? = if (choiceUp) intArrayOf(at, if (sideways) 1 else 0) else null

        fun input(input: Int, pressed: Boolean) {
            if (!pressed) {
                down.remove(input)
                return
            }
            if (!down.add(input)) return
            presses.add(input)
            if (lesson) {
                if (input == Console.BUTTON_YES) lesson = false
                return
            }
            if (frozen) return
            val next = if (sideways) Console.BUTTON_RIGHT else Console.BUTTON_DOWN
            val back = if (sideways) Console.BUTTON_LEFT else Console.BUTTON_UP
            when (input) {
                // The first press of the d-pad puts the cursor on the first answer.
                next -> at = when {
                    at < 0 -> 0
                    at + 1 < answers -> at + 1
                    wraps -> 0
                    else -> at
                }
                back -> at = when {
                    at < 0 -> 0
                    at > 0 -> at - 1
                    wraps -> answers - 1
                    else -> at
                }
                Console.BUTTON_YES -> if (at >= 0) {
                    picked = at
                    choiceUp = false
                }
            }
        }
    }

    private val padBefore = ReaderSession.padInput
    private val cursorBefore = ReaderSession.answerCursor

    @Before
    fun setUp() = ReaderCatalog.forget()

    @After
    fun tearDown() {
        ReaderSession.padInput = padBefore
        ReaderSession.answerCursor = cursorBefore
        ReaderCatalog.forget()
    }

    private fun play(game: Game) {
        ReaderSession.padInput = game::input
        ReaderSession.answerCursor = game::cursor
    }

    private fun runFor(millis: Long) = shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(millis))

    @Test
    fun anAnswerIsPickedWithTheDpadOnePressAtATimeThenCircle() {
        val game = Game(answers = 4)
        play(game)
        assertTrue(ReaderSession.pickAnswer(2))
        runFor(5000)
        assertEquals(2, game.picked)
        val d = Console.BUTTON_DOWN
        assertEquals(listOf(d, d, d, Console.BUTTON_YES), game.presses)
    }

    @Test
    fun theCursorIsMovedBackAndSidewaysAsTheProfileSays() {
        val up = Game(answers = 3, at = 2)
        play(up)
        assertTrue(ReaderSession.pickAnswer(0))
        runFor(5000)
        assertEquals(0, up.picked)
        assertEquals(listOf(Console.BUTTON_UP, Console.BUTTON_UP, Console.BUTTON_YES), up.presses)

        val sideways = Game(answers = 2, at = 0, sideways = true)
        play(sideways)
        assertTrue(ReaderSession.pickAnswer(1))
        runFor(5000)
        assertEquals(1, sideways.picked)
        assertEquals(listOf(Console.BUTTON_RIGHT, Console.BUTTON_YES), sideways.presses)

        // Already there: ○ alone (CLANNAD's cursor starts on the first answer).
        val there = Game(answers = 2, at = 0)
        play(there)
        assertTrue(ReaderSession.pickAnswer(0))
        runFor(2000)
        assertEquals(listOf(Console.BUTTON_YES), there.presses)
    }

    @Test
    fun nothingIsPressedWhenTheProfileDoesNotSayWhereTheCursorIsAndAPickStopsWithTheChoice() {
        val game = Game(answers = 3)
        game.choiceUp = false
        play(game)
        assertFalse(ReaderSession.pickAnswer(1))
        runFor(2000)
        assertTrue(game.presses.isEmpty())

        // The choice goes away half way (the player pressed something themselves): no ○.
        val going = Game(answers = 4)
        play(going)
        assertTrue(ReaderSession.pickAnswer(3))
        runFor(300)
        going.choiceUp = false
        runFor(5000)
        assertFalse(Console.BUTTON_YES in going.presses)
        assertEquals(-1, going.picked)

    }

    @Test
    fun aLessonOverTheChoiceIsPutAwayWithCircleWhenTheDpadMovesNothing() {
        val game = Game(answers = 3, lesson = true)
        play(game)
        assertTrue(ReaderSession.pickAnswer(1))
        runFor(10_000)
        assertEquals(1, game.picked)
        val d = Console.BUTTON_DOWN
        val o = Console.BUTTON_YES
        // Two presses that moved nothing, then the first ○ for the lesson.
        assertEquals(listOf(d, d, o, d, d, o), game.presses)

        // A game that takes nothing is given up on after 48 presses.
        val frozen = Game(answers = 2, at = 0, frozen = true)
        play(frozen)
        assertTrue(ReaderSession.pickAnswer(1))
        runFor(60_000)
        assertEquals(48, frozen.presses.size)
    }

    @Test
    fun goingOnPressesTheAdvanceButtonByTheEmulatorsNumbers() {
        val game = Game(answers = 2)
        play(game)
        ReaderSession.pressAdvance()
        runFor(1000)
        assertEquals(listOf(Console.BUTTON_YES), game.presses)
        assertEquals(5, Console.BUTTON_YES)
        assertEquals(listOf(0, 1, 2, 3), listOf(Console.BUTTON_UP, Console.BUTTON_RIGHT, Console.BUTTON_DOWN, Console.BUTTON_LEFT))
    }

    @Test
    fun anAdvanceButtonKeptByThePspsNumbersBecomesThePs2s() {
        val preferences = PreferenceManager.getDefaultSharedPreferences(context)
        for ((old, now) in listOf("13" to 5, "14" to 6, "9" to 12, "6" to 6, "12" to 12)) {
            preferences.edit().putString(ReaderSettings.KEY_ADVANCE_BUTTON, old).commit()
            assertEquals(old, now, ReaderSettings(context).advanceButton)
            assertEquals("kept as the new number", now.toString(), preferences.getString(ReaderSettings.KEY_ADVANCE_BUTTON, null))
        }
        preferences.edit().remove(ReaderSettings.KEY_ADVANCE_BUTTON).commit()
        assertEquals(Console.BUTTON_YES, ReaderSettings(context).advanceButton)
        assertTrue("on unless switched off", ReaderSettings(context).touchPlay)
    }

    @Test
    fun theVisualNovelsSayTheyArePlayedByPressesAndTheOtherGamesSayNothing() {
        val novels = listOf(
            "SLPS-25256", // Never7
            "SLPM-65421", // Ever17
            "SLPM-65550", // Remember11
            "SLPM-66302", // CLANNAD
            "SLPS-25918", // アマガミ
            "SLPM-66913", // ひぐらしのなく頃に祭
            "SLPM-65320", // EVE burst error PLUS
            "SLPS-20121", // Kanon
        )
        val games = ReaderCatalog.games(context).associateBy { it.titleId }
        for (id in novels) {
            assertEquals(id, ReaderCatalog.TOUCH_PRESS, games.getValue(id).touch)
        }
        for ((id, game) in games) {
            if (id !in novels) assertEquals("$id is not played by touch", "", game.touch)
        }
        assertEquals(ReaderCatalog.TOUCH_PRESS, ReaderCatalog.touch(context, "slps-25256"))
        assertEquals("", ReaderCatalog.touch(context, "SLPM-65555")) // ドラゴンクエストV
        val said = ReaderCatalog.describe("SLPM-69999", "-- reader: title X\n-- reader: touch Press\n", own = true)
        assertEquals(ReaderCatalog.TOUCH_PRESS, said.touch)
        assertEquals("", ReaderCatalog.describe("SLPM-69999", "-- reader: touch sideways\n", own = true).touch)
    }

    @Test
    fun aGamesMenuIsPickedLikeAChoiceButIsNotRead() {
        val menu = com.armsx2.reader.model.TextLine.fromJson(
            """{"text":"見る・調べる\n移動する\n話す","channel":"menu","window":"menu","id":"menu:1"}""", 0L
        )!!
        assertTrue(menu.isChoice) // a tap on an entry picks it
        assertTrue(menu.isMenu)   // and it stays out of the statistics, the backlog and the panel
        val choice = com.armsx2.reader.model.TextLine.fromJson("""{"text":"はい\nいいえ","channel":"choice"}""", 0L)!!
        assertTrue(choice.isChoice)
        assertFalse(choice.isMenu)
    }
}
