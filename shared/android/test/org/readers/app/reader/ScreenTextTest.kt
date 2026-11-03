// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.readers.app.reader.model.ScreenLayout
import org.readers.app.reader.model.TextLine
import org.readers.app.reader.screen.Area
import org.readers.app.reader.screen.PlacedText
import org.readers.app.reader.screen.ScreenGeometry
import org.readers.app.reader.screen.ScreenTexts
import org.readers.app.reader.stats.LineAccounting
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Text in the game's own picture: reading what the engine reports, working out where it is on
 * a display, and deciding what a finger is on. No Android classes involved.
 */
class ScreenTextTest {
    private fun line(text: String, sequence: Long = 1, window: String = "w") = TextLine(
        sequence = sequence, frame = 1, audioPosition = 0, channel = "dialogue", speaker = "",
        text = text, ruby = emptyList(), sourceId = "id$sequence", meta = emptyMap(), timestamp = 1,
        window = window
    )

    private fun presented(line: TextLine, backlogId: Long = line.sequence) =
        PresentedLine(line, LineAccounting(1, false, true, 1), backlogId)

    /**
     * Rows of characters in cells 16 wide and 21 high, 26 apart, in a text box across the foot
     * of the picture. The made-up game of these tests measures its picture as 400 by 240.
     */
    private fun layout(
        text: String,
        sequence: Long = 1,
        box: String? = "[0,179,400,65]"
    ): ScreenLayout {
        val cells = StringBuilder()
        var x = 40
        var y = 191
        var i = 0
        while (i < text.length) {
            if (cells.isNotEmpty()) cells.append(',')
            if (text[i] == '\n') {
                cells.append("null")
                x = 40
                y += 26
            } else {
                cells.append("[$x,$y,16,21]")
                x += 16
            }
            i += Character.charCount(text.codePointAt(i))
        }
        val boxPart = if (box == null) "" else ""","box":$box"""
        return ScreenLayout.fromJson(
            """{"window":"w","seq":$sequence,"space":[400,240]$boxPart,"glyphs":[$cells]}"""
        )!!
    }

    // ---- What the engine sends ----

    @Test
    fun layoutsAreReadAsTheEngineWritesThem() {
        val shown = ScreenLayout.fromJson(
            """{"window":"talk","seq":6,"space":[1920,1080],"box":[0,58,320,90],""" +
                """"glyphs":[[65,70,14,18],null,[65.5,97.25,14,18]]}"""
        )!!
        assertEquals("talk", shown.window)
        assertEquals(6L, shown.sequence)
        assertFalse(shown.closed)
        assertEquals(1920f, shown.spaceWidth)
        assertEquals(1080f, shown.spaceHeight)
        assertEquals(ScreenLayout.Cell(0f, 58f, 320f, 90f), shown.box)
        assertEquals(
            listOf(ScreenLayout.Cell(65f, 70f, 14f, 18f), null, ScreenLayout.Cell(65.5f, 97.25f, 14f, 18f)),
            shown.cells
        )

        val closed = ScreenLayout.fromJson("""{"window":"talk","seq":6,"closed":true}""")!!
        assertTrue(closed.closed)
        assertTrue(closed.cells.isEmpty())

        // No box is fine, and a layout that does not say what it measures in means 1280 by 720.
        val plain = ScreenLayout.fromJson("""{"window":"w","seq":1,"glyphs":[]}""")!!
        assertNull(plain.box)
        assertEquals(1280f, plain.spaceWidth)
        assertEquals(720f, plain.spaceHeight)
        // A picture of no size is not believed.
        val flat = ScreenLayout.fromJson("""{"window":"w","seq":1,"space":[0,-5],"glyphs":[]}""")!!
        assertEquals(1280f, flat.spaceWidth)
        assertEquals(720f, flat.spaceHeight)
        // Nonsense is not a layout.
        assertNull(ScreenLayout.fromJson("not json"))
        assertNull(ScreenLayout.fromJson("""{"seq":1,"glyphs":[]}"""))
        assertNull(ScreenLayout.fromJson("""{"window":"w","seq":1}"""))
        // A broken cell is no cell, not a crash.
        val odd = ScreenLayout.fromJson("""{"window":"w","seq":1,"glyphs":[[1,2,3],"x",[1,2,-3,4],[1,2,3,4]]}""")!!
        assertEquals(listOf(null, null, null, ScreenLayout.Cell(1f, 2f, 3f, 4f)), odd.cells)
    }

