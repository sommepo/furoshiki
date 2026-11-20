// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.widget.Toast
import io.github.sommepo.furoshiki.R
import io.github.sommepo.furoshiki.Services
import io.github.sommepo.furoshiki.reader.ReaderSession
import io.github.sommepo.furoshiki.reader.model.TextLine
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.concurrent.Executors
import org.json.JSONArray
import org.json.JSONObject

/**
 * A game read by OCR: its text areas ([OcrAreas]) looked at a few times a second in a small copy
 * of the game's picture; once one has changed and settled ([OcrWatch]) the picture is copied whole,
 * the areas cut out and read by the engine chosen ([OcrSettings.choose]), and what was read becomes
 * a line of the reading session as a script's line would (statistics, backlog, cards, the text
 * server), with its characters' places in the picture where the engine gives them, so a tap on the
 * game's text looks a word up. On a second display the line is shown large and looked up there
 * (ReaderOverlay, a line [TextLine.isFromPicture]).
 *
 * Never on the emulator's thread: the copies are the display's (PixelCopy, as a card's picture is
 * taken, or the screen's capture for a game another app plays), the rest on a thread of its own,
 * one read at a time.
 */
class OcrSource(
    private val activity: Activity,
    /** Where the pictures come from: the game's surface, or the screen's capture ([OcrFrames]). */
    val frames: OcrFrames,
    private val gameId: String,
    /** What is said when a read is asked for with OCR off: where this screen turns it on. */
    private val offHint: Int = R.string.ocr_off_here,
    /** Whether the game is in front and playing now: nothing is looked at otherwise. */
    private val running: () -> Boolean,
) {
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { Thread(it, "Ocr").apply { priority = Thread.MIN_PRIORITY + 1 } }
    private val areas = OcrAreas(File(activity.filesDir, "ocr-areas.json"))
    private val watch = OcrWatch()
    private val backoff = OcrBackoff()

    /**
     * Where a layer of the app's own lies over the picture now, as parts (0 to 1) of [frames]: a
     * capture of the screen holds it (an app's companion). Those parts are left out of the look
     * for a change, and the text is not read while one of them lies over it.
     */
    var drawn: () -> List<android.graphics.RectF> = { emptyList() }

    /** The text has begun to change: the game moved on (an app's companion closes its popup). */
    var onChanged: () -> Unit = {}

    /** This game's record; read again when it is changed. */
    var game: OcrGame = areas.of(gameId)
        private set

    /** Whether the game has a record of its own: else it is read by OCR when no script reads it. */
    private var recorded = gameId.isNotEmpty() && gameId.trim().uppercase() in areas.games()

    /** Whether the game's text areas were set by the user (else the first guess: the lower third). */
    val areaSet: Boolean get() = recorded

    /** Whether a script was found for the game, once the session has said. */
    private var scripted: Boolean? = null

    private var started = false
    private var stopped = false

    @Volatile
    private var reading = false // a read in flight (main thread sets, worker clears)
    private var sequence = 0L
    private var lastText = ""

    /** What was read as the newest line, its places among it. */
    private var lastRead: OcrRead? = null
    private var placed = false

    private var settings = OcrSettings.read(activity)
    private var device: MeikiOcr? = null

    /** Whether the game is being read by OCR now. */
    val active: Boolean get() = started && !stopped && wanted()

    private fun wanted(): Boolean = if (recorded) game.on else scripted == false

    // ---- From GameActivity ----

    /** The game is running: looking begins once the session says whether a script reads it. */
    fun start() {
        if (started) return
        started = true
        val status = ReaderSession.profileStatus
        if (status.loaded) scripted = true
        // A game whose script never says it loaded has none.
        main.postDelayed({ if (scripted == null) scripted = ReaderSession.profileStatus.loaded }, NO_SCRIPT_AFTER_MS)
        main.post(tick)
    }

    fun stop() {
        if (stopped) return
        stopped = true
        main.removeCallbacksAndMessages(null)
        worker.execute { synchronized(this) { device?.close(); device = null } }
        worker.shutdown()
    }

    /** Back in front: the settings may have been changed on the page meanwhile. */
    fun reload() {
        val now = OcrSettings.read(activity)
        if (now != settings) {
            settings = now
            backoff.answered()
            watch.forget()
        }
    }

    /** This game's areas and whether it is read so, as the setup kept them. */
    fun setGame(next: OcrGame) {
        areas.set(gameId, next)
        game = areas.of(gameId)
        recorded = true
        watch.forget()
        lastText = ""
        if (!game.on) clearPlacement()
    }

    /** Reads the areas read on request (a menu); where there are none, the others, even if unchanged. */
    fun readNow() {
        if (!active) {
            say(activity.getString(offHint))
            return
        }
        copyAndRead(game.asked.ifEmpty { game.always }, asked = true)
    }

    // ---- Looking at the picture ----

    private val tick = object : Runnable {
        override fun run() {
            if (stopped) return
            main.postDelayed(this, LOOK_MS)
            if (scripted == null && ReaderSession.profileStatus.loaded) scripted = true
            if (!wanted() || !running() || !ReaderSession.isActive || reading) return
            val picture = frames.picture() ?: return
            frames.copy(LOOK_WIDTH) { bitmap ->
                if (bitmap == null) return@copy
                val now = SystemClock.uptimeMillis()
                val look = game.always
                val over = drawn()
                val hidden = if (over.isEmpty()) null else hiddenCells(picture, look, over)
                worker.execute {
                    val verdict = try {
                        watch.see(signature(bitmap, picture, look), now, hidden)
                    } finally {
                        bitmap.recycle()
                    }
                    when (verdict) {
                        // Something in the area moves (a bobbing mark, a character behind the text): the
                        // places and the popup stay until a reading says the text itself is other
                        // (the maintainer, 2026-11-17: lookups "very very intermittently" on a still line).
                        OcrWatch.Verdict.CHANGED -> Unit
                        OcrWatch.Verdict.READ -> main.post {
                            when {
                                // The app's own marks lie over the text: read once they are gone.
                                hidden?.any { it } == true && overText(picture, look) -> watch.forget()
                                backoff.mayAsk(SystemClock.uptimeMillis()) -> copyAndRead(look, asked = false)
                                else -> watch.forget()
                            }
                        }
                        OcrWatch.Verdict.WAIT -> Unit
                    }
                }
            }
        }
    }

    /** The cells of the areas' look (as [signature] lays them) that one of [over] lies on. */
    private fun hiddenCells(picture: Rect, look: List<OcrArea>, over: List<android.graphics.RectF>): BooleanArray {
        val columns = OcrSignature.COLUMNS
        val rows = OcrSignature.ROWS
        val hidden = BooleanArray(columns * rows * look.size)
        val width = frames.width.coerceAtLeast(1).toFloat()
        val height = frames.height.coerceAtLeast(1).toFloat()
        look.forEachIndexed { index, area ->
            val left = (picture.left + area.x * picture.width()) / width
            val top = (picture.top + area.y * picture.height()) / height
            val right = (picture.left + area.right * picture.width()) / width
            val bottom = (picture.top + area.bottom * picture.height()) / height
            for (row in 0 until rows) {
                val y0 = top + (bottom - top) * row / rows
                val y1 = top + (bottom - top) * (row + 1) / rows
                for (column in 0 until columns) {
                    val x0 = left + (right - left) * column / columns
                    val x1 = left + (right - left) * (column + 1) / columns
                    if (over.any { it.left < x1 && it.right > x0 && it.top < y1 && it.bottom > y0 }) {
                        hidden[index * columns * rows + row * columns + column] = true
                    }
                }
            }
        }
        return hidden
    }

    /** Whether one of the app's own layers lies over the text areas now. */
    private fun overText(picture: Rect, look: List<OcrArea>): Boolean {
        val over = drawn()
        return over.isNotEmpty() && hiddenCells(picture, look, over).any { it }
    }

    /** The areas' look in a copy of the surface [bitmap] ([picture] in the surface's pixels). */
    private fun signature(bitmap: Bitmap, picture: Rect, look: List<OcrArea>): OcrSignature {
        val whole = pixels(bitmap)
        val scaleX = bitmap.width.toFloat() / frames.width.coerceAtLeast(1)
        val scaleY = bitmap.height.toFloat() / frames.height.coerceAtLeast(1)
        return OcrSignature.joined(look.map { area ->
            OcrSignature.of(
                whole,
                (picture.left + area.x * picture.width()) * scaleX / bitmap.width,
                (picture.top + area.y * picture.height()) * scaleY / bitmap.height,
                (picture.left + area.right * picture.width()) * scaleX / bitmap.width,
                (picture.top + area.bottom * picture.height()) * scaleY / bitmap.height
            )
        })
    }

    private fun pixels(bitmap: Bitmap): OcrPicture {
        val out = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(out, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        return OcrPicture(bitmap.width, bitmap.height, out)
    }

    // ---- Reading ----

    /** A copy of the whole picture, then [look] read in it. Main thread. */
    private fun copyAndRead(look: List<OcrArea>, asked: Boolean) {
        if (reading || stopped || look.isEmpty()) return
        val picture = frames.picture() ?: return
        reading = true
        // Where the game's sound is as the line is read: a card's audio from there.
        val audio = frames.audioPosition()
        frames.copy(FULL_WIDTH) { bitmap ->
            if (bitmap == null) {
                reading = false
                return@copy
            }
            worker.execute {
                try {
                    val read = read(bitmap, picture, look)
                    main.post {
                        if (backoff.answered()) say(activity.getString(R.string.ocr_back))
                        present(read, audio, asked)
                    }
                } catch (e: Exception) {
                    Log.w("Furoshiki", "OCR: ${e.message}")
                    main.post {
                        if (backoff.failed(SystemClock.uptimeMillis())) say(activity.getString(R.string.ocr_failed, e.message ?: e.javaClass.simpleName))
                        // What was not read is read once the engine answers again.
                        watch.forget()
                    }
                } finally {
                    bitmap.recycle()
                    reading = false
                }
            }
        }
    }

    /**
     * [look] read in [bitmap], a copy of the surface ([picture] in its pixels): the areas one after
     * the other, from the top, as one line, each character's place made a place in the game's picture.
     */
    fun read(bitmap: Bitmap, picture: Rect, look: List<OcrArea>): OcrRead {
        val scaleX = bitmap.width.toFloat() / frames.width.coerceAtLeast(1)
        val scaleY = bitmap.height.toFloat() / frames.height.coerceAtLeast(1)
        val left = picture.left * scaleX
        val top = picture.top * scaleY
        val width = picture.width() * scaleX
        val height = picture.height() * scaleY
        val engine = engine() ?: throw IllegalStateException(activity.getString(R.string.ocr_no_engine))
        val lines = ArrayList<OcrLine>()
        var name = ""
        for (area in look.sortedWith(compareBy({ it.y }, { it.x }))) {
            val x0 = (left + area.x * width).toInt().coerceIn(0, bitmap.width - 1)
            val y0 = (top + area.y * height).toInt().coerceIn(0, bitmap.height - 1)
            val x1 = (left + area.right * width).toInt().coerceIn(x0 + 1, bitmap.width)
            val y1 = (top + area.bottom * height).toInt().coerceIn(y0 + 1, bitmap.height)
            val part = Bitmap.createBitmap(bitmap, x0, y0, x1 - x0, y1 - y0)
            val read = try {
                when (engine) {
                    OcrEngine.DEVICE -> {
                        val pixels = pixels(part)
                        OcrText.clean(deviceEngine().read(pixels), pixels.width, pixels.height)
                    }
                    OcrEngine.SERVICE -> service().read(jpeg(part))
                    OcrEngine.LENS -> OcrText.clean(GoogleLens.read(png(part), part.width, part.height), part.width, part.height, "glens")
                }
            } finally {
                if (part !== bitmap) part.recycle()
            }
            name = read.engine
            // From the part's fractions to the picture's.
            val fx = (x0 - left) / width
            val fy = (y0 - top) / height
            val fw = (x1 - x0) / width
            val fh = (y1 - y0) / height
            for (line in read.lines) {
                lines.add(OcrLine(line.text, line.boxes?.map { floatArrayOf(fx + it[0] * fw, fy + it[1] * fh, it[2] * fw, it[3] * fh) }, line.upright))
            }
        }
        return OcrRead(lines, name)
    }

    /** The engine to read with now, or null when there is none. */
    fun engine(): OcrEngine? =
        OcrSettings.choose(settings.engine, OcrSettings.dev, serviceAddress().isNotEmpty(), deviceAvailable(activity))

    private fun serviceAddress(): String = settings.address.ifEmpty { Services.of(activity, "furochibi") }

    private fun service() = OcrService(serviceAddress(), settings.token)

    @Synchronized
    private fun deviceEngine(): MeikiOcr = device ?: deviceEngineFor(activity).also { device = it }

    private fun png(bitmap: Bitmap): ByteArray = ByteArrayOutputStream().also { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()

    private fun jpeg(bitmap: Bitmap): ByteArray = ByteArrayOutputStream().also { bitmap.compress(Bitmap.CompressFormat.JPEG, 92, it) }.toByteArray()

    // ---- To the reading session ----

    /** [read] as the session's newest line, and its place in the picture. Main thread. */
    private fun present(read: OcrRead, audio: Long, asked: Boolean) {
        val text = read.text.trim()
        if (text.isEmpty() || !OcrText.hasJapanese(text)) {
            if (asked) say(activity.getString(R.string.ocr_nothing))
            else if (placed) {
                // The text is gone from the area: nothing of it is pointed at any more.
                clearPlacement()
                lastText = ""
                onChanged()
            }
            return
        }
        // The same text again (a window shut and opened on it, a cursor that blinked, a character
        // read a little otherwise): not a new line. Its places are those of the line as it was
        // read, where the reading differs, so that each still lies on its own character.
        if (!asked && OcrText.sameText(text, lastText)) {
            place(if (text == lastText) read else lastRead ?: read)
            return
        }
        // Another text: the popup over the one before closes.
        if (lastText.isNotEmpty()) onChanged()
        lastText = text
        lastRead = read
        sequence++
        val meta = JSONObject().put(TextLine.META_SOURCE, TextLine.SOURCE_OCR).put("engine", read.engine)
        val line = JSONObject()
            .put("seq", sequence).put("frame", 0).put("audio", audio).put("channel", TextLine.CHANNEL_DIALOGUE)
            .put("speaker", "").put("text", text).put("ruby", JSONArray()).put("id", OcrText.sourceId(text))
            .put("meta", meta)
        val places = placesOf(read)
        if (places != null) line.put("window", WINDOW)
        Log.i("Furoshiki", "OCR: a line of ${text.codePointCount(0, text.length)} characters, ${read.boxes?.size ?: 0} places" + if (places == null) " (not placed)" else "")
        ReaderSession.onNativeEvent(line.toString())
        place(read)
    }

    /** Where the newest line's characters are, for a tap on the game's own text. */
    private fun place(read: OcrRead) {
        val boxes = placesOf(read) ?: return
        val glyphs = JSONArray()
        var left = Float.MAX_VALUE
        var top = Float.MAX_VALUE
        var right = 0f
        var bottom = 0f
        for (box in boxes) {
            glyphs.put(JSONArray(listOf(box[0] * SPACE, box[1] * SPACE, box[2] * SPACE, box[3] * SPACE).map { it.toDouble() }))
            left = minOf(left, box[0])
            top = minOf(top, box[1])
            right = maxOf(right, box[0] + box[2])
            bottom = maxOf(bottom, box[1] + box[3])
        }
        val layout = JSONObject().put("window", WINDOW).put("seq", sequence)
            .put("space", JSONArray(listOf(SPACE.toDouble(), SPACE.toDouble())))
            .put("box", JSONArray(listOf(left * SPACE, top * SPACE, (right - left) * SPACE, (bottom - top) * SPACE).map { it.toDouble() }))
            .put("glyphs", glyphs)
        ReaderSession.onNativeLayout(layout.toString())
        placed = true
    }

    /** Each character's place, when there is one for every character of the line as it is sent. */
    private fun placesOf(read: OcrRead): List<FloatArray>? {
        val text = read.text
        val boxes = read.boxes ?: return null
        return boxes.takeIf { text == text.trim() && it.size == text.codePointCount(0, text.length) }
    }

    /** The text read is no longer on the screen: nothing of it is tapped. */
    private fun clearPlacement() {
        if (!placed) return
        placed = false
        ReaderSession.onNativeLayout(JSONObject().put("window", WINDOW).put("seq", sequence).put("closed", true).toString())
    }

    private fun say(text: String) {
        if (!stopped) Toast.makeText(activity, text, Toast.LENGTH_LONG).show()
    }

    companion object {
        private const val LOOK_MS = 250L
        private const val LOOK_WIDTH = 480
        private const val FULL_WIDTH = 1920
        private const val NO_SCRIPT_AFTER_MS = 4000L
        private const val WINDOW = "ocr"

        /** The units the places are given in (ScreenLayout's space): fractions of the picture, finer. */
        private const val SPACE = 10000f

        /** Whether this build carries the engine of the device (its models: -PreaderOcrModels=false leaves them out). */
        fun deviceAvailable(context: android.content.Context): Boolean =
            runCatching { context.assets.list("ocr")?.toSet()?.containsAll(listOf("detect.onnx", "read.onnx")) == true }.getOrDefault(false)

        /** The engine of the device, its models from the APK. Not on the main thread: it takes a moment. */
        fun deviceEngineFor(context: android.content.Context): MeikiOcr {
            val assets = context.assets
            return MeikiOcr(
                assets.open("ocr/detect.onnx").use { it.readBytes() },
                assets.open("ocr/read.onnx").use { it.readBytes() },
                { runCatching { assets.open("ocr/read-upright.onnx").use { it.readBytes() } }.getOrNull() }
            )
        }
    }
}
