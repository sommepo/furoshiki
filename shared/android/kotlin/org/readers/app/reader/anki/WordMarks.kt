// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.anki

/**
 * Where a learnt word stops being weak and becomes well known, and what counts as learnt: the
 * rule behind the word marks (Furochibi underlines the words of a line by how well Anki has
 * them, after Jiten Reader). It is turned into searches in Anki's own syntax ([gradingOf]), so
 * R (`prop:r`, from FSRS), intervals, reviews and lapses are Anki's own numbers.
 *
 * Each of the limits is left out of the rule when 0 (or, for [maxLapses], below 0). [search],
 * when not blank, is used as the rule instead of them all.
 */
data class WordMarkRule(
    /** R (recall probability now) at least this, in percent; Anki's `prop:r`. */
    val minRetrievability: Int = 75,
    /** The interval at least this many days; 21 is Anki's (and Jiten's) "mature". */
    val minInterval: Int = 21,
    /** At least this many reviews. */
    val minReviews: Int = 0,
    /** At most this many lapses (times forgotten); below 0 for any. */
    val maxLapses: Int = -1,
    /** A card added but never reviewed: [UNREVIEWED_NEW] (with words not in Anki) or [UNREVIEWED_WEAK]. */
    val unreviewed: String = UNREVIEWED_NEW,
    /** A suspended card: [SUSPENDED_RULE] (as any other), [SUSPENDED_KNOWN] or [SUSPENDED_NEW]. */
    val suspended: String = SUSPENDED_RULE,
    /** A search of the user's own for "well known", in place of the limits above. */
    val search: String = "",
) {
    /** The rule's own part of the search: what a learnt card must also be to be well known. */
    fun ruleSearch(): String {
        if (search.isNotBlank()) return "(" + search.trim() + ")"
        return buildList {
            if (minRetrievability > 0) add("prop:r>=" + "%.2f".format(java.util.Locale.ROOT, minRetrievability.coerceAtMost(100) / 100.0))
            if (minInterval > 0) add("prop:ivl>=$minInterval")
            if (minReviews > 0) add("prop:reps>=$minReviews")
            if (maxLapses >= 0) add("prop:lapses<=$maxLapses")
        }.joinToString(" ")
    }

    companion object {
        const val UNREVIEWED_NEW = "new"
        const val UNREVIEWED_WEAK = "weak"
        const val SUSPENDED_RULE = "rule"
        const val SUSPENDED_KNOWN = "known"
        const val SUSPENDED_NEW = "new"

        /** The two searches of [rule] (AnkiIndex.grade adds the deck and the field to each). */
        fun gradingOf(rule: WordMarkRule): AnkiIndex.Grading {
            val learntCard = when (rule.suspended) {
                SUSPENDED_NEW -> "-is:new -is:suspended"
                else -> "-is:new"
            }
            val wellKnown = (learntCard + " " + rule.ruleSearch()).trim()
            val known = if (rule.suspended == SUSPENDED_KNOWN) "($wellKnown) OR is:suspended" else wellKnown
            val learnt = when {
                rule.unreviewed != UNREVIEWED_WEAK -> learntCard
                rule.suspended == SUSPENDED_NEW -> "-is:suspended"
                else -> null
            }
            return AnkiIndex.Grading(known, learnt)
        }
    }
}