    @Test
    fun aLineRemembersItsWindow() {
        val parsed = TextLine.fromJson("""{"seq":3,"text":"あ","id":"x","meta":{},"window":"w7"}""", 5)!!
        assertEquals("w7", parsed.window)
        assertEquals("w7", parsed.toJson().getString("window"))
        val without = TextLine.fromJson("""{"seq":3,"text":"あ","id":"x","meta":{}}""", 5)!!
        assertEquals("", without.window)
        assertFalse(without.toJson().has("window"))
    }

    // ---- Where the picture is ----

    @Test
    fun theGamesPictureIsReadFromTheEmulator() {
        // A display the picture fills, and one where it has bars above and below.
        val filled = ScreenGeometry.fromPicture(intArrayOf(1920, 1080, 0, 0, 1920, 1080))!!
        assertEquals(listOf(Area(0f, 0f, 1920f, 1080f)), filled.areasOf(ScreenLayout.SCREEN_GAME))
        assertTrue(filled.areasOf(ScreenLayout.SCREEN_PAD).isEmpty())
        val barred = ScreenGeometry.fromPicture(intArrayOf(2000, 1200, 0, 37, 2000, 1162))!!
        assertEquals(listOf(Area(0f, 37f, 2000f, 1162f)), barred.areasOf(ScreenLayout.SCREEN_GAME))

        // The second display of a device with two is all trackpad, and shows no picture.
        val pad = ScreenGeometry.padOnly(1240, 1080)!!
        assertTrue(pad.areasOf(ScreenLayout.SCREEN_GAME).isEmpty())
        assertEquals(listOf(Area(0f, 0f, 1240f, 1080f)), pad.areasOf(ScreenLayout.SCREEN_PAD))

        // Nothing to map: no game running, a short answer, a picture of no size, no view yet.
        assertNull(ScreenGeometry.fromPicture(null))
        assertNull(ScreenGeometry.fromPicture(intArrayOf(1, 2, 3)))
        assertNull(ScreenGeometry.fromPicture(intArrayOf(1920, 1080, 0, 0, 0, 0)))
        assertNull(ScreenGeometry.padOnly(0, 0))

        // The game's units scale with the area its picture is drawn in. A game that measures
        // its picture as 1280 by 720, drawn 1920 by 1080: one and a half times.
        val cell = ScreenLayout.Cell(333.33f, 561.98f, 32f, 32.5f)
        val hd = ScreenLayout("w", 1, false, 1280f, 720f, null, listOf(cell))
        assertArea(
            Area(500f, 842.97f, 548f, 891.72f),
            ScreenGeometry.place(cell, hd, Area(0f, 0f, 1920f, 1080f))
        )
        // The same picture with bars: 2000 / 1280 across, 1125 / 720 down, 37 from the top.
        assertArea(
            Area(520.83f, 37f + 561.98f * 1.5625f, 570.83f, 37f + 594.48f * 1.5625f),
            ScreenGeometry.place(cell, hd, Area(0f, 37f, 2000f, 1162f))
        )
    }

    private fun assertArea(expected: Area, actual: Area) {
        assertEquals(expected.left, actual.left, 0.01f)
        assertEquals(expected.top, actual.top, 0.01f)
        assertEquals(expected.right, actual.right, 0.01f)
        assertEquals(expected.bottom, actual.bottom, 0.01f)
    }

    // ---- What a finger is on ----

    private val text = "今日は良い天気だ\n散歩に行こう"

    /** The text placed in a picture drawn four times its size from the surface's origin. */
    private fun placedAtFour(
        layout: ScreenLayout = layout(text),
        geometry: ScreenGeometry = ScreenGeometry(1600, 2000, listOf(Area(0f, 0f, 1600f, 960f)), emptyList())
    ) = PlacedText.place(presented(line(text)), layout, geometry)

    private fun PlacedText.offsetAt(x: Float, y: Float): Int? =
        (hit(x, y) as? PlacedText.Hit.OnCharacter)?.character?.offset

    @Test
    fun everyCharacterIsHitWhereItIsDrawn() {
        val placed = placedAtFour().single()
        assertEquals(14, placed.characters.size) // 15 code points, one of them the line break

        // First row at y 191 to 212, second at 217 to 238, cells 16 wide from x 40: times four.
        for (i in 0 until 8) {
            assertEquals(i, placed.offsetAt((40 + 16 * i + 8) * 4f, 201f * 4))
        }
        for (i in 0 until 6) {
            assertEquals(9 + i, placed.offsetAt((40 + 16 * i + 8) * 4f, 227f * 4))
        }
        // The very edges of a cell belong to it, and to the next one from there on.
        assertEquals(5, placed.offsetAt(120f * 4, 201f * 4))
        assertEquals(5, placed.offsetAt(136f * 4 - 1, 201f * 4))
        assertEquals(6, placed.offsetAt(136f * 4, 201f * 4))
    }

