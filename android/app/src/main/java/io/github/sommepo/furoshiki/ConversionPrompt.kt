// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import io.github.sommepo.furoshiki.reader.dict.DictionaryConversion
import io.github.sommepo.furoshiki.reader.dict.DictionaryDatabase
import io.github.sommepo.furoshiki.reader.dict.DictionaryStore
import io.github.sommepo.furoshiki.reader.ui.DictionaryManagerActivity
import kotlin.concurrent.thread

/**
 * The dictionaries of an earlier version's file moved into the dictionary engine, said on the home
 * screen (the maintainer, 2026-11-24: "existing users that update, how will the dict conversion work
 * for them? it needs to be a prompt so they are aware it's happening, with a progress bar").
 *
 * The move starts here, in the app's own process, the first time the home screen shows after the
 * update: never from a game's process, which goes on with the old file until the move is done. A
 * dialog says what is happening and why, the dictionary being moved, n of N and a bar for the whole
 * move; Hide puts it away to a strip at the foot of the home screen, which a tap brings back. At the
 * end it says how it went until OK: all moved, or which did not come and what to do.
 */
class ConversionPrompt(private val activity: Activity) {
    private val handler = Handler(Looper.getMainLooper())
    private var dialog: AlertDialog? = null
    private var strip: LinearLayout? = null
    private var watching = false

    // The dialog's parts, kept to be told the progress.
    private lateinit var body: TextView
    private lateinit var which: TextView
    private lateinit var title: TextView
    private lateinit var bar: ProgressBar
    private lateinit var percent: TextView

    private val tick = object : Runnable {
        override fun run() {
            if (!watching || givingUp) return
            val state = DictionaryConversion.state
            when {
                state.finished && !resultSeen -> showResult(state)
                state.running -> {
                    if (dialog == null && strip?.visibility != View.VISIBLE) showProgress()
                    update(state)
                }
            }
            // Until it is over: a move just started counts nothing yet, and is waited for.
            if (state.running || DictionaryConversion.busy || (state.finished && !resultSeen)) handler.postDelayed(this, 250)
        }
    }

    /** As the home screen comes up: starts the move where there is one, and shows how it goes. */
    fun resume() {
        watching = true
        pendingRemoved?.let { removed ->
            pendingRemoved = null
            showRemoved(removed)
            return
        }
        if (givingUp) return
        val state = DictionaryConversion.state
        if (state.finished && !resultSeen) {
            handler.post(tick)
            return
        }
        val legacy = activity.getDatabasePath(DictionaryDatabase.DATABASE_NAME)
        if (!DictionaryConversion.busy && !state.running && !legacy.exists()) return
        val app = activity.applicationContext
        thread(name = "dictionary-conversion-check") {
            val database = DictionaryStore.get(app).database
            if (database.conversionNeeded) database.startConversion()
            handler.post(tick)
        }
    }

    /** As it goes (a game starts): the dialog put away for now; the move goes on. */
    fun pause() {
        watching = false
        handler.removeCallbacks(tick)
        dialog?.setOnDismissListener(null)
        dialog?.dismiss()
        dialog = null
    }

