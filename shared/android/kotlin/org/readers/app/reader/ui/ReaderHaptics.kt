// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.content.Context
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import androidx.preference.PreferenceManager
import org.readers.app.reader.ReaderSettings

/**
 * A short tick in the hand: when a card lands in Anki ("land") and when the popup's d-pad moves
 * to another entry ("step"). The lightest effect Android has ready, on a device with a vibrator,
 * while Settings › Controller › Haptics is on. The popup's page asks for it by a message
 * (`{"type":"feedback","kind":…}`, LookupPopupView) and a card made tells it too (LookupLayer.report):
 * the two for one card are one tick.
 */
object ReaderHaptics {
    const val LAND = "land"
    const val STEP = "step"

    /** Two asks for the same kind closer than this are one. */
    private val GAP_MS = mapOf(LAND to 800L, STEP to 40L)
    private val last = HashMap<String, Long>()

    /** Whether a tick of [kind] asked for at [now] is given, [before] being the last of its kind. */
    fun due(kind: String, now: Long, before: Long?): Boolean {
        val gap = GAP_MS[kind] ?: return false
        return before == null || now - before >= gap || now < before
    }

    fun tick(context: Context, kind: String) {
        val now = SystemClock.uptimeMillis()
        synchronized(last) {
            if (!due(kind, now, last[kind])) return
            last[kind] = now
        }
        val on = PreferenceManager.getDefaultSharedPreferences(context).getBoolean(ReaderSettings.KEY_HAPTICS, true)
        if (!on) return
        runCatching {
            val vibrator = context.getSystemService(Vibrator::class.java) ?: return
            if (!vibrator.hasVibrator()) return
            vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK))
        }
    }
}
