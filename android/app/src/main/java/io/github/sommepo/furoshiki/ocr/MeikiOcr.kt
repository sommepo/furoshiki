// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import ai.onnxruntime.OnnxJavaType
import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import ai.onnxruntime.TensorInfo
import java.nio.FloatBuffer
import java.nio.LongBuffer
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** A picture as its pixels, row after row, each 0xAARRGGBB (android.graphics.Bitmap.getPixels). */
class OcrPicture(val width: Int, val height: Int, val pixels: IntArray) {
    init {
        require(width > 0 && height > 0 && pixels.size >= width * height) { "not a picture" }
    }

    /** The part of it from ([left], [top]) to ([right], [bottom]), kept inside the picture. */
    fun crop(left: Int, top: Int, right: Int, bottom: Int): OcrPicture {
        val x0 = left.coerceIn(0, width - 1)
        val y0 = top.coerceIn(0, height - 1)
        val x1 = right.coerceIn(x0 + 1, width)
        val y1 = bottom.coerceIn(y0 + 1, height)
        val out = IntArray((x1 - x0) * (y1 - y0))
        for (y in y0 until y1) System.arraycopy(pixels, y * width + x0, out, (y - y0) * (x1 - x0), x1 - x0)
        return OcrPicture(x1 - x0, y1 - y0, out)
    }
}

/**
 * OCR on the device: meikiocr (rtr46, https://github.com/rtr46/meikiocr, Apache-2.0; its models
 * LGPL-3.0), "high-speed, high-accuracy, local OCR for Japanese video games", the engine the
 * OCR helper reads with on a computer. This is its pipeline (meikiocr/ocr.py, 0.3.5) in Kotlin
 * over ONNX Runtime: the text lines are found (a D-FINE detector at 960x544), then each is read
 * by a model that answers its characters with their places, lying lines by one and upright
 * lines by another. Nothing here is Android's: the tests run it on a computer.
 *
 * One call at a time. The models are given as their bytes; the upright one is asked for only
 * when a picture has an upright line.
 */
