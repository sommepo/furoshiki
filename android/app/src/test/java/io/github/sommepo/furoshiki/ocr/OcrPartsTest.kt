// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The parts of OCR that need no device: when to read, what is kept, the service's answer, the engine. */
class OcrPartsTest {
    private fun plain(width: Int, height: Int, colour: Int) = OcrPicture(width, height, IntArray(width * height) { colour })

    /** A dark picture with [marks] light blocks in its lower part: another number, another look. */
    private fun marked(marks: Int): OcrPicture {
        val picture = plain(320, 180, 0xFF101830.toInt())
        for (i in 0 until marks) {
            for (y in 130..160) for (x in 20 + i * 30 until 40 + i * 30) picture.pixels[y * 320 + x] = 0xFFF0F0F0.toInt()
        }
        return picture
    }

    private fun lower(picture: OcrPicture) = OcrSignature.of(picture, 0f, 0.66f, 1f, 1f)

    // ---- When an area is read ----

    @Test
    fun aSignatureSeesAChangeInItsAreaOnly() {
        val one = lower(marked(2))
        assertEquals(0f, one.changedShare(lower(marked(2))))
        assertTrue(one.changedShare(lower(marked(3))) > 0.02f)
        // A change above the area is not the area's.
        val above = marked(2).also { for (x in 0 until 320) it.pixels[10 * 320 + x] = 0xFFFFFFFF.toInt() }
        assertEquals(0f, one.changedShare(lower(above)))
        // Two areas as one: either changing is a change.
        val both = OcrSignature.joined(listOf(one, OcrSignature.of(marked(2), 0f, 0f, 1f, 0.3f)))
        assertEquals(OcrSignature.ROWS * 2, both.rows)
        assertEquals(1f, both.changedShare(one))
    }