    @Test
    fun thereIsRoomAroundTheCharactersForAFinger() {
        val placed = placedAtFour().single()
        // Above the first row, where the game puts furigana.
        assertEquals(0, placed.offsetAt(48f * 4, 183f * 4))
        // The gap between the rows belongs to the lower one, for the same reason.
        assertEquals(9, placed.offsetAt(48f * 4, 214f * 4))
        assertEquals(0, placed.offsetAt(48f * 4, 211f * 4))
        // A little left of a row's first and right of its last character.
        assertEquals(0, placed.offsetAt(34f * 4, 201f * 4))
        assertEquals(7, placed.offsetAt(172f * 4, 201f * 4))
        assertEquals(14, placed.offsetAt(140f * 4, 227f * 4))
        // Just below the last row, as far as the picture goes (240 units down).
        assertEquals(9, placed.offsetAt(48f * 4, 239.5f * 4))
    }

    @Test
    fun theTextBoxTakesWhatIsNotACharacterAndNothingElseDoes() {
        val placed = placedAtFour().single()
        // Inside the box, away from the characters: the box, not the game.
        assertTrue(placed.hit(300f * 4, 201f * 4) is PlacedText.Hit.Box)
        assertTrue(placed.hit(4f, 181f * 4) is PlacedText.Hit.Box)
        // Above the box: the game.
        assertSame(PlacedText.Hit.Outside, placed.hit(300f * 4, 100f * 4))
        assertSame(PlacedText.Hit.Outside, placed.hit(48f * 4, 170f * 4))

        // Without a box from the game, only the characters and their margins are protected.
        val bare = placedAtFour(layout(text, box = null)).single()
        assertEquals(0, bare.offsetAt(48f * 4, 201f * 4))
        assertSame(PlacedText.Hit.Outside, bare.hit(300f * 4, 201f * 4))
        assertTrue(bare.box.contains(48f * 4, 201f * 4))
    }

    @Test
    fun nothingReachesBeyondThePicture() {
        // The picture ends above the foot of the surface. The box the game reports runs past
        // the picture's lower edge (179 + 65 > 240) and the last row's margin would too.
        val geometry = ScreenGeometry(
            1600, 2000,
            listOf(Area(0f, 0f, 1600f, 960f)),
            listOf(Area(160f, 960f, 1440f, 1920f))
        )
        val placed = placedAtFour(geometry = geometry).single()
        assertEquals(960f, placed.box.bottom)
        assertTrue(placed.characters.all { it.touch.bottom <= 960f })
        assertSame(PlacedText.Hit.Outside, placed.hit(48f * 4, 961f))
        assertEquals(9, placed.offsetAt(48f * 4, 959f))
    }

    @Test
    fun aWordIsMarkedRowByRow() {
        val placed = placedAtFour().single()
        // 天気 (offsets 5 and 6) on the first row.
        assertEquals(listOf(Area(120f * 4, 191f * 4, 152f * 4, 212f * 4)), placed.marks(5..6))
        // From だ over the line break to 散歩: one mark per row.
        assertEquals(
            listOf(Area(152f * 4, 191f * 4, 168f * 4, 212f * 4), Area(40f * 4, 217f * 4, 72f * 4, 238f * 4)),
            placed.marks(7..10)
        )
        assertTrue(placed.marks(8..8).isEmpty()) // the line break itself
    }

    @Test
    fun textIsOnTheGamesDisplayAndNeverOnTheTrackpad() {
        val placed = PlacedText.place(
            presented(line(text)),
            layout(text),
            ScreenGeometry.fromPicture(intArrayOf(1920, 1080, 0, 0, 1920, 1080))!!
        ).single()
        // 天 (offset 5): 120 to 136 across, 191 to 212 down, of 400 by 240, drawn 4.8 and 4.5 times.
        assertEquals(5, placed.offsetAt(128 * 4.8f, 201 * 4.5f))
        assertTrue(PlacedText.place(presented(line(text)), layout(text), ScreenGeometry.padOnly(1240, 1080)!!).isEmpty())
    }