class MeikiOcr(
    detect: ByteArray,
    read: ByteArray,
    private val readUpright: () -> ByteArray?,
    /** Threads of the processor a read may use: few, as a game is running beside it. */
    private val threads: Int = 2
) : AutoCloseable {
    /** A character and where it is in the picture, in its pixels. */
    class Glyph(val text: String, val left: Int, val top: Int, val right: Int, val bottom: Int, val confidence: Float)

    class Line(val text: String, val glyphs: List<Glyph>, val upright: Boolean)

    private val environment = OrtEnvironment.getEnvironment()
    private val detector = session(detect)
    private val reader = session(read)
    private var uprightReader: OrtSession? = null

    private fun session(model: ByteArray): OrtSession {
        val options = OrtSession.SessionOptions()
        options.setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
        options.setIntraOpNumThreads(threads)
        options.setInterOpNumThreads(1)
        // Threads that wait for work sleep: spinning would take a processor from the game.
        options.addConfigEntry("session.intra_op.allow_spinning", "0")
        options.addConfigEntry("session.inter_op.allow_spinning", "0")
        return environment.createSession(model, options)
    }

    override fun close() {
        detector.close()
        reader.close()
        uprightReader?.close()
    }

    /** The lines of text in [picture], from the top. */
    @Synchronized
    fun read(picture: OcrPicture): List<Line> {
        val boxes = detect(picture)
        val out = ArrayList<Line>()
        for (box in boxes) {
            val width = box[2] - box[0]
            val height = box[3] - box[1]
            if (width <= 0 || height <= 0) continue
            val line = if (height > width) readUprightLine(picture, box) else readLine(picture, box)
            if (line != null && line.text.isNotEmpty()) out.add(line)
        }
        return out
    }

    // ---- Finding the lines ----

    /** The boxes of the text lines ([left, top, right, bottom], pixels), from the top. */
    internal fun detect(picture: OcrPicture): List<IntArray> {
        val scale = min(DETECT_WIDTH.toFloat() / picture.width, DETECT_HEIGHT.toFloat() / picture.height)
        val width = (picture.width * scale).toInt().coerceAtLeast(1)
        val height = (picture.height * scale).toInt().coerceAtLeast(1)
        val input = planes(picture, 0, 0, picture.width, picture.height, width, height, DETECT_WIDTH, DETECT_HEIGHT)
        val sizes = longArrayOf((DETECT_WIDTH / scale).toLong(), (DETECT_HEIGHT / scale).toLong())
        val outputs = run(detector, input, DETECT_WIDTH, DETECT_HEIGHT, sizes)
        val boxes = outputs.floats(1)
        val scores = outputs.floats(2)
        val found = ArrayList<IntArray>()
        for (i in scores.indices) {
            if (scores[i] <= DETECT_THRESHOLD) continue
            found.add(
                intArrayOf(
                    boxes[i * 4].coerceIn(0f, picture.width.toFloat()).toInt(),
                    boxes[i * 4 + 1].coerceIn(0f, picture.height.toFloat()).toInt(),
                    boxes[i * 4 + 2].coerceIn(0f, picture.width.toFloat()).toInt(),
                    boxes[i * 4 + 3].coerceIn(0f, picture.height.toFloat()).toInt()
                )
            )
        }
        found.sortBy { it[1] }
        return found
    }

    // ---- Reading a line ----

    private class Candidate(val glyph: Glyph, var confidence: Float, val from: Int, val to: Int)

    private fun readLine(picture: OcrPicture, box: IntArray): Line? {
        val width = box[2] - box[0]
        val height = box[3] - box[1]
        var newHeight = READ_HEIGHT
        var newWidth = (width * (READ_HEIGHT.toFloat() / height)).roundToInt()
        if (newWidth > READ_WIDTH) {
            newHeight = (newHeight * (READ_WIDTH.toFloat() / newWidth)).roundToInt()
            newWidth = READ_WIDTH
        }
        if (newWidth < 1 || newHeight < 1) return null
        val input = planes(picture, box[0], box[1], width, height, newWidth, newHeight, READ_WIDTH, READ_HEIGHT)
        val outputs = run(reader, input, READ_WIDTH, READ_HEIGHT, longArrayOf(READ_WIDTH.toLong(), READ_HEIGHT.toLong()))
        val labels = outputs.longs(0)
        val boxes = outputs.floats(1)
        val scores = outputs.floats(2)
        val candidates = ArrayList<Candidate>()
        for (i in scores.indices) {
            if (scores[i] < READ_THRESHOLD) continue
            var x1 = boxes[i * 4]
            var x2 = boxes[i * 4 + 2]
            if (x1 >= newWidth) continue
            x1 = min(x1, newWidth.toFloat())
            x2 = min(x2, newWidth.toFloat())
            val left = box[0] + (x1 / newWidth * width).toInt()
            val right = box[0] + (x2 / newWidth * width).toInt()
            val top = box[1] + (boxes[i * 4 + 1] / READ_HEIGHT * height).toInt()
            val bottom = box[1] + (boxes[i * 4 + 3] / READ_HEIGHT * height).toInt()
            val text = character(labels[i]) ?: continue
            candidates.add(Candidate(Glyph(text, left, top, right, bottom, scores[i]), scores[i], left, right))
        }
        return chosen(candidates, upright = false)
    }

    private fun readUprightLine(picture: OcrPicture, box: IntArray): Line? {
        val model = uprightReader ?: readUpright()?.let { session(it) }?.also { uprightReader = it } ?: return null
        val width = box[2] - box[0]
        val height = box[3] - box[1]
        val scale = UPRIGHT_WIDTH.toFloat() / width
        val scaled = height * scale
        // A column longer than the model takes is read in parts that overlap.
        val starts = ArrayList<Float>()
        val part: Float
        val most: Int
        if (scaled > UPRIGHT_HEIGHT) {
            most = UPRIGHT_CONTENT
            part = UPRIGHT_CONTENT / scale
            val stride = (UPRIGHT_CONTENT - UPRIGHT_OVERLAP) / scale
            var at = box[1].toFloat()
            while (at + part < box[3]) {
                starts.add(at)
                at += stride
            }
            val last = box[3] - part
            if (starts.isEmpty() || last > starts.last() + 1f) starts.add(last)
        } else {
            most = UPRIGHT_HEIGHT
            part = height.toFloat()
            starts.add(box[1].toFloat())
        }
        val candidates = ArrayList<Candidate>()
        for (start in starts) {
            val top = start.roundToInt()
            val bottom = min((start + part).roundToInt(), box[3])
            val partHeight = bottom - top
            if (partHeight <= 0) continue
            val newHeight = min((partHeight * scale).roundToInt(), most).coerceAtLeast(1)
            val input = planes(picture, box[0], top, width, partHeight, UPRIGHT_WIDTH, newHeight, UPRIGHT_WIDTH, UPRIGHT_HEIGHT)
            val outputs = run(model, input, UPRIGHT_WIDTH, UPRIGHT_HEIGHT, longArrayOf(UPRIGHT_WIDTH.toLong(), UPRIGHT_HEIGHT.toLong()))
            val labels = outputs.longs(0)
            val boxes = outputs.floats(1)
            val scores = outputs.floats(2)
            for (i in scores.indices) {
                if (scores[i] < READ_THRESHOLD) continue
                var y1 = boxes[i * 4 + 1]
                var y2 = boxes[i * 4 + 3]
                if (y1 >= newHeight) continue
                y1 = min(y1, newHeight.toFloat())
                y2 = min(y2, newHeight.toFloat())
                val left = box[0] + (boxes[i * 4] / UPRIGHT_WIDTH * width).toInt()
                val right = box[0] + (boxes[i * 4 + 2] / UPRIGHT_WIDTH * width).toInt()
                val glyphTop = top + (y1 / newHeight * partHeight).toInt()
                val glyphBottom = top + (y2 / newHeight * partHeight).toInt()
                if (glyphBottom <= glyphTop) continue
                val text = character(labels[i]) ?: continue
                candidates.add(Candidate(Glyph(text, left, glyphTop, right, glyphBottom, scores[i]), scores[i], glyphTop, glyphBottom))
            }
        }
        return chosen(candidates, upright = true)
    }

    /** Of characters that lie over one another, the surest; then in the order they are read. */
    private fun chosen(candidates: List<Candidate>, upright: Boolean): Line? {
        if (candidates.isEmpty()) return null
        for (candidate in candidates) {
            if (isPunctuation(candidate.glyph.text)) candidate.confidence *= PUNCTUATION_FACTOR
        }
        val accepted = ArrayList<Candidate>()
        for (candidate in candidates.sortedByDescending { it.confidence }) {
            val length = candidate.to - candidate.from + EPSILON
            val covered = accepted.any { other ->
                if (candidate.from >= other.to || other.from >= candidate.to) return@any false
                val shared = max(0, min(candidate.to, other.to) - max(candidate.from, other.from))
                shared / min(length, other.to - other.from + EPSILON) > OVERLAP
            }
            if (!covered) accepted.add(candidate)
        }
        accepted.sortBy { it.from }
        val glyphs = accepted.map { it.glyph }.toMutableList()
        // Pairs the model is known to give the wrong way round.
        var text = glyphs.joinToString("") { it.text }
        for ((wrong, right) in SWAPPED) {
            val at = glyphs.indices.firstOrNull { it + 1 < glyphs.size && glyphs[it].text + glyphs[it + 1].text == wrong } ?: continue
            val first = glyphs[at]
            val second = glyphs[at + 1]
            glyphs[at] = Glyph(right.substring(0, 1), first.left, first.top, first.right, first.bottom, first.confidence)
            glyphs[at + 1] = Glyph(right.substring(1), second.left, second.top, second.right, second.bottom, second.confidence)
            text = glyphs.joinToString("") { it.text }
        }
        return Line(text, glyphs, upright)
    }

    private fun character(label: Long): String? {
        val point = label.toInt()
        if (point <= 0 || !Character.isValidCodePoint(point)) return null
        // What the model could not name is a gap.
        if (point == 0xFFFD) return " "
        return String(Character.toChars(point))
    }

    private fun isPunctuation(text: String): Boolean = when (Character.getType(text.codePointAt(0)).toByte()) {
        Character.CONNECTOR_PUNCTUATION, Character.DASH_PUNCTUATION, Character.START_PUNCTUATION,
        Character.END_PUNCTUATION, Character.INITIAL_QUOTE_PUNCTUATION, Character.FINAL_QUOTE_PUNCTUATION,
        Character.OTHER_PUNCTUATION -> true
        else -> false
    }

    // ---- Pictures to tensors, and a model's answer ----

    /**
     * The part of [picture] at ([left], [top]) of [width] by [height], made [newWidth] by
     * [newHeight] (between neighbours, as OpenCV's INTER_LINEAR does) in the top left of a
     * black picture of [fullWidth] by [fullHeight]: its blue, green and red planes, 0 to 1.
     */
    private fun planes(
        picture: OcrPicture, left: Int, top: Int, width: Int, height: Int,
        newWidth: Int, newHeight: Int, fullWidth: Int, fullHeight: Int
    ): FloatArray {
        val plane = fullWidth * fullHeight
        val out = FloatArray(plane * 3)
        val stepX = width.toFloat() / newWidth
        val stepY = height.toFloat() / newHeight
        val x0 = IntArray(newWidth)
        val x1 = IntArray(newWidth)
        val shareX = FloatArray(newWidth)
        for (x in 0 until newWidth) {
            val at = ((x + 0.5f) * stepX - 0.5f).coerceIn(0f, (width - 1).toFloat())
            x0[x] = at.toInt()
            x1[x] = min(x0[x] + 1, width - 1)
            shareX[x] = at - x0[x]
        }
        val pixels = picture.pixels
        for (y in 0 until newHeight) {
            val at = ((y + 0.5f) * stepY - 0.5f).coerceIn(0f, (height - 1).toFloat())
            val y0 = at.toInt()
            val y1 = min(y0 + 1, height - 1)
            val shareY = at - y0
            val row0 = (top + y0) * picture.width + left
            val row1 = (top + y1) * picture.width + left
            val to = y * fullWidth
            for (x in 0 until newWidth) {
                val a = pixels[row0 + x0[x]]
                val b = pixels[row0 + x1[x]]
                val c = pixels[row1 + x0[x]]
                val d = pixels[row1 + x1[x]]
                val sx = shareX[x]
                for (channel in 0..2) {
                    // Blue first: the models were taught on OpenCV's pictures.
                    val shift = channel * 8
                    val upper = ((a shr shift) and 0xFF) * (1 - sx) + ((b shr shift) and 0xFF) * sx
                    val lower = ((c shr shift) and 0xFF) * (1 - sx) + ((d shr shift) and 0xFF) * sx
                    out[channel * plane + to + x] = (upper * (1 - shareY) + lower * shareY) / 255f
                }
            }
        }
        return out
    }

    /** A model's answer: its three outputs in its own order, labels, boxes (four numbers each), scores. */
    private class Answer(private val outputs: List<Any>) {
        fun floats(at: Int): FloatArray = when (val output = outputs[at]) {
            is FloatArray -> output
            is LongArray -> FloatArray(output.size) { output[it].toFloat() }
            else -> FloatArray(0)
        }

        fun longs(at: Int): LongArray = when (val output = outputs[at]) {
            is LongArray -> output
            is FloatArray -> LongArray(output.size) { output[it].toLong() }
            else -> LongArray(0)
        }
    }

    private fun run(model: OrtSession, planes: FloatArray, width: Int, height: Int, sizes: LongArray): Answer {
        val inputs = HashMap<String, OnnxTensor>()
        try {
            for ((name, node) in model.inputInfo) {
                val info = node.info as TensorInfo
                inputs[name] = if (info.type == OnnxJavaType.FLOAT) {
                    OnnxTensor.createTensor(environment, FloatBuffer.wrap(planes), longArrayOf(1, 3, height.toLong(), width.toLong()))
                } else {
                    OnnxTensor.createTensor(environment, LongBuffer.wrap(sizes), longArrayOf(1, 2))
                }
            }
            model.run(inputs).use { result ->
                val outputs = ArrayList<Any>()
                for ((_, value) in result) {
                    val tensor = value as OnnxTensor
                    outputs.add(
                        when ((tensor.info as TensorInfo).type) {
                            OnnxJavaType.FLOAT -> tensor.floatBuffer.let { buffer -> FloatArray(buffer.remaining()).also { buffer.get(it) } }
                            OnnxJavaType.INT64 -> tensor.longBuffer.let { buffer -> LongArray(buffer.remaining()).also { buffer.get(it) } }
                            OnnxJavaType.INT32 -> tensor.intBuffer.let { buffer -> LongArray(buffer.remaining()) { buffer.get().toLong() } }
                            else -> FloatArray(0)
                        }
                    )
                }
                require(outputs.size >= 3) { "not one of meikiocr's models" }
                return Answer(outputs)
            }
        } finally {
            inputs.values.forEach { it.close() }
        }
    }

    companion object {
        private const val DETECT_WIDTH = 960
        private const val DETECT_HEIGHT = 544
        private const val DETECT_THRESHOLD = 0.5f
        private const val READ_WIDTH = 960
        private const val READ_HEIGHT = 32
        private const val READ_THRESHOLD = 0.1f
        private const val UPRIGHT_WIDTH = 32
        private const val UPRIGHT_HEIGHT = 480
        private const val UPRIGHT_CONTENT = 420
        private const val UPRIGHT_OVERLAP = 64
        private const val OVERLAP = 0.3f
        private const val EPSILON = 1e-6f

        /** A mark is believed less than a character it lies over (owocr reads with this too). */
        private const val PUNCTUATION_FACTOR = 0.2f

        private val SWAPPED = mapOf(
            "儡傀" to "傀儡", "談冗" to "冗談", "汰淘" to "淘汰", "沱滂" to "滂沱",
            "攣痙" to "痙攣", "酊酩" to "酩酊", "麭麺" to "麺麭", "哭慟" to "慟哭"
        )
    }
}
