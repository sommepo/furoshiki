// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.security.SecureRandom

/**
 * Google Lens as an OCR engine: the text area is sent to Google's Lens service, the one Chrome's
 * own Lens overlay asks, and its answer (paragraphs, lines, words and where each lies) is read
 * back. The best reading of Japanese there is, at about a second or two a line; **every area read
 * goes to Google**, so it is never the default (the user chooses it in Settings, *OCR engine*).
 *
 * The request and the answer are protocol buffers, written and read here by hand: only the few
 * fields this needs ([Proto]), so no library comes with it.
 */
object GoogleLens {
    private const val ADDRESS = "https://lensfrontend-pa.googleapis.com/v1/crupload"

    /** Chrome's own key for the Lens overlay, as Chrome sends it. */
    private const val KEY = "AIzaSyDr2UxVnv_U85AbhhY8XSHSIavUW0DC-sY"

    private const val PLATFORM_WEB = 3
    private const val SURFACE_CHROMIUM = 4
    private const val AUTO_FILTER = 7
    private const val TOP_TO_BOTTOM = 2

    private val random = SecureRandom()

    /** [png] (a picture [width] by [height]) read by Lens: its lines as the device engine gives them. */
    fun read(png: ByteArray, width: Int, height: Int): List<MeikiOcr.Line> {
        val connection = (URL(ADDRESS).openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            connectTimeout = 10_000
            readTimeout = 20_000
            setRequestProperty("Content-Type", "application/x-protobuf")
            setRequestProperty("X-Goog-Api-Key", KEY)
        }
        try {
            connection.outputStream.use { it.write(request(png, width, height)) }
            val status = connection.responseCode
            if (status != 200) throw IllegalStateException("Google Lens answered $status")
            return lines(connection.inputStream.use { it.readBytes() }, width, height)
        } finally {
            connection.disconnect()
        }
    }

    /** The request: one picture, Japanese, as Chrome's overlay asks. */
    fun request(png: ByteArray, width: Int, height: Int): ByteArray {
        val analytics = ByteArray(16).also { random.nextBytes(it) }
        val requestId = Proto.Writer().varint(1, random.nextLong()).bytes(4, analytics)
        val locale = Proto.Writer().string(1, "ja").string(2, "Asia/Tokyo")
        val filters = Proto.Writer().message(1, Proto.Writer().varint(1, AUTO_FILTER.toLong()))
        val client = Proto.Writer().varint(1, PLATFORM_WEB.toLong()).varint(2, SURFACE_CHROMIUM.toLong())
            .message(4, locale).message(17, filters)
        val context = Proto.Writer().message(3, requestId).message(4, client)
        val image = Proto.Writer().message(1, Proto.Writer().bytes(1, png))
            .message(3, Proto.Writer().varint(1, width.toLong()).varint(2, height.toLong()))
        val objects = Proto.Writer().message(1, context).message(3, image)
        return Proto.Writer().message(1, objects).toByteArray()
    }

    /**
     * The answer's lines, each word's characters given an equal share of the word's box along the
     * line (Lens says where words are, not characters), in pixels of the picture.
     */
    fun lines(answer: ByteArray, width: Int, height: Int): List<MeikiOcr.Line> {
        val out = ArrayList<MeikiOcr.Line>()
        val objects = Proto.message(answer, 2) ?: return out
        val layout = Proto.message(Proto.message(objects, 3) ?: return out, 1) ?: return out
        for (paragraph in Proto.messages(layout, 1)) {
            val upright = Proto.varint(paragraph, 4) == TOP_TO_BOTTOM.toLong()
            for (line in Proto.messages(paragraph, 2)) {
                val glyphs = ArrayList<MeikiOcr.Glyph>()
                for (word in Proto.messages(line, 1)) {
                    val text = Proto.string(word, 2)
                    val box = Proto.message(Proto.message(word, 4) ?: continue, 1) ?: continue
                    val cx = Proto.float(box, 1) * width
                    val cy = Proto.float(box, 2) * height
                    val w = Proto.float(box, 3) * width
                    val h = Proto.float(box, 4) * height
                    val characters = text.codePoints().toArray().map { String(Character.toChars(it)) }.filter { it.isNotBlank() }
                    if (characters.isEmpty()) continue
                    characters.forEachIndexed { index, character ->
                        val share = characters.size.toFloat()
                        val left: Float
                        val top: Float
                        val right: Float
                        val bottom: Float
                        if (upright) {
                            left = cx - w / 2; right = cx + w / 2
                            top = cy - h / 2 + h * index / share; bottom = cy - h / 2 + h * (index + 1) / share
                        } else {
                            top = cy - h / 2; bottom = cy + h / 2
                            left = cx - w / 2 + w * index / share; right = cx - w / 2 + w * (index + 1) / share
                        }
                        glyphs += MeikiOcr.Glyph(character, left.toInt(), top.toInt(), right.toInt().coerceAtLeast(left.toInt() + 1), bottom.toInt().coerceAtLeast(top.toInt() + 1), 1f)
                    }
                }
                if (glyphs.isNotEmpty()) out += MeikiOcr.Line(glyphs.joinToString("") { it.text }, glyphs, upright)
            }
        }
        return out
    }

