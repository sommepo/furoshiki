// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.os.SystemClock
import android.util.TypedValue
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import org.readers.app.R

/**
 * What a card asks before it is made, in a game whose cards ask about their audio: whether it
 * gets what the game played during its line. Three boxed buttons with signs and no words, in
 * the dictionary popup's look: ▶ listens to the clip (■ while it plays), a speaker with a tick
 * makes the card with it, a speaker with a cross without it. What each does is its content
 * description, for a screen reader.
 */
@SuppressLint("ViewConstructor")
class AudioPromptView(context: Context) : LinearLayout(context) {
    var onListen: (() -> Unit)? = null
    var onKeep: (() -> Unit)? = null
    var onDrop: (() -> Unit)? = null

    private val listen = button(R.drawable.ic_reader_audio_play, R.string.reader_audio_listen, ReaderStyle.POPUP_BUTTON_TEXT) {
        onListen?.invoke()
    }
    private val keep = button(R.drawable.ic_reader_audio_keep, R.string.reader_audio_keep, ReaderStyle.POPUP_BUTTON_KEEP) {
        onKeep?.invoke()
    }
    private val drop = button(R.drawable.ic_reader_audio_drop, R.string.reader_audio_drop, ReaderStyle.POPUP_BUTTON_TEXT) {
        onDrop?.invoke()
    }
    private var shownAt = 0L

    init {
        orientation = HORIZONTAL
        background = ConsoleLook.ground(resources.displayMetrics.density)
        elevation = dp(8f)
        isClickable = true // taps between the buttons answer nothing
        contentDescription = context.getString(R.string.reader_audio_question)
        for (button in listOf(listen, keep, drop)) {
            addView(button, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        }
        prepare(100)
    }

    /**
     * Makes the question ready to be shown, its buttons at [scale] percent of the normal size
     * (the dictionary popup's text size setting).
     */
    fun prepare(scale: Int) {
        val factor = scale.coerceIn(50, 250) / 100f
        val pad = dp(5f * factor).toInt()
        setPadding(pad, pad, pad, pad)
        for ((index, button) in listOf(listen, keep, drop).withIndex()) {
            val params = button.layoutParams as LayoutParams
            params.width = dp(BUTTON_WIDTH_DP * factor).toInt()
            params.height = dp(BUTTON_HEIGHT_DP * factor).toInt()
            params.marginStart = if (index == 0) 0 else dp(4f * factor).toInt()
            button.layoutParams = params
            val inset = dp(6f * factor).toInt()
            button.setPadding(inset, inset, inset, inset)
        }
        setPlaying(false)
        shownAt = SystemClock.uptimeMillis()
    }

    /** While the clip plays, the first button stops it. */
    fun setPlaying(playing: Boolean) {
        listen.setImageResource(if (playing) R.drawable.ic_reader_audio_stop else R.drawable.ic_reader_audio_play)
        listen.contentDescription =
            context.getString(if (playing) R.string.reader_audio_stop else R.string.reader_audio_listen)
    }

    private fun button(icon: Int, description: Int, colour: Int, action: () -> Unit) = ImageButton(context).apply {
        setImageResource(icon)
        imageTintList = ColorStateList.valueOf(colour)
        scaleType = ImageView.ScaleType.FIT_CENTER
        contentDescription = context.getString(description)
        background = boxBackground(colour)
        setOnClickListener {
            // The question comes up where a finger has just been: the press that asked for
            // the card, or its bounce, is not an answer.
            if (SystemClock.uptimeMillis() - shownAt >= GUARD_MS) action()
        }
    }

    private fun boxBackground(colour: Int): Drawable {
        fun shape(fill: Int) = GradientDrawable().apply {
            setColor(fill)
            cornerRadius = dp(4f)
            setStroke(dp(1f).toInt().coerceAtLeast(1), (colour and 0x00FFFFFF) or 0xA0000000.toInt())
        }
        return StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_pressed), shape(ReaderStyle.POPUP_BOX_PRESSED))
            addState(intArrayOf(), shape(ReaderStyle.POPUP_BOX))
        }
    }

    private fun dp(value: Float) =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics)

    companion object {
        private const val BUTTON_WIDTH_DP = 40f
        private const val BUTTON_HEIGHT_DP = 30f

        /** For this long after it came up, the question takes no answer. */
        const val GUARD_MS = 300L
    }
}
