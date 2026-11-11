// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.mining

import kotlin.math.abs
import kotlin.math.floor

/** Helpers for interleaved 16-bit PCM. */
object PcmTools {
    /**
     * Resamples interleaved PCM with linear interpolation. Good enough for speech taken from a
     * 32.7 kHz console mix, and cheap.
     */
    fun resample(input: ShortArray, channels: Int, fromRate: Int, toRate: Int): ShortArray {
        require(channels > 0 && fromRate > 0 && toRate > 0)
        val inFrames = input.size / channels
        if (inFrames == 0 || fromRate == toRate) return input.copyOf(inFrames * channels)
        val outFrames = (inFrames.toLong() * toRate / fromRate).toInt()
        val out = ShortArray(outFrames * channels)
        val step = fromRate.toDouble() / toRate
        for (i in 0 until outFrames) {
            val position = i * step
            val index = floor(position).toInt().coerceAtMost(inFrames - 1)
            val next = (index + 1).coerceAtMost(inFrames - 1)
            val fraction = position - index
            for (c in 0 until channels) {
                val a = input[index * channels + c].toInt()
                val b = input[next * channels + c].toInt()
                out[i * channels + c] = (a + (b - a) * fraction).toInt().toShort()
            }
        }
        return out
    }

    /**
     * Drops near-silent audio from the end, keeping [paddingFrames] after the last frame louder
     * than [threshold]. Returns an empty array when everything is silent.
     */
    fun trimTrailingSilence(
        input: ShortArray,
        channels: Int,
        threshold: Int,
        paddingFrames: Int
    ): ShortArray {
        val frames = input.size / channels
        var last = -1
        var frame = frames - 1
        while (frame >= 0 && last < 0) {
            for (c in 0 until channels) {
                if (abs(input[frame * channels + c].toInt()) > threshold) {
                    last = frame
                    break
                }
            }
            frame--
        }
        if (last < 0) return ShortArray(0)
        val keep = (last + 1 + paddingFrames).coerceAtMost(frames)
        return input.copyOf(keep * channels)
    }

    /**
     * The part of a clip in which something is said: from a little before the first sound that
     * stands out of the clip's own quiet to a little after the last. Loudness is measured in
     * windows of 20 ms against the clip's quietest tenth, so a hiss counts as quiet, and a click
     * shorter than three windows is no beginning. A clip that is the same all through (music
     * under the whole line) is kept whole; one with nothing above [threshold] is empty. The
     * desktop's PcmTools::TrimToVoice, the same sums.
     */
    fun trimToVoice(input: ShortArray, channels: Int, rate: Int, threshold: Int, paddingMs: Int): ShortArray {
        if (channels <= 0 || rate <= 0) return ShortArray(0)
        val frames = input.size / channels
        val window = maxOf(rate / 50, 1)
        val windows = frames / window
        if (windows < 4) return trimTrailingSilence(input, channels, threshold, rate * paddingMs / 1000)
        val loud = DoubleArray(windows)
        val count = window * channels
        for (w in 0 until windows) {
            var sum = 0.0
            val from = w * count
            for (i in 0 until count) {
                val v = input[from + i].toDouble()
                sum += v * v
            }
            loud[w] = kotlin.math.sqrt(sum / count)
        }
        val sorted = loud.sortedArray()
        val quiet = sorted[sorted.size / 10]
        val peak = sorted[sorted.size * 95 / 100]
        if (peak <= threshold / 3.0) return ShortArray(0)
        if (peak < quiet * 2.5) return input.copyOf(frames * channels)
        val gate = maxOf(quiet * 2.5, peak * 0.06, threshold / 3.0)
        var first = -1
        var last = -1
        var run = 0
        for (w in 0 until windows) {
            run = if (loud[w] > gate) run + 1 else 0
            if (run >= 3) {
                if (first < 0) first = w - 2
                last = w
            }
        }
        if (first < 0) return ShortArray(0)
        val padding = (rate.toLong() * paddingMs / 1000).toInt()
        val begin = maxOf(first * window - padding, 0)
        val end = minOf((last + 1) * window + padding, frames)
        return input.copyOfRange(begin * channels, end * channels)
    }

    /** Fades the first and last [frames] frames to avoid clicks at the clip edges. */
    fun fadeEdges(samples: ShortArray, channels: Int, frames: Int) {
        val total = samples.size / channels
        val n = frames.coerceAtMost(total / 2)
        for (i in 0 until n) {
            val gain = i.toFloat() / n
            for (c in 0 until channels) {
                val head = i * channels + c
                val tail = (total - 1 - i) * channels + c
                samples[head] = (samples[head] * gain).toInt().toShort()
                samples[tail] = (samples[tail] * gain).toInt().toShort()
            }
        }
    }
}
