// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import io.github.sommepo.furoshiki.reader.Console

/**
 * The Game scripts screen (GameScriptsActivity: every console's games, 2026-11-23), at the page's
 * choice of console, or every console's where it names none. In the reader's process. Nothing of
 * its own is shown.
 */
class ScriptsActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val console = intent.getStringExtra(EXTRA_CONSOLE).orEmpty()
        if (console.isNotEmpty()) Console.use(console)
        startActivity(Intent(this, GameScriptsActivity::class.java).putExtra(GameScriptsActivity.EXTRA_CONSOLE, console))
        finish()
    }

    companion object {
        const val EXTRA_CONSOLE = "console"
    }
}
