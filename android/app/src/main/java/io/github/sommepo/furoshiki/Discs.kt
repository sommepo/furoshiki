// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import java.io.File

/**
 * A game on several discs is one title (the user, 2026-11-13: "make multi disc games show as one
 * thing"): its files are named alike but for "(Disc N)" (Redump's naming, as the user's games are),
 * in one folder. The shelf lists the first disc; the game is given the others as it starts, so the
 * menu over it changes discs; the disc it was left on is the one it starts with next time.
 */
object Discs {
    private val DISC = Regex("""\s*\((?:Disc|Disk|CD)\s*(\d+)(?:\s+of\s+\d+)?\)""", RegexOption.IGNORE_CASE)

    /** The disc's number in its file's name, or null for a game on one disc. */
    fun number(name: String): Int? = DISC.find(name)?.groupValues?.get(1)?.toIntOrNull()

    /** The name with the disc taken out: alike for every disc of a game. */
    fun groupKey(path: String): String {
        val file = File(path)
        return File(file.parentFile, DISC.replace(file.name, "")).path.lowercase()
    }

    /** A title without its " (Disc N)". */
    fun withoutDisc(title: String): String = DISC.replace(title, "").trim()

    /**
     * Every disc of the game [path] is one of, in the order of their numbers, [path]'s own among
     * them; only [path] for a game on one disc. Discs are files of the same kind beside it.
     */
    fun of(path: String): List<String> {
        val file = File(path)
        if (number(file.name) == null) return listOf(path)
        val key = groupKey(path)
        val ext = file.extension.lowercase()
        val found = file.parentFile?.listFiles().orEmpty()
            .filter { it.isFile && it.extension.lowercase() == ext && number(it.name) != null && groupKey(it.path) == key }
            .sortedBy { number(it.name) }
            .map { it.path }
        return if (path in found) found else listOf(path) + found
    }
}
