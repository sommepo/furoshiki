// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/**
 * Keys along the foot of the second display while a game is played (the maintainer, 2026-11-13:
 * "the option to have the back button hotkeys like pause stats, pause emu, guide button"): what
 * a computer's controller has on its back buttons, for a thumb. Words between hairlines, as the
 * page's keys are; one that is on is filled. The trackpad keeps the display above them.
 */
object LowerKeys {
    private const val KEY = "lower_keys"

    /**
     * A key: its word, whether it is on now (a switch), the word it has while on where that says
     * more than the fill does ("Resume" for "Pause"), and what a touch does.
     */
    class Key(
        val label: String, val on: (() -> Boolean)? = null, val labelOn: String? = null,
        /** What a screen reader says where the word on the key is short for it ("Menu": "Game menu"). */
        val says: String? = null,
        val act: () -> Unit,
    )

    fun wanted(context: Context): Boolean = context.getSharedPreferences("game", Context.MODE_PRIVATE).getBoolean(KEY, true)

    fun want(context: Context, on: Boolean) =
        context.getSharedPreferences("game", Context.MODE_PRIVATE).edit().putBoolean(KEY, on).apply()

    fun view(context: Context, keys: List<Key>): View {
        val density = context.resources.displayMetrics.density
        val main = Handler(Looper.getMainLooper())
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            isBaselineAligned = false
            setBackgroundColor(0xE6101A3A.toInt())
            val pad = (8 * density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        val views = ArrayList<Pair<Key, TextView>>()
        fun paint() {
            for ((key, view) in views) {
                val on = key.on?.invoke() == true
                // The word says the state too, not the fill alone.
                val word = (if (on) key.labelOn else null) ?: key.label
                if (view.text.toString() != word.uppercase()) view.text = word.uppercase()
                view.contentDescription = key.says ?: word
                view.setTextColor(if (on) 0xFF101A3A.toInt() else 0xFFECE5D3.toInt())
                view.background = GradientDrawable().apply {
                    setColor(if (on) 0xFFD9472B.toInt() else Color.TRANSPARENT)
                    setStroke(1, if (on) 0xFFD9472B.toInt() else 0x66ECE5D3)
                }
            }
        }
        for (key in keys) {
            val view = TextView(context).apply {
                text = key.label.uppercase()
                typeface = Typeface.MONOSPACE
                // 12sp where the key has room for it, smaller only as far as a word needs to fit
                // (a row of many keys on a narrow display): a word of its own stays on one line.
                letterSpacing = if (keys.size > 8) .04f else .08f
                gravity = Gravity.CENTER
                maxLines = if (' ' in key.label || key.labelOn?.contains(' ') == true) 2 else 1
                setAutoSizeTextTypeUniformWithConfiguration(9, 12, 1, android.util.TypedValue.COMPLEX_UNIT_SP)
                setOnClickListener {
                    key.act()
                    // What it switched is told a moment later (the statistics' through the session).
                    paint()
                    main.postDelayed({ paint() }, 250)
                }
            }
            views += key to view
            row.addView(view, LinearLayout.LayoutParams(0, (58 * density).toInt(), 1f).apply {
                marginStart = (3 * density).toInt()
                marginEnd = (3 * density).toInt()
            })
        }
        paint()
        return row
    }
}
