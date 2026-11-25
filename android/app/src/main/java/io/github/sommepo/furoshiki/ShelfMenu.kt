// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Dialog
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * Every menu and question over a game, drawn as the home page draws a list (front.css, #pick): a
 * sheet in the theme's raised colour with the ink rule along its top, the title in small tracked
 * capitals, rows between hairlines, and the row in hand marked by the accent bar the page's rows
 * have. One builder for all of them (the game menu, the Quick menu, save slots, patches, discs,
 * "Quit game?"), so none looks like another app's dialog. The theme is the page's
 * ([ShelfLook]): Kon, Mono, Paper or Solarized.
 *
 * A finger taps a row; a controller walks the rows with the d-pad, A (or Enter) does the row in
 * hand, B or Back shuts the menu. A row that [keep]s the menu open is a switch: it shows its
 * value at the right and changes it in place.
 */
class ShelfMenu(private val context: Context) {
    private class Row(val label: CharSequence, val value: (() -> String)?, val keep: Boolean, val act: () -> Unit)

    private var title: CharSequence = ""
    private var message: CharSequence = ""
    private var content: View? = null
    private val rows = ArrayList<Row>()
    private var closed: ((() -> Unit)?) -> Unit = { picked -> picked?.invoke() }

    fun title(text: CharSequence) = apply { title = text }

    /** A sentence under the title (a question's second line). */
    fun message(text: CharSequence) = apply { message = text }

    /** A view of the caller's between the message and the rows (a field to type in). */
    fun view(view: View) = apply { content = view }

    /** A row; [value] is said at its right in small capitals (a slot's date). */
    fun row(label: CharSequence, value: String = "", act: () -> Unit) = apply {
        rows += Row(label, if (value.isEmpty()) null else ({ value }), false, act)
    }

    /** A row that stays: [act] changes something and [value] says what it is now. */
    fun keep(label: CharSequence, value: () -> String, act: () -> Unit) = apply { rows += Row(label, value, true, act) }

    /**
     * What happens as the menu shuts, with what the chosen row does (null: none was chosen). By
     * itself the row's action is done; a caller that holds the game still does its own first.
     */
    fun onClosed(block: (picked: (() -> Unit)?) -> Unit) = apply { closed = block }

    /** Shows it, the row [at] in hand. */
    fun show(at: Int = 0): Dialog {
        val look = ShelfLook.of(context)
        val density = context.resources.displayMetrics.density
        fun dp(value: Float) = (value * density).toInt()
        var picked: (() -> Unit)? = null
        val views = ArrayList<View>()
        val marks = ArrayList<(Boolean) -> Unit>()
        var inHand = at.coerceIn(0, maxOf(rows.lastIndex, 0))
        fun mark(index: Int) {
            inHand = index.coerceIn(0, maxOf(rows.lastIndex, 0))
            marks.forEachIndexed { i, set -> set(i == inHand) }
            views.getOrNull(inHand)?.let { it.requestRectangleOnScreen(android.graphics.Rect(0, 0, it.width, it.height), false) }
        }
        // Intercept controller keys before ViewRootImpl's touch-mode exit / focus search can
        // swallow the first press or paint a full-screen focus highlight over the dialog.
        val dialog = object : Dialog(context, android.R.style.Theme_Translucent_NoTitleBar) {
            override fun dispatchKeyEvent(event: KeyEvent): Boolean {
                when (event.keyCode) {
                    KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN -> {
                        if (event.action == KeyEvent.ACTION_DOWN) mark(inHand + if (event.keyCode == KeyEvent.KEYCODE_DPAD_UP) -1 else 1)
                        return true
                    }
                    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> {
                        if (event.action == KeyEvent.ACTION_UP) views.getOrNull(inHand)?.performClick()
                        return true
                    }
                    KeyEvent.KEYCODE_BUTTON_B -> {
                        if (event.action == KeyEvent.ACTION_UP) dismiss()
                        return true
                    }
                }
                return super.dispatchKeyEvent(event)
            }
        }
        fun noFocusHighlight(view: View) {
            view.isFocusable = false
            view.isFocusableInTouchMode = false
            view.defaultFocusHighlightEnabled = false
        }

        val box = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            // The sheet: raised, a hairline all round, and the ink rule along its top.
            background = LayerDrawable(
                arrayOf(
                    GradientDrawable().apply { setColor(look.raised); setStroke(1, ShelfLook.over(look.rule, look.raised)) },
                    GradientDrawable().apply { setColor(look.ink) },
                )
            ).apply { setLayerGravity(1, Gravity.TOP); setLayerHeight(1, dp(2f)) }
            setPadding(0, dp(2f), 0, 0)
            isClickable = true // a tap on the sheet is not a tap beside it
            noFocusHighlight(this)
        }
        fun hairline() = View(context).apply {
            setBackgroundColor(look.rule)
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 1)
        }
        if (title.isNotEmpty()) {
            box.addView(TextView(context).apply {
                text = title.toString().uppercase()
                typeface = ShelfLook.display(context)
                textSize = 12f
                letterSpacing = .14f
                setTextColor(look.muted)
                setPadding(dp(18f), dp(14f), dp(18f), dp(14f))
            })
            box.addView(hairline())
        }
        if (message.isNotEmpty()) {
            box.addView(TextView(context).apply {
                text = message
                typeface = ShelfLook.text(context)
                textSize = 15f
                setLineSpacing(0f, 1.3f)
                setTextColor(look.ink)
                setPadding(dp(18f), dp(14f), dp(18f), dp(14f))
            })
            box.addView(hairline())
        }
        content?.let { view ->
            box.addView(FrameLayout(context).apply { setPadding(dp(18f), dp(10f), dp(18f), dp(10f)); addView(view) })
            box.addView(hairline())
        }

        val list = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val faint = Color.argb(158, Color.red(look.ink), Color.green(look.ink), Color.blue(look.ink))
        rows.forEachIndexed { index, row ->
            val bar = View(context).apply { setBackgroundColor(look.accent); visibility = View.INVISIBLE }
            val label = TextView(context).apply {
                text = row.label
                typeface = ShelfLook.text(context)
                textSize = 16f
                setTextColor(faint)
            }
            val value = TextView(context).apply {
                typeface = ShelfLook.mono(context)
                textSize = 12f
                letterSpacing = .08f
                setTextColor(look.muted)
                text = row.value?.invoke()?.uppercase().orEmpty()
            }
            val line = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                minimumHeight = dp(48f)
                isClickable = true
                contentDescription = row.label.toString() + (row.value?.invoke()?.let { ", $it" } ?: "")
                addView(bar, LinearLayout.LayoutParams(dp(3f), ViewGroup.LayoutParams.MATCH_PARENT).apply { topMargin = 1; bottomMargin = 1 })
                addView(label, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { marginStart = dp(15f); marginEnd = dp(12f); topMargin = dp(8f); bottomMargin = dp(8f) })
                addView(value, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(18f) })
                setOnClickListener {
                    mark(index)
                    if (row.keep) {
                        row.act()
                        val now = row.value?.invoke().orEmpty()
                        value.text = now.uppercase()
                        contentDescription = row.label.toString() + ", " + now
                    } else {
                        picked = row.act
                        dialog.dismiss()
                    }
                }
                noFocusHighlight(this)
            }
            views += line
            marks += { on ->
                bar.visibility = if (on) View.VISIBLE else View.INVISIBLE
                label.setTextColor(if (on) look.ink else faint)
            }
            list.addView(line)
            if (index < rows.lastIndex) list.addView(hairline())
        }
        box.addView(ScrollView(context).apply {
            isVerticalScrollBarEnabled = false
            noFocusHighlight(this)
            addView(list)
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))

        val metrics = context.resources.displayMetrics
        val root = FrameLayout(context).apply {
            // The page behind, dimmed in the theme's ground; a tap beside the sheet shuts it.
            setBackgroundColor(Color.argb(209, Color.red(look.ground), Color.green(look.ground), Color.blue(look.ground)))
            setOnClickListener { dialog.dismiss() }
            noFocusHighlight(this)
            setPadding(dp(16f), dp(16f), dp(16f), dp(16f))
            addView(
                box,
                FrameLayout.LayoutParams(minOf(dp(440f), metrics.widthPixels - dp(32f)), ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER)
            )
        }
        dialog.setContentView(root)
        dialog.window?.let { window ->
            window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            window.setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT)
            // Over a game the system's bars stay away, as they are over the game itself.
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }
        dialog.setOnDismissListener { closed(picked) }
        dialog.show()
        mark(inHand)
        return dialog
    }
}
