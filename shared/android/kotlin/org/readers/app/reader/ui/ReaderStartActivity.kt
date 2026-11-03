// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import org.readers.app.R
import org.readers.app.reader.ReaderSettings
import org.readers.app.reader.ReaderSetup

/**
 * What the reader asks at an app's first start, where the app has no setup screen of its own
 * to put it in (Eden's and Azahar's have; ReaderSetup counts the steps there too): save sync,
 * then a dictionary, then Anki, each with the button that opens its screen, or that it is done.
 * Nothing has to be done: *Start reading* leaves, and everything is in Reader settings later.
 */
class ReaderStartActivity : ReaderScreen() {
    override val titleRes = R.string.reader_start_title

    /** One step: its heading, what it is for, its button, where that leads, whether it is done. */
    private class Step(
        val title: Int,
        val description: Int,
        val button: Int,
        val open: (Context) -> Intent,
        val done: (Context) -> Boolean
    )

    private val steps = listOfNotNull(
        Step(
            R.string.reader_setup_dictionaries_title, R.string.reader_setup_dictionaries_description,
            R.string.reader_setup_dictionaries_button,
            { Intent(it, DictionaryManagerActivity::class.java) },
            ReaderSetup::hasDictionary
        ),
        Step(
            R.string.reader_setup_anki_title, R.string.reader_setup_anki_description, R.string.reader_setup_anki_button,
            { Intent(it, AnkiSetupActivity::class.java) },
            ReaderSetup::hasAnki
        )
    )

    private val buttons = ArrayList<Pair<Step, Button>>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val column = scrollingColumn()
        column.addView(body(getString(R.string.reader_start_intro)))
        for (step in steps) {
            column.addView(heading(getString(step.title)))
            column.addView(body(getString(step.description)))
            val button = Button(this).apply {
                isAllCaps = false
                setOnClickListener { startActivity(step.open(this@ReaderStartActivity)) }
            }
            column.addView(button, LinearLayout.LayoutParams(WRAP, WRAP))
            buttons += step to button
        }
        column.addView(spacer(16f))
        column.addView(
            Button(this).apply {
                id = R.id.reader_start_done
                setText(R.string.reader_start_done)
                isAllCaps = false
                setOnClickListener { finish() }
            },
            LinearLayout.LayoutParams(WRAP, WRAP)
        )
        column.addView(note(getString(R.string.reader_start_later)))
    }

    override fun onResume() {
        super.onResume()
        // Back from a step's screen: what it did shows here.
        for ((step, button) in buttons) {
            val done = step.done(this)
            button.text = if (done) getString(R.string.reader_start_step_done, getString(step.title)) else getString(step.button)
        }
    }

    /** The steps' headings and whether each is done, for tests. */
    internal fun stepsShown(): List<Pair<String, Boolean>> = buttons.map { (step, _) -> getString(step.title) to step.done(this) }

    companion object {
        private const val KEY_OFFERED = "reader_start_offered"

        /**
         * At an app's start: opens this screen once, the first time, and only while none of
         * its steps is done (an install that reads already has a dictionary). An app with a
         * setup screen of its own puts the steps there instead and does not call this.
         */
        fun offerOnce(activity: Activity): Boolean {
            val settings = ReaderSettings(activity)
            if (settings.preferences.getBoolean(KEY_OFFERED, false)) return false
            settings.preferences.edit().putBoolean(KEY_OFFERED, true).apply()
            var anyDone = ReaderSetup.hasDictionary(activity) || ReaderSetup.hasAnki(activity)
            if (anyDone) return false
            activity.startActivity(Intent(activity, ReaderStartActivity::class.java))
            return true
        }
    }
}