    @Test
    fun textOnAConsolesLowerScreenIsPlacedWhereverThatScreenIsShown() {
        // The engine says "screen":1 for the lower screen of a console with two (the 3DS); a
        // layout that says nothing is on the game's picture.
        assertEquals(ScreenLayout.SCREEN_GAME, layout(text).screen)
        val lower = ScreenLayout.fromJson(
            """{"window":"w","seq":1,"screen":1,"space":[320,240],"glyphs":[[40,191,16,21]]}"""
        )!!
        assertEquals(ScreenLayout.SCREEN_LOWER, lower.screen)
        // The upper screen above, the lower one below it: the lower one is also the trackpad.
        val upper = Area(0f, 0f, 800f, 480f)
        val below = Area(80f, 480f, 720f, 960f)
        val stacked = ScreenGeometry(800, 960, listOf(upper), listOf(below), listOf(below))
        val placed = PlacedText.place(presented(line("あ")), lower, stacked).single()
        // あ at 40 to 56 of 320 across, drawn twice as large below the upper screen.
        assertEquals(0, placed.offsetAt(80f + 48f * 2f, 480f + 201f * 2f))
        assertTrue(PlacedText.place(presented(line("あ")), lower, ScreenGeometry(800, 480, listOf(upper), emptyList())).isEmpty())
        // The upper screen's text is not placed on the lower one.
        assertEquals(1, PlacedText.place(presented(line(text)), layout(text), stacked).size)
        // A trackpad that shows a screen of the console is not a bare pad: it is only dimmed.
        assertFalse(stacked.padOnly)
        assertFalse(ScreenGeometry(640, 480, emptyList(), listOf(below), listOf(below)).padOnly)
        assertTrue(ScreenGeometry.padOnly(1240, 1080)!!.padOnly)
    }

    @Test
    fun charactersNotDrawnYetAndOddOnesAreHandled() {
        // Only the first three characters have been typed out so far.
        val typing = ScreenLayout.fromJson(
            """{"window":"w","seq":1,"space":[400,240],"glyphs":[[40,191,16,21],[56,191,16,21],[72,191,16,21]]}"""
        )!!
        val placed = placedAtFour(typing).single()
        assertEquals(3, placed.characters.size)
        assertSame(PlacedText.Hit.Outside, placed.hit(96f * 4 + 60, 201f * 4))
        // Nothing drawn at all: nothing to tap.
        val nothing = ScreenLayout.fromJson("""{"window":"w","seq":1,"glyphs":[null,null]}""")!!
        assertTrue(placedAtFour(nothing).isEmpty())

        // A character outside the basic plane is two UTF-16 units but one cell.
        val rare = "𠮷野家"
        val rarePlaced = PlacedText.place(
            presented(line(rare)),
            layout(rare),
            ScreenGeometry(1600, 960, listOf(Area(0f, 0f, 1600f, 960f)), emptyList())
        ).single()
        assertEquals(listOf(0, 2, 3), rarePlaced.characters.map { it.offset })
        assertEquals(2, rarePlaced.offsetAt(64f * 4, 201f * 4))
    }

    // ---- Lines and layouts arrive separately ----

    @Test
    fun aLineAndItsLayoutFindEachOtherInEitherOrder() {
        val texts = ScreenTexts()
        val first = presented(line("あ", sequence = 1))

        // Line first.
        assertFalse(texts.adoptLine(first))
        assertTrue(texts.onScreen.isEmpty())
        assertTrue(texts.applyLayout(layout("あ", sequence = 1)))
        assertSame(first, texts.onScreen.single().presented)
        assertNotNull(texts.placementOf(first))

        // Layout first: it waits for its line, and the old text stays until the line is there.
        val second = presented(line("い", sequence = 2))
        assertFalse(texts.applyLayout(layout("い", sequence = 2)))
        assertSame(first, texts.onScreen.single().presented)
        assertTrue(texts.adoptLine(second))
        assertSame(second, texts.onScreen.single().presented)
        assertNull(texts.placementOf(first))

        // The layout moves (text being typed out): same line, new cells.
        assertTrue(texts.applyLayout(layout("い", sequence = 2, box = null)))
        assertNull(texts.onScreen.single().layout.box)
    }

