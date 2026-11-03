// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PPSSPP Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import org.readers.app.reader.Console
import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.util.TypedValue
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.preference.PreferenceManager

/**
 * The console's buttons for a game played by touch, where they are on its controller: the d-pad
 * as a cross at the left edge of the screen with the left shoulder buttons above it and SELECT
 * (or −) below, the face buttons as the console's own diamond at the right edge ([Console.TOUCH_FACE])
 * with the right shoulder buttons above and START (or +) below. Shown only while Play by touch
 * is on and such a game runs; the space between them is the game's. A finger held on a button
 * holds the game's button. A tab above each side tucks both sides away and brings them back;
 * that choice is kept (asked for on the Fold, 2026-10-16, where a game's own subset of buttons
 * had left a menu that could not be answered; laid out as on a controller since 2026-10-17).
 */
@SuppressLint("ViewConstructor")
class TouchButtons(
    context: Context,
    /** Presses (true) or releases (false) a button of the console, by its number. */
    private val press: (code: Int, down: Boolean) -> Unit
) : FrameLayout(context) {

    /** One of the console's buttons: its number for [press], what the button shows. */
    data class Button(val code: Int, val label: String, val colour: Int)

    private var shownNames: List<String> = emptyList()
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)
    private val leftSide = Side(atLeft = true)
    private val rightSide = Side(atLeft = false)

    /** One side of the controller: its tab, and under it what the tab tucks away. */
    private inner class Side(val atLeft: Boolean) {
        val root = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            gravity = if (atLeft) Gravity.START else Gravity.END
        }
        val tab = TextView(context)
        val body = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
        }

        init {
            tab.apply {
                tag = if (atLeft) TAB_LEFT else TAB
                gravity = Gravity.CENTER
                includeFontPadding = false
                setTextColor(TAB_INK)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
                typeface = Typeface.DEFAULT_BOLD
                background = GradientDrawable().apply {
                    cornerRadius = dp(TAB_DP / 2f).toFloat()
                    setColor(FILL)
                    setStroke(dp(1f), EDGE)
                }
                alpha = REST_ALPHA
                setOnClickListener { tucked = !tucked }
            }
            root.addView(tab, LinearLayout.LayoutParams(dp(TAB_DP * 1.4f), dp(TAB_DP)).apply {
                setMargins(0, 0, 0, dp(GAP_DP))
            })
            root.addView(body, LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
            addView(root, LayoutParams(
                LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT,
                (if (atLeft) Gravity.START else Gravity.END) or Gravity.CENTER_VERTICAL
            ).apply { if (atLeft) leftMargin = dp(EDGE_DP) else rightMargin = dp(EDGE_DP) })
        }

        /** Lays out these buttons: shoulders on top, the cross or diamond, the small ones under. */
        fun fill(names: List<String>, diamond: List<String>) {
            body.removeAllViews()
            root.visibility = if (names.isEmpty()) GONE else VISIBLE
            if (names.isEmpty()) return
            val shoulders = names.filter { SHOULDER.matches(it) }
            val inDiamond = diamond.filter { it in names }
            val small = names.filter { it !in shoulders && it !in inDiamond }
            if (shoulders.isNotEmpty()) body.addView(row(shoulders), wrap(bottom = GAP_DP))
            if (inDiamond.isNotEmpty()) body.addView(cross(diamond, names), wrap())
            if (small.isNotEmpty()) body.addView(row(small), wrap(top = GAP_DP))
        }

        fun showTucked(away: Boolean) {
            body.visibility = if (away) GONE else VISIBLE
            // The arrow points the way the buttons go: out from the edge, or back to it.
            tab.text = if (away == atLeft) "›" else "‹"
            tab.contentDescription = if (away) "Show the buttons" else "Hide the buttons"
        }
    }

    init {
        visibility = GONE
        showTucked()
    }

    /** The buttons are tucked away behind their tabs (kept between starts). */
    var tucked: Boolean
        get() = preferences.getBoolean(KEY_TUCKED, false)
        set(value) {
            preferences.edit().putBoolean(KEY_TUCKED, value).apply()
            showTucked()
        }

    private fun showTucked() {
        val away = tucked
        leftSide.showTucked(away)
        rightSide.showTucked(away)
    }

    /** Shows the buttons of these names (unknown names are passed over), or none. */
    fun show(names: List<String>) {
        val known = names.filter { it in BUTTONS }
        if (known == shownNames) return
        shownNames = known
        val (left, right) = known.partition { it in LEFT }
        leftSide.fill(left, DPAD)
        rightSide.fill(right, FACE)
        visibility = if (known.isEmpty()) GONE else VISIBLE
    }

    /** The names shown, for tests (also while tucked away). */
    val names: List<String> get() = shownNames

    /** The buttons can be seen and touched (shown, and not tucked away). */
    val buttonsShown: Boolean
        get() = visibility == VISIBLE && listOf(leftSide, rightSide).any {
            it.root.visibility == VISIBLE && it.body.visibility == VISIBLE
        }

    /**
     * Four buttons where a cross (or diamond) has them, `ways` naming the top, right, bottom and
     * left one; a way without a shown button is left empty, so each keeps its place.
     */
    private fun cross(ways: List<String>, shown: List<String>): View {
        val cell = dp(SIZE_DP) + dp(CROSS_GAP_DP)
        val box = FrameLayout(context)
        val places = listOf(1 to 0, 2 to 1, 1 to 2, 0 to 1)
        for ((name, place) in ways.zip(places)) {
            if (name !in shown) continue
            box.addView(round(name, BUTTONS.getValue(name)), LayoutParams(dp(SIZE_DP), dp(SIZE_DP)).apply {
                leftMargin = place.first * cell
                topMargin = place.second * cell
            })
        }
        box.minimumWidth = 3 * cell - dp(CROSS_GAP_DP)
        box.minimumHeight = 3 * cell - dp(CROSS_GAP_DP)
        return box
    }

    /** Small buttons side by side (shoulders, START and SELECT). */
    private fun row(names: List<String>) = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        for (name in names) {
            addView(pill(name, BUTTONS.getValue(name)), LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, dp(PILL_DP)).apply {
                setMargins(dp(GAP_DP / 2), 0, dp(GAP_DP / 2), 0)
            })
        }
    }

    private fun wrap(top: Float = 0f, bottom: Float = 0f) =
        LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(top)
            bottomMargin = dp(bottom)
        }

    private fun round(name: String, button: Button) = key(name, button).apply {
        setTextSize(TypedValue.COMPLEX_UNIT_SP, if (button.label.length > 1) 13f else 20f)
        background = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(FILL)
            setStroke(dp(1.5f), EDGE)
        }
    }

    private fun pill(name: String, button: Button) = key(name, button).apply {
        setTextSize(TypedValue.COMPLEX_UNIT_SP, if (button.label.length > 2) 9f else 12f)
        minWidth = dp(PILL_DP * 1.6f)
        setPadding(dp(8f), 0, dp(8f), 0)
        background = GradientDrawable().apply {
            cornerRadius = dp(PILL_DP / 2f).toFloat()
            setColor(FILL)
            setStroke(dp(1.5f), EDGE)
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun key(name: String, button: Button) = TextView(context).apply {
        text = button.label
        tag = name
        contentDescription = name
        gravity = Gravity.CENTER
        includeFontPadding = false
        setTextColor(button.colour)
        typeface = Typeface.DEFAULT_BOLD
        alpha = REST_ALPHA
        setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    view.alpha = 1f
                    press(button.code, true)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    view.alpha = REST_ALPHA
                    press(button.code, false)
                }
            }
            true
        }
    }

    private fun dp(value: Float) =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics).toInt()

    companion object {
        private const val SIZE_DP = 40f
        private const val CROSS_GAP_DP = 2f
        private const val PILL_DP = 24f
        private const val GAP_DP = 8f
        private const val TAB_DP = 26f
        private const val EDGE_DP = 10f
        private const val REST_ALPHA = 0.55f
        /** The buttons' ground and edge, the console's ([ReaderStyle]). */
        private const val FILL = ReaderStyle.TOUCH_FILL
        private const val EDGE = ReaderStyle.TOUCH_EDGE

        /** The preference that keeps the buttons tucked away. */
        const val KEY_TUCKED = "reader_touch_buttons_tucked"

        /** The tabs' tags, for tests: the right side's, and the left side's. */
        const val TAB = "tab"
        const val TAB_LEFT = "tab-left"

        private const val TAB_INK = 0xFFE8ECF2.toInt()

        /** The d-pad's buttons: top, right, bottom, left. */
        private val DPAD = listOf("up", "right", "down", "left")

        /** Shoulder buttons (L, R, L1, R2, ZL…): above the cross or the diamond. */
        private val SHOULDER = Regex("z?[lr][12]?")

        /** Every button, as they are shown for a game played by touch (the console's: [Console]). */
        val ALL: List<String> get() = Console.TOUCH_ALL

        /** The buttons on the left side (the rest are on the right). */
        private val LEFT: Set<String> get() = Console.TOUCH_LEFT

        /** The face buttons where the console's controller has them: top, right, bottom, left. */
        private val FACE: List<String> get() = Console.TOUCH_FACE

        /** The console's buttons by the names scripts use ([Console.TOUCH_BUTTONS]). */
        val BUTTONS: Map<String, Button> get() = Console.TOUCH_BUTTONS
    }
}