    @Test
    fun readOnlyOnceAChangeHasSettled() {
        val watch = OcrWatch(settleMs = 300)
        // The first look is read once it has stood still.
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(1)), 0))
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(1)), 250))
        assertEquals(OcrWatch.Verdict.READ, watch.see(lower(marked(1)), 300))
        // Unchanged: nothing more, however long.
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(1)), 5000))
        // A line being written out: told once that it changed, read once it stops.
        assertEquals(OcrWatch.Verdict.CHANGED, watch.see(lower(marked(2)), 5250))
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(3)), 5500))
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(4)), 5750))
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(4)), 6000))
        assertEquals(OcrWatch.Verdict.READ, watch.see(lower(marked(4)), 6050))
        // Back to what was read (a cursor that blinks): nothing.
        assertEquals(OcrWatch.Verdict.CHANGED, watch.see(lower(marked(5)), 6300))
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(4)), 6550))
        assertEquals(OcrWatch.Verdict.WAIT, watch.see(lower(marked(4)), 9000))
    }

    @Test
    fun anAreaThatNeverRestsIsReadAnyway() {
        val watch = OcrWatch(settleMs = 300, longestWaitMs = 1000)
        watch.see(lower(marked(1)), 0)
        assertEquals(OcrWatch.Verdict.READ, watch.see(lower(marked(1)), 400))
        var verdicts = (1..7).map { watch.see(lower(marked(2 + it % 2)), 400L + it * 200) }
        // Moving from 600 on: read at the first look a second later (1600), not before.
        assertEquals(5, verdicts.indexOf(OcrWatch.Verdict.READ))
        assertEquals(OcrWatch.Verdict.CHANGED, verdicts.last())
        // Forgotten: read again at the next settled look, though unchanged.
        watch.forget()
        verdicts = listOf(watch.see(lower(marked(3)), 2000), watch.see(lower(marked(3)), 2400))
        assertEquals(listOf(OcrWatch.Verdict.WAIT, OcrWatch.Verdict.READ), verdicts)
    }

    @Test
    fun aServiceThatFailsIsLeftAloneAndToldOnce() {
        val backoff = OcrBackoff(firstMs = 1000, longestMs = 5000)
        assertTrue(backoff.mayAsk(0))
        assertTrue(backoff.failed(0)) // said
        assertFalse(backoff.mayAsk(999))
        assertTrue(backoff.mayAsk(1000))
        assertFalse(backoff.failed(1000)) // not said again
        assertFalse(backoff.mayAsk(2999))
        assertTrue(backoff.mayAsk(3000))
        backoff.failed(3000)
        backoff.failed(7000)
        assertFalse(backoff.mayAsk(11_999)) // at most five seconds
        assertTrue(backoff.mayAsk(12_000))
        assertTrue(backoff.answered()) // back: say so
        assertFalse(backoff.answered())
        assertTrue(backoff.mayAsk(12_000))
        assertTrue(backoff.failed(13_000))
    }

    // ---- A game's areas ----

    @Test
    fun areasAreKeptForEachGame() {
        val folder = Files.createTempDirectory("ocr").toFile()
        try {
            val file = File(folder, "ocr-areas.json")
            val areas = OcrAreas(file)
            assertEquals(OcrGame(), areas.of("SLPS-01234"))
            // Nothing drawn: the lower third is read.
            assertEquals(listOf(OcrArea.LOWER_THIRD), OcrGame(on = true).always)
            areas.set(
                "slps-01234",
                OcrGame(
                    true,
                    listOf(
                        OcrArea(0.1f, 0.7f, 0.8f, 0.25f),
                        OcrArea(0.1f, 0.6f, 0.3f, 0.08f),
                        OcrArea(0.6f, 0.1f, 0.3f, 0.4f, ask = true),
                        OcrArea(0.9f, 0.9f, 0.5f, 0.001f), // out of the picture, too thin: fitted
                    )
                )
            )
            val kept = OcrAreas(file).of("SLPS-01234")
            assertTrue(kept.on)
            assertEquals(4, kept.areas.size)
            // Read by themselves from the top; the one on request apart.
            assertEquals(listOf(0.6f, 0.7f, 0.9f), kept.always.map { it.y })
            assertEquals(listOf(0.6f), kept.asked.map { it.x })
            val fitted = kept.areas[3]
            assertEquals(0.5f, fitted.x, 1e-4f)
            assertEquals(OcrArea.SMALLEST, fitted.height, 1e-4f)
            assertTrue(fitted.right <= 1f && fitted.bottom <= 1f)
            // Only boxes on request: nothing is read by itself.
            assertEquals(emptyList<OcrArea>(), OcrGame(true, listOf(OcrArea(0f, 0f, 0.5f, 0.5f, ask = true))).always)
            // Another game keeps its own; switched off, the areas stay.
            areas.set("OTHER", OcrGame(false, listOf(OcrArea(0f, 0f, 1f, 0.2f))))
            assertEquals(setOf("SLPS-01234", "OTHER"), areas.games())
            areas.set("SLPS-01234", kept.copy(on = false))
            assertFalse(areas.of("SLPS-01234").on)
            assertEquals(4, areas.of("SLPS-01234").areas.size)
            // A file broken by hand is no file.
            file.writeText("{not json")
            assertEquals(OcrGame(), OcrAreas(file).of("OTHER"))
        } finally {
            folder.deleteRecursively()
        }
    }

    // ---- The service's answer ----

    @Test
    fun theServicesAnswer() {
        val read = OcrRead.fromJson(
            """{"text":"アリス今日は","engine":"meikiocr","ms":40,"lines":[
              {"text":"アリス","boxes":[[0.1,0.1,0.1,0.2],[0.2,0.1,0.1,0.2],[0.3,0.1,0.1,0.2]],"vertical":false},
              {"text":"今日は","boxes":[[0.1,0.5,0.1,0.2],[0.2,0.5,0.1,0.2],[0.3,0.5,0.1,0.2]]}]}"""
        )
        assertEquals("アリス今日は", read.text)
        assertEquals("meikiocr", read.engine)
        assertEquals(6, read.boxes!!.size)
        assertEquals(0.5f, read.boxes!![3][1])
        // A line whose boxes are not one for each character: its places are unknown, and so the whole's.
        val short = OcrRead.fromJson("""{"lines":[{"text":"今日は","boxes":[[0,0,1,1]]}]}""")
        assertEquals("今日は", short.text)
        assertNull(short.boxes)
        // Characters outside the BMP count once.
        assertEquals(1, OcrRead.fromJson("""{"lines":[{"text":"𠮷","boxes":[[0,0,1,1]]}]}""").boxes!!.size)
        // The text alone.
        assertEquals("はい", OcrRead.fromJson("""{"text":"はい"}""").text)
        assertEquals("", OcrRead.fromJson("""{"text":"","lines":[]}""").text)
        // What went wrong is said.
        val failed = runCatching { OcrRead.fromJson("""{"error":"OneOCR is not available"}""") }.exceptionOrNull()
        assertEquals("OneOCR is not available", failed?.message)
        assertTrue(runCatching { OcrRead.fromJson("<html>") }.isFailure)
        assertTrue(runCatching { OcrRead.fromJson("{}") }.isFailure)
    }

    // ---- Which engine ----

    @Test
    fun theEngineChosen() {
        val choose = OcrSettings.Companion::choose
        // By default: the maintainer's build the service where one is known; a published one the device.
        assertEquals(OcrEngine.SERVICE, choose("", true, true, true))
        assertEquals(OcrEngine.DEVICE, choose("", true, false, true))
        assertEquals(OcrEngine.DEVICE, choose("", false, true, true))
        // Chosen: that one, while it can be had.
        assertEquals(OcrEngine.DEVICE, choose("device", true, true, true))
        assertEquals(OcrEngine.SERVICE, choose("service", false, true, true))
        assertEquals(OcrEngine.DEVICE, choose("service", false, false, true))
        assertEquals(OcrEngine.SERVICE, choose("device", false, true, false))
        assertEquals(OcrEngine.SERVICE, choose("", false, true, false))
        assertNull(choose("", true, false, false))
    }

    @Test
    fun theSettingsAsKept() {
        assertEquals(OcrSettings(), OcrSettings.parse(""))
        assertEquals(OcrSettings(), OcrSettings.parse("""{"engine":"cloud"}"""))
        val kept = OcrSettings("service", "http://pc:8794", "a-token")
        assertEquals(kept, OcrSettings.parse(kept.toJson()))
        assertEquals("http://192.168.1.20:8794", OcrSettings.clean(" 192.168.1.20:8794/ "))
        assertEquals("https://ocr.example:8794", OcrSettings.clean("https://ocr.example:8794/"))
        assertEquals("", OcrSettings.clean("  "))
    }

    // ---- What the device's engine read, made lines ----

    private fun line(text: String, left: Int, top: Int, size: Int, upright: Boolean = false) = MeikiOcr.Line(
        text,
        text.codePoints().toArray().mapIndexed { n, point ->
            val x = if (upright) left else left + n * size
            val y = if (upright) top + n * size else top
            MeikiOcr.Glyph(String(Character.toChars(point)), x, y, x + size, y + size, 0.9f)
        },
        upright
    )

    @Test
    fun theDevicesLinesCleaned() {
        val read = OcrText.clean(
            listOf(
                line("今日はいい天気だ。▼", 20, 100, 20),
                line("きょう", 20, 86, 8), // furigana over 今日
                line("アリス", 20, 60, 20), // the name above
                line("12:00", 250, 5, 10), // no Japanese
            ),
            400, 200
        )
        assertEquals(listOf("アリス", "今日はいい天気だ。"), read.lines.map { it.text })
        assertEquals("アリス今日はいい天気だ。", read.text)
        val boxes = read.boxes!!
        assertEquals(read.text.length, boxes.size)
        assertEquals(0.05f, boxes[0][0], 1e-5f)
        assertEquals(0.3f, boxes[0][1], 1e-5f)
        assertEquals(0.05f, boxes[0][2], 1e-5f)
        assertEquals(0.1f, boxes[0][3], 1e-5f)
        // Upright columns from the right, their furigana to the right of them dropped.
        val upright = OcrText.clean(
            listOf(line("ふたつめ", 100, 10, 20, true), line("一行目です", 140, 10, 20, true), line("いちぎょうめ", 162, 10, 8, true)),
            200, 200
        )
        assertEquals(listOf("一行目です", "ふたつめ"), upright.lines.map { it.text })
        assertEquals(OcrRead(emptyList()).text, OcrText.clean(emptyList(), 10, 10).text)
    }

    @Test
    fun theSameTextHasTheSameName() {
        assertEquals(OcrText.sourceId("今日は"), OcrText.sourceId("今日は"))
        assertNotEquals(OcrText.sourceId("今日は"), OcrText.sourceId("今日も"))
        assertTrue(OcrText.sourceId("x").startsWith("ocr:"))
        assertTrue(OcrText.hasJapanese("abc。あ"))
        assertFalse(OcrText.hasJapanese("12:00 HP"))
    }
}
