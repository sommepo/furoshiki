// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

package com.armsx2.reader.ui

import android.content.Context
import android.content.Intent
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import com.armsx2.ui.touch.TouchControls
import com.armsx2.GameInfo
import com.armsx2.Main
import com.armsx2.reader.ReaderCatalog
import com.armsx2.ui.InGameOverlay
import com.armsx2.ui.MemoryCardManager
import com.armsx2.ui.SetupImpl
import com.armsx2.ui.WindowImpl

/**
 * What ties the PS2-style menu ([Ps2Home]) to this app: the library's games as the Browser
 * shows them, and the lines of System Configuration with what each opens. The library screen
 * calls [Screen] in its own place while the menu is wanted.
 */
object Ps2Glue {
    /** The menu stands in the library's place (on unless switched off). */
    @Composable
    fun wanted(): Boolean {
        val context = LocalContext.current
        return Ps2Home.wantedState.value ?: Ps2Home.wanted(context).also { Ps2Home.wantedState.value = it }
    }

    @Composable
    fun Screen(games: List<GameInfo>, onLaunch: (GameInfo) -> Unit, onScan: () -> Unit) {
        val context = LocalContext.current
        val all = Ps2Home.allGames(context)
        val shown = games.map { game ->
            Ps2Home.Game(
                key = game.uri.toString(),
                name = game.title,
                serial = game.serial ?: "",
                cover = game.coverUrl,
                readable = game.serial?.let { covers(context, it) } ?: false,
            )
        }.filter { all || it.readable }
        val settings = remember(context) { settings(context, onScan) }
        Ps2Home.Screen(
            games = shown,
            settings = settings,
            onLaunch = { chosen -> games.firstOrNull { it.uri.toString() == chosen.key }?.let(onLaunch) },
            onAddGames = {
                SetupImpl.resetForReentry()
                Main.reopenSetup()
            },
        )
    }

    private fun covers(context: Context, serial: String): Boolean = try {
        ReaderCatalog.covers(context, serial)
    } catch (_: Exception) {
        false
    }

    private fun open(context: Context, page: Class<*>) {
        context.startActivity(Intent(context, page))
    }

    /** The lines of System Configuration: the reader's pages first, then the emulator's own. */
    fun settings(context: Context, onScan: () -> Unit): List<Ps2Home.Setting> = listOf(
        Ps2Home.Setting("statistics", "Reading Statistics") { open(context, ReaderStatsActivity::class.java) },
        Ps2Home.Setting(
            "games", "Game Scripts",
            value = { runCatching { ReaderCatalog.games(context).size.toString() }.getOrDefault("") },
        ) { open(context, ReaderGamesActivity::class.java) },
        Ps2Home.Setting("dictionaries", "Dictionaries") { open(context, DictionaryManagerActivity::class.java) },
        Ps2Home.Setting("anki", "Anki") { open(context, AnkiSetupActivity::class.java) },
        Ps2Home.Setting("reader", "Reader Settings") { open(context, ReaderSettingsActivity::class.java) },
        Ps2Home.Setting("emulator", "Emulator Settings") { InGameOverlay.openGlobalSettings() },
        Ps2Home.Setting("cards", "Memory Cards") { MemoryCardManager.visible.value = true },
        // The buttons drawn over a game for playing by touch: off unless switched on here.
        Ps2Home.Setting(
            "touch", "On-screen Controller",
            value = { if (TouchControls.wanted.value) "On" else "Off" },
        ) { TouchControls.setWanted(!TouchControls.wanted.value) },
        Ps2Home.Setting("bios", "Console Menu") {
            WindowImpl.showLibrary.value = false
            Main.startBios()
        },
        // Only the games whose text can be read, unless switched to every game found.
        Ps2Home.Setting(
            "shown", "Games Shown",
            value = { if (Ps2Home.allGames(context)) "All" else "Readable Only" },
        ) { Ps2Home.setAllGames(context, !Ps2Home.allGames(context)) },
        Ps2Home.Setting("scan", "Look for Games Again") { onScan() },
        Ps2Home.Setting("setup", "Folders and Setup") {
            SetupImpl.resetForReentry()
            Main.reopenSetup()
        },
        Ps2Home.Setting("list", "Game List", value = { "This menu" }) { Ps2Home.setWanted(context, false) },
    )
}
