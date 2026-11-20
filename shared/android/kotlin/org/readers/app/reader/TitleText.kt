// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

/**
 * A game's title as it is shown, where it was taken from a file's name: the dump's tags at its
 * end go ("CHULIP (USA) (1)", "Game (Japan) [!]", "Game (En,Fr) (Rev 1)") and the "_ " a file
 * name has in the place of ": " comes back ("PARANORMASIGHT_ THE SEVEN MYSTERIES"). A disc's
 * number stays, and so does any other bracket: a game script's own "作り話 (A Made-up Game)" is
 * a name, not a tag. The file is never renamed. The page has the same rule (front.js, cleanTitle),
 * and the desktop program its own (home/src).
 */
object TitleText {
    private val PART = Regex(
        "(USA|Japan|Europe|World|Asia|Korea|China|Taiwan|Hong Kong|Australia|Brazil|Canada|France|Germany|Italy|Spain|" +
            "Netherlands|Sweden|Russia|UK|NTSC(-[UJ])?|PAL|En|Ja|Fr|De|Es|It|Nl|Pt|Sv|Ko|Zh|Ru|" +
            "Rev ?[0-9A-Z.]+|v?[0-9][0-9.]*|Beta ?[0-9]*|Demo|Proto(type)?|Unl|Alt ?[0-9]*|Sample)",
        RegexOption.IGNORE_CASE
    )
    private val LAST_ROUND = Regex("\\s*\\(([^()]*)\\)\\s*$")
    private val LAST_SQUARE = Regex("\\s*\\[[^\\[\\]]*]\\s*$")

    /** Whether what stands in a pair of round brackets is a dump's tag: every part of it one. */
    fun isTag(inside: String): Boolean =
        inside.split(',').map { it.trim() }.let { parts -> parts.isNotEmpty() && parts.all { PART.matches(it) } }

    fun clean(title: String): String {
        var text = title
        while (true) {
            val square = LAST_SQUARE.find(text)
            val round = LAST_ROUND.find(text)
            val cut = when {
                square != null -> text.removeRange(square.range)
                round != null && isTag(round.groupValues[1]) -> text.removeRange(round.range)
                else -> break
            }
            if (cut.isBlank()) break
            text = cut
        }
        return text.replace("_ ", ": ").replace(Regex("\\s{2,}"), " ").trim().ifEmpty { title }
    }
}