    /** The little of the protocol buffers' wire format this needs. */
    object Proto {
        class Writer {
            private val out = ByteArrayOutputStream()

            private fun raw(value: Long) {
                var rest = value
                while (rest and 0x7FL.inv() != 0L) {
                    out.write(((rest and 0x7F) or 0x80).toInt())
                    rest = rest ushr 7
                }
                out.write(rest.toInt())
            }

            private fun key(field: Int, type: Int) = raw((field.toLong() shl 3) or type.toLong())

            fun varint(field: Int, value: Long) = apply { key(field, 0); raw(value) }

            fun bytes(field: Int, value: ByteArray) = apply { key(field, 2); raw(value.size.toLong()); out.write(value) }

            fun string(field: Int, value: String) = bytes(field, value.toByteArray(Charsets.UTF_8))

            fun float(field: Int, value: Float) = apply {
                key(field, 5)
                val bits = java.lang.Float.floatToIntBits(value)
                for (i in 0 until 4) out.write((bits ushr (8 * i)) and 0xFF)
            }

            fun message(field: Int, value: Writer) = bytes(field, value.toByteArray())

            fun toByteArray(): ByteArray = out.toByteArray()
        }

        /** Each field of [data] as (number, wire type, value: a Long for numbers, a ByteArray for lengths). */
        private fun fields(data: ByteArray): List<Triple<Int, Int, Any>> {
            val out = ArrayList<Triple<Int, Int, Any>>()
            var at = 0
            fun varint(): Long {
                var result = 0L
                var shift = 0
                while (at < data.size) {
                    val byte = data[at++].toInt() and 0xFF
                    result = result or ((byte and 0x7F).toLong() shl shift)
                    if (byte and 0x80 == 0) return result
                    shift += 7
                }
                throw IllegalArgumentException("Google Lens's answer is cut short")
            }
            while (at < data.size) {
                val key = varint()
                val field = (key ushr 3).toInt()
                when (val type = (key and 7).toInt()) {
                    0 -> out += Triple(field, type, varint())
                    1 -> { out += Triple(field, type, (0 until 8).fold(0L) { acc, i -> acc or ((data[at + i].toLong() and 0xFF) shl (8 * i)) }); at += 8 }
                    2 -> {
                        val length = varint().toInt()
                        if (length < 0 || at + length > data.size) throw IllegalArgumentException("Google Lens's answer is cut short")
                        out += Triple(field, type, data.copyOfRange(at, at + length)); at += length
                    }
                    5 -> { out += Triple(field, type, (0 until 4).fold(0L) { acc, i -> acc or ((data[at + i].toLong() and 0xFF) shl (8 * i)) }); at += 4 }
                    else -> throw IllegalArgumentException("Google Lens's answer has a field of type $type")
                }
            }
            return out
        }

        fun messages(data: ByteArray, field: Int): List<ByteArray> =
            fields(data).filter { it.first == field && it.second == 2 }.map { it.third as ByteArray }

        fun message(data: ByteArray, field: Int): ByteArray? = messages(data, field).firstOrNull()

        fun string(data: ByteArray, field: Int): String = message(data, field)?.toString(Charsets.UTF_8).orEmpty()

        fun varint(data: ByteArray, field: Int): Long = fields(data).firstOrNull { it.first == field && it.second == 0 }?.third as Long? ?: 0L

        fun float(data: ByteArray, field: Int): Float =
            (fields(data).firstOrNull { it.first == field && it.second == 5 }?.third as Long?)?.let { java.lang.Float.intBitsToFloat(it.toInt()) } ?: 0f
    }
}
