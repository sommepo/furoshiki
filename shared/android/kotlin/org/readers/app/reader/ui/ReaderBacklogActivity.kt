// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.os.Bundle
import android.widget.LinearLayout
import org.readers.app.R
import org.readers.app.reader.ReaderSession

/** The backlog as a screen of its own ([BacklogView]). */
class ReaderBacklogActivity : ReaderScreen() {
    override val titleRes = R.string.reader_backlog

    private lateinit var backlog: BacklogView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val game = intent.getStringExtra(EXTRA_GAME) ?: ReaderSession.gameKey
        backlog = BacklogView(this, game) {
            startActivity(android.content.Intent(this, DictionaryManagerActivity::class.java))
        }
        content.addView(backlog, LinearLayout.LayoutParams(MATCH, 0, 1f))
    }

    override fun onDestroy() {
        backlog.release()
        super.onDestroy()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (backlog.lookups.isOpen) backlog.lookups.close() else super.onBackPressed()
    }

    companion object {
        const val EXTRA_GAME = "game"
    }
}