    private fun dp(value: Float) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, activity.resources.displayMetrics).toInt()

    private fun showProgress() {
        val pad = dp(20f)
        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, dp(8f), pad, 0)
        }
        body = TextView(activity).apply { textSize = 15f }
        which = TextView(activity).apply { textSize = 12f; setPadding(0, dp(16f), 0, 0); alpha = 0.7f }
        title = TextView(activity).apply { textSize = 16f; setPadding(0, dp(2f), 0, dp(6f)) }
        bar = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000 }
        percent = TextView(activity).apply { textSize = 12f; alpha = 0.7f }
        listOf(body, which, title, bar, percent).forEach { box.addView(it) }
        dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.conversion_title)
            .setView(box)
            .setCancelable(true)
            .setPositiveButton(R.string.conversion_hide) { _, _ -> putAway() }
            .setNeutralButton(R.string.conversion_start_over) { _, _ -> confirmStartOver() }
            .create().apply {
                setOnCancelListener { putAway() }
                show()
            }
        update(DictionaryConversion.state)
    }

    private fun update(state: DictionaryConversion.State) {
        val current = if (state.total > 0) (state.done + 1).coerceAtMost(state.total) else 0
        strip?.let { s ->
            (s.getChildAt(0) as TextView).text =
                activity.getString(R.string.conversion_strip, current, state.total, state.title)
            (s.getChildAt(1) as ProgressBar).progress = (state.share * 1000).toInt()
        }
        if (dialog == null) return
        body.text = activity.resources.getQuantityString(R.plurals.conversion_body, state.total, state.total)
        which.text = activity.getString(R.string.conversion_which, current, state.total)
        title.text = state.title
        bar.progress = (state.share * 1000).toInt()
        val share = (state.share * 100).toInt()
        percent.text = if (state.resumed > 0) {
            activity.resources.getQuantityString(R.plurals.conversion_resumed, state.resumed, share, state.resumed)
        } else {
            activity.getString(R.string.conversion_percent, share)
        }
    }

    /** Hide: the dialog goes, a strip at the foot says it on, and brings it back when tapped. */
    private fun putAway() {
        dialog = null
        val s = strip ?: LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.argb(230, 16, 26, 58))
            setPadding(dp(16f), dp(8f), dp(16f), dp(8f))
            addView(TextView(activity).apply { textSize = 13f; setTextColor(Color.WHITE); maxLines = 1 })
            addView(ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000 })
            isClickable = true
            setOnClickListener {
                visibility = View.GONE
                showProgress()
            }
            activity.addContentView(this, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM))
        }
        strip = s
        s.visibility = View.VISIBLE
        update(DictionaryConversion.state)
    }

    /** Start over: asked first, as it deletes what the user has; then the move is given up. */
    private fun confirmStartOver() {
        dialog = null
        val total = DictionaryConversion.state.total
        fun resumeOrFinish() {
            dialog = null
            val state = DictionaryConversion.state
            when {
                state.finished && !resultSeen -> showResult(state)
                state.running -> showProgress()
            }
        }
        dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.conversion_confirm_title)
            .setMessage(activity.resources.getQuantityString(R.plurals.conversion_confirm_body, total, total))
            .setNegativeButton(R.string.conversion_cancel) { _, _ -> resumeOrFinish() }
            .setPositiveButton(R.string.conversion_remove) { _, _ -> dialog = null; startOver() }
            .setOnCancelListener { resumeOrFinish() }
            .create().apply { show() }
    }

    private fun startOver() {
        resultSeen = true // the move's own result is not said after this
        givingUp = true
        strip?.visibility = View.GONE
        val app = activity.applicationContext
        thread(name = "dictionary-conversion-abandon") {
            val outcome = runCatching { DictionaryStore.get(app).database.abandonConversion() }
            handler.post {
                if (outcome.isFailure) {
                    givingUp = false
                    if (!watching || activity.isFinishing || activity.isDestroyed) return@post
                    dialog = AlertDialog.Builder(activity)
                        .setTitle(R.string.conversion_failed_title)
                        .setMessage(activity.getString(R.string.conversion_error_body, outcome.exceptionOrNull()?.message.orEmpty()))
                        .setPositiveButton(R.string.conversion_ok) { _, _ -> dialog = null }
                        .create().apply { show() }
                    return@post
                }
                val removed = outcome.getOrThrow()
                if (!watching || activity.isFinishing || activity.isDestroyed) {
                    pendingRemoved = removed
                    return@post
                }
                showRemoved(removed)
            }
        }
    }

    private fun showRemoved(removed: Int) {
        if (removed < 0) {
            dialog = AlertDialog.Builder(activity)
                .setTitle(R.string.conversion_done_title)
                .setMessage(R.string.conversion_already_done_body)
                .setPositiveButton(R.string.conversion_ok) { _, _ -> dialog = null }
                .create().apply { show() }
            return
        }
        dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.conversion_removed_title)
            .setMessage(activity.resources.getQuantityString(R.plurals.conversion_removed_body, removed, removed))
            .setPositiveButton(R.string.conversion_import) { _, _ ->
                dialog = null
                activity.startActivity(Intent(activity, DictionaryManagerActivity::class.java))
            }
            .setNegativeButton(R.string.conversion_ok) { _, _ -> dialog = null }
            .create().apply { show() }
    }

    private fun showResult(state: DictionaryConversion.State) {
        resultSeen = true
        strip?.visibility = View.GONE
        dialog?.setOnCancelListener(null)
        dialog?.dismiss()
        val ok = state.failed.isEmpty()
        val seconds = Math.round(state.seconds).toInt()
        dialog = AlertDialog.Builder(activity)
            .setTitle(if (ok) R.string.conversion_done_title else R.string.conversion_failed_title)
            .setMessage(
                if (ok) activity.resources.getQuantityString(R.plurals.conversion_done_body, state.total, state.total, seconds)
                else activity.getString(R.string.conversion_failed_body, state.failed.joinToString("\n"))
            )
            .setPositiveButton(R.string.conversion_ok) { _, _ -> dialog = null }
            .create().apply { show() }
    }

    companion object {
        /** The result was shown in this process: not again on the next resume. */
        @Volatile
        private var resultSeen = false

        /** Start over was chosen: the move's dialog and strip do not come back. */
        @Volatile
        private var givingUp = false

        /** If abandoning finished while the activity was paused, show it on the next resume. */
        @Volatile
        private var pendingRemoved: Int? = null
    }
}
