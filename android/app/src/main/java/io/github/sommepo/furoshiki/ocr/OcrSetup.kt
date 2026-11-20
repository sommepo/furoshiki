// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView
import io.github.sommepo.furoshiki.R
import kotlin.concurrent.thread
import kotlin.math.abs

/**
 * Setting a game's text areas up, over the game held still: its picture as it was, a box dragged
 * over the text with a finger (more boxes for a name or a second window; a box read only when
 * asked for, for a menu), what OCR reads in them tried at once, and kept for the game by its ID.
 * The keys along the foot take a controller's d-pad and A: *Lower third* gives a box without a
 * finger.
 */
class OcrSetup(
    private val activity: Activity,
    /** The game's picture as it stood, a copy of the whole surface. */
    private val frame: Bitmap,
    /** Where the game's picture is in [frame], in its pixels. */
    private val picture: Rect,
    start: OcrGame,
    /** Reads [areas] in [frame] (off the main thread); throws with what went wrong. */
    private val read: (List<OcrArea>) -> OcrRead,
    private val onSave: (OcrGame) -> Unit,
    private val onClosed: () -> Unit,
) {
    private val density = activity.resources.displayMetrics.density
    private val areas = start.areas.map { it }.toMutableList()
    private var on = start.on || start.areas.isEmpty()
    private var chosen = if (areas.isEmpty()) -1 else 0
    private lateinit var board: Board
    private lateinit var said: TextView
    private lateinit var onKey: TextView
    private lateinit var askKey: TextView

    fun show() {
        val dialog = Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
        val root = FrameLayout(activity)
        root.setBackgroundColor(Color.BLACK)
        board = Board(activity)
        root.addView(board, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        val head = TextView(activity).apply {
            text = activity.getString(R.string.ocr_setup_hint)
            setTextColor(TEXT)
            textSize = 14f
            setPadding(dp(12), dp(8), dp(12), dp(8))
            background = GradientDrawable().apply { setColor(0xCC101A3A.toInt()); cornerRadius = dp(8).toFloat() }
        }
        root.addView(head, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(10)
        })

        val foot = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0xE6101A3A.toInt())
            setPadding(dp(8), dp(6), dp(8), dp(8))
        }
        said = TextView(activity).apply {
            setTextColor(TEXT)
            textSize = 16f
            setPadding(dp(6), dp(2), dp(6), dp(6))
            text = activity.getString(R.string.ocr_setup_try_hint)
            maxLines = 3
        }
        foot.addView(said)
        val keys = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        // Save and Cancel first: the row scrolls sideways on a narrow screen, and the two that
        // leave the screen must never be the ones out of sight.
        key(keys, activity.getString(R.string.ocr_setup_save)) {
            onSave(OcrGame(on, areas.map { it.fitted() }))
            dialog.dismiss()
        }
        key(keys, activity.getString(android.R.string.cancel)) { dialog.dismiss() }
        onKey = key(keys, "") { on = !on; paint() }
        key(keys, activity.getString(R.string.ocr_setup_try)) { tryIt() }
        key(keys, activity.getString(R.string.ocr_setup_lower_third)) {
            if (areas.size < OcrAreas.MOST) {
                areas.add(OcrArea.LOWER_THIRD)
                chosen = areas.size - 1
                paint()
            }
        }
        askKey = key(keys, activity.getString(R.string.ocr_setup_on_request)) {
            if (chosen in areas.indices) {
                areas[chosen] = areas[chosen].copy(ask = !areas[chosen].ask)
                paint()
            }
        }
        key(keys, activity.getString(R.string.ocr_setup_next_box)) {
            if (areas.isNotEmpty()) {
                chosen = (chosen + 1) % areas.size
                paint()
            }
        }
        key(keys, activity.getString(R.string.ocr_setup_remove)) {
            if (chosen in areas.indices) {
                areas.removeAt(chosen)
                chosen = areas.size - 1
                paint()
            }
        }
        foot.addView(HorizontalScrollView(activity).apply {
            // The bar stays in sight: it is what says there are more keys to the side.
            isHorizontalScrollBarEnabled = true
            isScrollbarFadingEnabled = false
            addView(keys)
        })
        root.addView(foot, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM))

        dialog.setContentView(root)
        dialog.setOnDismissListener {
            frame.recycle()
            onClosed()
        }
        dialog.show()
        paint()
        onKey.requestFocus()
    }

    private fun key(row: LinearLayout, label: String, act: () -> Unit): TextView {
        val view = TextView(activity).apply {
            text = label.uppercase()
            typeface = Typeface.MONOSPACE
            textSize = 13f
            letterSpacing = .08f
            gravity = Gravity.CENTER
            setTextColor(TEXT)
            setPadding(dp(14), 0, dp(14), 0)
            isFocusable = true
            isClickable = true
            setOnClickListener { act() }
            setOnFocusChangeListener { _, _ -> paintKey(this) }
        }
        row.addView(view, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(48)).apply { marginEnd = dp(6) })
        paintKey(view)
        return view
    }

    private fun paintKey(view: TextView) {
        val focused = view.isFocused
        view.background = GradientDrawable().apply {
            setColor(if (focused) 0x55ECE5D3 else Color.TRANSPARENT)
            setStroke(1, 0x66ECE5D3)
        }
    }

    private fun paint() {
        onKey.text = activity.getString(if (on) R.string.ocr_setup_read_on else R.string.ocr_setup_read_off).uppercase()
        val box = areas.getOrNull(chosen)
        askKey.alpha = if (box == null) .4f else 1f
        askKey.text = activity.getString(if (box?.ask == true) R.string.ocr_setup_on_request_on else R.string.ocr_setup_on_request).uppercase()
        board.invalidate()
    }

    private fun tryIt() {
        val box = areas.getOrNull(chosen)
        val look = when {
            box != null -> listOf(box.fitted())
            areas.isNotEmpty() -> areas.filter { !it.ask }.ifEmpty { areas }
            else -> listOf(OcrArea.LOWER_THIRD)
        }
        said.text = activity.getString(R.string.ocr_setup_reading)
        thread(name = "OcrTry") {
            val answer = try {
                read(look).text.ifEmpty { activity.getString(R.string.ocr_nothing) }
            } catch (e: Exception) {
                activity.getString(R.string.ocr_failed, e.message ?: e.javaClass.simpleName)
            }
            activity.runOnUiThread { said.text = answer }
        }
    }

    private fun dp(value: Int) = (value * density).toInt()

    /** The picture held still and the boxes over it, drawn and moved by a finger. */
    private inner class Board(context: Context) : View(context) {
        private val shown = RectF()
        private val bitmapPaint = Paint(Paint.FILTER_BITMAP_FLAG)
        private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 2 * density }
        private val fill = Paint().apply { style = Paint.Style.FILL }
        private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = 12 * density; typeface = Typeface.MONOSPACE }
        private var mode = 0 // 0 nothing, 1 drawing, 2 moving, 3 resizing
        private var downX = 0f
        private var downY = 0f
        private var before: OcrArea? = null

        /** The game's picture on this view. */
        private fun pictureOnView(): RectF {
            val scale = minOf(width.toFloat() / frame.width, height.toFloat() / frame.height)
            val w = frame.width * scale
            val h = frame.height * scale
            shown.set((width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2)
            return RectF(
                shown.left + picture.left * scale, shown.top + picture.top * scale,
                shown.left + picture.right * scale, shown.top + picture.bottom * scale
            )
        }

        private fun onView(area: OcrArea, at: RectF) =
            RectF(at.left + area.x * at.width(), at.top + area.y * at.height(), at.left + area.right * at.width(), at.top + area.bottom * at.height())

        override fun onDraw(canvas: Canvas) {
            val at = pictureOnView()
            canvas.drawBitmap(frame, null, shown, bitmapPaint)
            // The picture outside the boxes a little dimmed, so the boxes stand out.
            fill.color = 0x55000000
            canvas.drawRect(at, fill)
            val drawn = if (areas.isEmpty()) listOf(OcrArea.LOWER_THIRD) else areas
            drawn.forEachIndexed { index, area ->
                val box = onView(area, at)
                val isChosen = index == chosen
                val colour = if (area.ask) ASK else if (isChosen) CHOSEN else BOX
                line.color = colour
                line.pathEffect = if (areas.isEmpty()) DashPathEffect(floatArrayOf(10 * density, 6 * density), 0f) else null
                canvas.drawRect(box, line)
                if (isChosen) {
                    fill.color = colour
                    val r = HANDLE * density
                    canvas.drawRect(box.right - r, box.bottom - r, box.right + r * .4f, box.bottom + r * .4f, fill)
                }
                label.color = colour
                val name = when {
                    areas.isEmpty() -> context.getString(R.string.ocr_setup_default_box)
                    area.ask -> context.getString(R.string.ocr_setup_on_request)
                    else -> context.getString(R.string.ocr_setup_box, index + 1)
                }
                canvas.drawText(name, box.left + 4 * density, box.top - 5 * density, label)
            }
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            val at = pictureOnView()
            if (at.width() <= 0f || at.height() <= 0f) return false
            val x = ((event.x - at.left) / at.width()).coerceIn(0f, 1f)
            val y = ((event.y - at.top) / at.height()).coerceIn(0f, 1f)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downX = x
                    downY = y
                    val current = areas.getOrNull(chosen)
                    val box = current?.let { onView(it, at) }
                    val reach = HANDLE * 2 * density
                    mode = when {
                        box != null && abs(event.x - box.right) < reach && abs(event.y - box.bottom) < reach -> 3
                        else -> {
                            val inside = areas.indices.lastOrNull { onView(areas[it], at).contains(event.x, event.y) }
                            if (inside != null) {
                                chosen = inside
                                2
                            } else if (areas.size < OcrAreas.MOST) {
                                areas.add(OcrArea(x, y, 0f, 0f))
                                chosen = areas.size - 1
                                1
                            } else {
                                0
                            }
                        }
                    }
                    before = areas.getOrNull(chosen)
                    paint()
                }
                MotionEvent.ACTION_MOVE -> {
                    val was = before ?: return true
                    areas[chosen] = when (mode) {
                        1 -> OcrArea(minOf(downX, x), minOf(downY, y), abs(x - downX), abs(y - downY), was.ask)
                        2 -> was.copy(x = (was.x + x - downX).coerceIn(0f, 1f - was.width), y = (was.y + y - downY).coerceIn(0f, 1f - was.height))
                        3 -> was.copy(width = (x - was.x).coerceAtLeast(OcrArea.SMALLEST), height = (y - was.y).coerceAtLeast(OcrArea.SMALLEST))
                        else -> return true
                    }
                    invalidate()
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    val box = areas.getOrNull(chosen)
                    // A tap on nothing draws no box.
                    if (mode == 1 && box != null && (box.width < OcrArea.SMALLEST || box.height < OcrArea.SMALLEST)) {
                        areas.removeAt(chosen)
                        chosen = areas.size - 1
                    } else if (box != null) {
                        areas[chosen] = box.fitted()
                    }
                    mode = 0
                    before = null
                    paint()
                }
            }
            return true
        }
    }

    private companion object {
        const val TEXT = 0xFFECE5D3.toInt()
        const val BOX = 0xFFECE5D3.toInt()
        const val CHOSEN = 0xFFD9472B.toInt()
        const val ASK = 0xFF6FA8DC.toInt()
        const val HANDLE = 9
    }
}