    @Test
    fun newTextReplacesOldAndStaleLayoutsAreIgnored() {
        val texts = ScreenTexts()
        val first = presented(line("あ", sequence = 1))
        val second = presented(line("い", sequence = 2))
        texts.adoptLine(first)
        texts.applyLayout(layout("あ", sequence = 1))

        // New text in the window: what was shown belonged to the old text.
        assertTrue(texts.adoptLine(second))
        assertTrue(texts.onScreen.isEmpty())
        // A late layout for the old text changes nothing.
        assertFalse(texts.applyLayout(layout("あ", sequence = 1)))
        assertTrue(texts.onScreen.isEmpty())
        assertTrue(texts.applyLayout(layout("い", sequence = 2)))

        // A layout that waited for a line the app never showed is dropped by a later line.
        assertFalse(texts.applyLayout(layout("う", sequence = 3)))
        val fourth = presented(line("え", sequence = 4))
        assertTrue(texts.adoptLine(fourth)) // takes い off the screen
        assertTrue(texts.onScreen.isEmpty())
    }

    @Test
    fun closingAndRepeatsAndOtherWindows() {
        val texts = ScreenTexts()
        val first = presented(line("あ", sequence = 1))
        texts.adoptLine(first)
        texts.applyLayout(layout("あ", sequence = 1))

        val closed = ScreenLayout.fromJson("""{"window":"w","seq":1,"closed":true}""")!!
        assertTrue(texts.applyLayout(closed))
        assertFalse(texts.applyLayout(closed))
        assertTrue(texts.onScreen.isEmpty())
        // Shown again later (the box was hidden for a moment).
        assertTrue(texts.applyLayout(layout("あ", sequence = 1)))

        // The game set the same text again: a new event, the same line in the backlog.
        val again = PresentedLine(line("あ", sequence = 5), first.accounting, first.backlogId)
        assertTrue(texts.adoptLine(again))
        assertTrue(texts.applyLayout(layout("あ", sequence = 5)))
        assertNotNull("still the line the reader knows", texts.placementOf(first))

        // Windows do not disturb each other, and a line without a window is never on screen.
        val other = presented(line("か", sequence = 6, window = "v"), backlogId = 60)
        texts.adoptLine(other)
        val otherLayout = ScreenLayout.fromJson("""{"window":"v","seq":6,"glyphs":[[1,1,8,8]]}""")!!
        assertTrue(texts.applyLayout(otherLayout))
        assertEquals(2, texts.onScreen.size)
        assertFalse(texts.adoptLine(presented(line("さ", sequence = 7, window = ""))))
        assertEquals(2, texts.onScreen.size)

        assertTrue(texts.clear())
        assertFalse(texts.clear())
        assertTrue(texts.onScreen.isEmpty())
    }

    @Test
    fun anyWordNotUnderThePopupIsLookedUpAtOnce() {
        // A row from x 100 to 500 at y 200 to 240, the popup about its first word 6 pixels above it.
        val row = Area(100f, 200f, 500f, 240f)
        val popup = Area(90f, 100f, 290f, 194f)
        val own = Area(100f, 200f, 140f, 240f)
        val next = Area(180f, 200f, 220f, 240f)
        val under = Area(100f, 150f, 140f, 190f)
        val beside = Area(300f, 150f, 340f, 190f)
        val edge = Area(275f, 150f, 315f, 190f)

        // No popup: nothing holds a word back.
        assertFalse(PlacedText.keptByPopup(190f, 220f, next, false, Area(0f, 0f, 0f, 0f), row))
        // The popup's own word, and a word it covers, are the popup's.
        assertTrue(PlacedText.keptByPopup(120f, 220f, own, true, popup, row))
        assertTrue(PlacedText.keptByPopup(120f, 195f, under, false, popup, row))
        // The next word along the row, and one of the row above that the popup leaves bare, are
        // looked up at once.
        assertFalse(PlacedText.keptByPopup(190f, 220f, next, false, popup, row))
        assertFalse(PlacedText.keptByPopup(310f, 170f, beside, false, popup, row))
        assertFalse("peeks under its edge", PlacedText.keptByPopup(300f, 170f, edge, false, popup, row))
        // The gap between the row and the popup is the way to it; beside the popup it is not.
        assertTrue(PlacedText.keptByPopup(190f, 197f, next, false, popup, row))
        assertTrue(PlacedText.keptByPopup(250f, 196f, null, false, popup, row))
        assertFalse(PlacedText.keptByPopup(350f, 197f, null, false, popup, row))
        assertFalse(PlacedText.keptByPopup(400f, 260f, null, false, popup, row))

        // Below the row the same.
        val below = Area(90f, 246f, 290f, 340f)
        assertTrue(PlacedText.keptByPopup(150f, 243f, null, false, below, row))
        assertFalse(PlacedText.keptByPopup(190f, 220f, next, false, below, row))
    }
}
