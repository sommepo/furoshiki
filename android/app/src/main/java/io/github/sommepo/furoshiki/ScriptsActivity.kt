// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import io.github.sommepo.furoshiki.reader.Console
import io.github.sommepo.furoshiki.reader.ScriptStore
import io.github.sommepo.furoshiki.reader.ui.ReaderGamesActivity

/**
 * The reader's Game scripts screen for one console, the page's choice: its published list,
 * in the reader's process, where the screen takes the console's list from (Console). Nothing
 * of its own is shown.
 */
class ScriptsActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        Console.use(intent.getStringExtra(EXTRA_CONSOLE).orEmpty())
        ScriptStore.indexUrl = Console.SCRIPT_INDEX_URL
        startActivity(Intent(this, ReaderGamesActivity::class.java))
        finish()
    }

    companion object {
        const val EXTRA_CONSOLE = "console"
    }
}
