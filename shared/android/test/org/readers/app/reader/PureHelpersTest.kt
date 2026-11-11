// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import org.readers.app.reader.mining.Letterbox
import org.readers.app.reader.mining.PcmTools
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PureHelpersTest {
    @Test
    fun resamplesKeepingDurationAndChannels() {
        // One second of stereo at the console rate: left ramps up, right is constant.
        val rate = 32728
        val input = ShortArray(rate * 2) { i -> if (i % 2 == 0) (i / 2 % 1000).toShort() else 777 }
        val out = PcmTools.resample(input, 2, rate, 48000)
        assertEquals(48000 * 2, out.size)
        assertEquals(0, out[0].toInt())
        for (i in 1 until out.size step 2) assertEquals(777, out[i].toInt())
        // Same rate: an identical copy.
        assertArrayEquals(input, PcmTools.resample(input, 2, rate, rate))
    }

    @Test
    fun trimsTrailingSilenceWithPadding() {
        val samples = ShortArray(2000) // 1000 stereo frames
        samples[2 * 300] = 5000 // last loud frame is frame 300
        samples[2 * 100 + 1] = -4000
        samples[2 * 900] = 50 // below the threshold: still silence
        val trimmed = PcmTools.trimTrailingSilence(samples, 2, threshold = 180, paddingFrames = 20)
        assertEquals((301 + 20) * 2, trimmed.size)
        assertEquals(0, PcmTools.trimTrailingSilence(ShortArray(400), 2, 180, 20).size)
        // Padding never reads past the end.
        val short = ShortArray(20).also { it[18] = 9000 }
        assertEquals(20, PcmTools.trimTrailingSilence(short, 2, 180, 500).size)
    }

    @Test
    fun cutsAClipToWhatIsSaid() {
        val rate = 48000
        fun tone(samples: ShortArray, fromMs: Int, toMs: Int, level: Int) {
            for (i in fromMs * rate / 1000 until toMs * rate / 1000) {
                val value = (level * kotlin.math.sin(i * 2.0 * Math.PI * 300 / rate)).toInt().toShort()
                samples[i * 2] = value
                samples[i * 2 + 1] = value
            }
        }
        // Four seconds: a hiss all through, a click at 0.2 s, a voice from 1.0 to 2.5 s.
        val clip = ShortArray(rate * 4 * 2) { ((it.toLong() * 7919) % 41 - 20).toInt().toShort() }
        tone(clip, 200, 220, 9000)
        tone(clip, 1000, 2500, 6000)
        val seconds = PcmTools.trimToVoice(clip, 2, rate, 180, 150).size / 2 / rate.toDouble()
        assertTrue(seconds > 1.75 && seconds < 1.9)
        // Music under the whole line stays whole; hiss alone is nothing.
        val music = ShortArray(rate * 2 * 2).also { tone(it, 0, 2000, 5000) }
        assertEquals(music.size, PcmTools.trimToVoice(music, 2, rate, 180, 150).size)
        val hiss = ShortArray(rate * 2 * 2) { ((it.toLong() * 7919) % 41 - 20).toInt().toShort() }
        assertEquals(0, PcmTools.trimToVoice(hiss, 2, rate, 180, 150).size)
    }

    @Test
    fun fadesBothEnds() {
        val samples = ShortArray(200) { 1000 }
        PcmTools.fadeEdges(samples, 2, 10)
        assertEquals(0, samples[0].toInt())
        assertEquals(0, samples[199].toInt())
        assertEquals(1000, samples[100].toInt())
    }

    private fun picture(width: Int, height: Int, left: Int, top: Int, right: Int, bottom: Int): IntArray {
        val pixels = IntArray(width * height) { 0xFF000000.toInt() }
        for (y in top until bottom) for (x in left until right) pixels[y * width + x] = 0xFF336699.toInt()
        return pixels
    }

    @Test
    fun findsThePictureInsideBlackBars() {
        // A 5:3 picture pillarboxed in 16:9, as on a wide display.
        val bounds = Letterbox.bounds(picture(160, 90, 5, 0, 155, 90), 160, 90)
        assertEquals(Letterbox.Bounds(5, 0, 155, 90), bounds)
        val both = Letterbox.bounds(picture(100, 100, 10, 20, 90, 70), 100, 100)
        assertEquals(Letterbox.Bounds(10, 20, 90, 70), both)
    }

    @Test
    fun leavesDarkPicturesWhole() {
        // Entirely black, and a picture that is nearly all black: neither is cropped.
        assertEquals(Letterbox.Bounds(0, 0, 100, 100), Letterbox.bounds(picture(100, 100, 0, 0, 0, 0), 100, 100))
        assertEquals(Letterbox.Bounds(0, 0, 100, 100), Letterbox.bounds(picture(100, 100, 40, 40, 50, 50), 100, 100))
        // Nothing to remove.
        assertEquals(Letterbox.Bounds(0, 0, 8, 8), Letterbox.bounds(picture(8, 8, 0, 0, 8, 8), 8, 8))
    }
}
