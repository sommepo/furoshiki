// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import java.io.File
import org.json.JSONObject

/**
 * Where box art is looked for (the maintainer, 2026-11-23: "offer screenscraper and vndb as
 * options"), as on the desktop (`home/src/art_fetch.h`, Sources). ScreenScraper needs the user's
 * own account and a developer ID and password, which the user types: nothing of ScreenScraper's is
 * built in. A file of its own (`files/art.json`), as both the page's process, a scan and the reader's
 * screens read it.
 */
data class ArtSources(
    val vndb: Boolean = true,
    val screenScraper: Boolean = false,
    val ssUser: String = "",
    val ssPassword: String = "",
    val ssDevId: String = "",
    val ssDevPassword: String = "",
) {
    val screenScraperReady: Boolean get() = screenScraper && ssDevId.isNotEmpty() && ssDevPassword.isNotEmpty()

    fun save(context: Context) {
        val file = file(context)
        val partial = File(file.path + ".new")
        partial.writeText(
            JSONObject().put("vndb", vndb).put("screenscraper", screenScraper).put("ss_user", ssUser)
                .put("ss_password", ssPassword).put("ss_dev_id", ssDevId).put("ss_dev_password", ssDevPassword).toString()
        )
        if (!partial.renameTo(file)) partial.delete()
    }

    companion object {
        private fun file(context: Context) = File(context.filesDir, "art.json")

        fun load(context: Context): ArtSources = runCatching {
            val o = JSONObject(file(context).readText())
            ArtSources(
                o.optBoolean("vndb", true), o.optBoolean("screenscraper", false), o.optString("ss_user"),
                o.optString("ss_password"), o.optString("ss_dev_id"), o.optString("ss_dev_password"),
            )
        }.getOrDefault(ArtSources())
    }
}
