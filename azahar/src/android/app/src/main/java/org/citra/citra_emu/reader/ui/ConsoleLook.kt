// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader.ui

import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PixelFormat
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.drawable.Drawable
import android.view.View
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import org.citra.citra_emu.R

/**
 * The console's look for the word popup, the question about a card's audio and the pointer:
 * the same as the desktop app's (its `reader_overlay.cpp`, "The console's look").
 * Here: the 3DS's lower screen: grey pinstripes, a dark edge, pale boxes; the pointer is a
 * touch of the stylus, a ring and a fainter one spreading from it.
 */
@Suppress("unused")
object ConsoleLook {
    private fun argb(a: Int, r: Int, g: Int, b: Int) = (a shl 24) or (r shl 16) or (g shl 8) or b

    private abstract class Plain : Drawable() {
        protected val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        override fun setAlpha(alpha: Int) = Unit

        override fun setColorFilter(filter: ColorFilter?) = Unit

        @Deprecated("Deprecated in Java")
        override fun getOpacity() = PixelFormat.TRANSLUCENT
    }

    /** The popup's ground and its edge. */
    fun ground(density: Float): Drawable = object : Plain() {
        override fun draw(canvas: Canvas) {
            val area = RectF(bounds)
            val one = max(1f, density)
            // the lower screen's pinstripes
            paint.style = Paint.Style.FILL
            paint.color = argb(255, 0x4A, 0x4F, 0x55)
            canvas.drawRect(area, paint)
            paint.color = argb(255, 0x42, 0x47, 0x4D)
            var y = area.top + 2 * one
            while (y < area.bottom) {
                canvas.drawRect(area.left, y, area.right, min(area.bottom, y + 2 * one), paint)
                y += 4 * one
            }
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 2 * one
            paint.color = ReaderStyle.POPUP_EDGE
            canvas.drawRect(area.left + one, area.top + one, area.right - one, area.bottom - one, paint)
            paint.style = Paint.Style.FILL
            paint.color = argb(64, 255, 255, 255)
            canvas.drawRect(area.left + 2 * one, area.top + 2 * one, area.right - 2 * one, area.top + 3 * one, paint)
        }
    }

    /** The rule between two entries. */
    fun rule(density: Float): Drawable = object : Plain() {
        override fun getIntrinsicHeight() = max(2, (2 * density).toInt())

        override fun draw(canvas: Canvas) {
            val area = RectF(bounds)
            val one = max(1f, density)
            paint.style = Paint.Style.FILL
            paint.color = argb(255, 0x23, 0x26, 0x2A)
            canvas.drawRect(area.left, area.top, area.right, area.top + area.height() / 2, paint)
            paint.color = argb(46, 255, 255, 255)
            canvas.drawRect(area.left, area.top + area.height() / 2, area.right, area.bottom, paint)
        }
    }

    /**
     * What lies behind an entry, [head] being its headword's line and [first] true for the
     * entry at the top; null where the look has nothing there.
     */
    fun entry(density: Float, first: Boolean, head: View): Drawable? = null

    /** The pointer, [size] across, pointing at its own centre. */
    fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float) {
        val style = paint.style
        val width = paint.strokeWidth
        val cap = paint.strokeCap
        val join = paint.strokeJoin
        // a touch of the stylus: a ring, and a fainter one spreading from it
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = size * 0.05f
        paint.color = argb(110, 255, 255, 255)
        canvas.drawCircle(x, y, size * 0.44f, paint)
        paint.strokeWidth = size * 0.11f
        paint.color = argb(255, 0x1D, 0x20, 0x24)
        canvas.drawCircle(x, y, size * 0.24f, paint)
        paint.strokeWidth = size * 0.05f
        paint.color = argb(255, 255, 255, 255)
        canvas.drawCircle(x, y, size * 0.24f, paint)
        paint.style = Paint.Style.FILL
        canvas.drawCircle(x, y, size * 0.06f, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = size * 0.02f
        paint.color = argb(255, 0x1D, 0x20, 0x24)
        canvas.drawCircle(x, y, size * 0.06f, paint)
        paint.shader = null
        paint.style = style
        paint.strokeWidth = width
        paint.strokeCap = cap
        paint.strokeJoin = join
    }

    /**
     * The theme of the reader's own pages ([ReaderScreen]), set before they are made: the app's
     * own, as the user chose it (with the 3DS's colours in this build).
     */
    fun theme(activity: android.app.Activity) {
        try {
            org.citra.citra_emu.utils.ThemeUtil.setTheme(activity as androidx.appcompat.app.AppCompatActivity)
        } catch (_: RuntimeException) {
            // No application of the emulator's behind this (off-device tests): its theme alone.
            activity.setTheme(R.style.Theme_Reader)
        }
    }

    /**
     * Dresses one of the reader's pages ([ReaderScreen]: its [root] holds the [toolbar] and the
     * page) in the manner of the 3DS's own settings: a grey strip with the page's name, the page
     * on the pale ground, and a strip with a wide round Back button at the bottom. The arrow
     * leaves the page ([back]); the Back button goes where the system's Back goes.
     */
    fun dressScreen(activity: android.app.Activity, root: android.widget.LinearLayout,
                    toolbar: com.google.android.material.appbar.MaterialToolbar, back: () -> Unit) {
        val color = { id: Int -> androidx.core.content.ContextCompat.getColor(activity, id) }
        val density = activity.resources.displayMetrics.density
        fun dp(value: Float) = (value * density).toInt()
        val ink = color(R.color.reader_3ds_text)
        val strip = color(R.color.reader_3ds_bar)
        root.background = android.graphics.drawable.GradientDrawable(
            android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
            intArrayOf(color(R.color.reader_3ds_background_top), color(R.color.reader_3ds_background_bottom))
        )
        toolbar.setTitleTextColor(ink)
        toolbar.setBackgroundColor(strip)
        toolbar.setNavigationIcon(R.drawable.ic_back)
        toolbar.navigationIcon?.setTint(ink)
        val foot = android.widget.LinearLayout(activity).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
            gravity = android.view.Gravity.CENTER_VERTICAL
            setBackgroundColor(strip)
            setPadding(dp(14f), dp(6f), dp(14f), dp(6f))
        }
        foot.addView(
            android.widget.TextView(activity).apply {
                setText(R.string.reader_3ds_back)
                setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 15f)
                setTextColor(ink)
                gravity = android.view.Gravity.CENTER
                background = android.graphics.drawable.GradientDrawable(
                    android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                    intArrayOf(color(R.color.reader_3ds_open_top), color(R.color.reader_3ds_open_bottom))
                ).apply {
                    cornerRadius = dp(20f).toFloat()
                    setStroke(dp(1.5f), color(R.color.reader_3ds_bubble_edge))
                }
                isClickable = true
                isFocusable = true
                setOnClickListener {
                    (activity as? androidx.activity.ComponentActivity)?.onBackPressedDispatcher?.onBackPressed() ?: back()
                }
            },
            android.widget.LinearLayout.LayoutParams(dp(132f), dp(38f))
        )
        root.addView(foot, android.widget.LinearLayout.LayoutParams(
            android.widget.LinearLayout.LayoutParams.MATCH_PARENT, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT))
    }

}
