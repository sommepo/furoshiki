// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import java.io.File
import javax.imageio.ImageIO
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * The engine of the device, run here on the models themselves (fetched by the build into
 * .gradle/ocr-models; without them this is skipped) and a real game's picture: a frame of the
 * public README's clip of Remember11.
 */
class MeikiOcrTest {
    private val models = File(System.getProperty("ocr.models") ?: "none")
    private val clip = generateSequence(File("").absoluteFile) { it.parentFile }
        .map { File(it, "home/publish/media/lookup-remember11.gif") }.firstOrNull { it.isFile }

    private fun frame(): OcrPicture {
        val image = ImageIO.read(clip)
        return OcrPicture(image.width, image.height, image.getRGB(0, 0, image.width, image.height, null, 0, image.width))
    }

    private fun engine() = MeikiOcr(
        File(models, "detect.onnx").readBytes(), File(models, "read.onnx").readBytes(),
        { File(models, "read-upright.onnx").takeIf { it.isFile }?.readBytes() }
    )

    @Test
    fun aGamesLineIsReadWithItsCharactersPlaces() {
        assumeTrue(File(models, "detect.onnx").isFile && File(models, "read.onnx").isFile && clip != null)
        val whole = frame()
        // The message window: the lower third, as the app offers it where no box was drawn.
        val area = whole.crop(0, whole.height * 2 / 3, whole.width, whole.height)
        engine().use { engine ->
            val started = System.nanoTime()
            val lines = engine.read(area)
            val took = (System.nanoTime() - started) / 1_000_000
            println("meikiocr on the lower third (${area.width}x${area.height}): ${lines.map { it.text }} in $took ms")
            val line = lines.single { "視線" in it.text }
            assertEquals("私は視線をそらし、彼の肩越しへと目を向けた。", line.text)
            assertEquals(false, line.upright)
            // One place a character, from left to right, inside the picture.
            assertEquals(line.text.codePointCount(0, line.text.length), line.glyphs.size)
            assertTrue(line.glyphs.zipWithNext().all { (a, b) -> a.left < b.left })
            assertTrue(line.glyphs.all { it.left >= 0 && it.right <= area.width && it.top >= 0 && it.bottom <= area.height && it.right > it.left })
            // The whole picture too: the same line among whatever else it has.
            val all = engine.read(whole)
            println("meikiocr on the whole picture: ${all.map { it.text }}")
            assertTrue(all.any { it.text == line.text })
        }
    }

    @Test
    fun aPictureWithoutTextHasNoLines() {
        assumeTrue(File(models, "detect.onnx").isFile && File(models, "read.onnx").isFile)
        engine().use { engine ->
            assertEquals(emptyList<String>(), engine.read(OcrPicture(320, 120, IntArray(320 * 120) { 0xFF102040.toInt() })).map { it.text })
        }
    }

    @Test
    fun aPartOfAPicture() {
        val picture = OcrPicture(4, 3, IntArray(12) { it })
        val part = picture.crop(1, 1, 3, 9)
        assertEquals(listOf(2, 2), listOf(part.width, part.height))
        assertEquals(listOf(5, 6, 9, 10), part.pixels.toList())
    }
}
