// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package com.flycast.emulator.reader

import androidx.preference.PreferenceManager
import com.flycast.emulator.R
import org.robolectric.RuntimeEnvironment
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The button that "Next line" and touch play press is a bit of the Dreamcast's button word. The
 * first builds offered the PSP's bits, where circle (13) is the Dreamcast's second d-pad: a
 * value saved by them is taken as the button it meant.
 */
@RunWith(RobolectricTestRunner::class)
class AdvanceButtonTest {
    private val context = RuntimeEnvironment.getApplication()

    private fun saved(value: String?): Int {
        val preferences = PreferenceManager.getDefaultSharedPreferences(context)
        preferences.edit().apply {
            if (value == null) remove(ReaderSettings.KEY_ADVANCE_BUTTON) else putString(ReaderSettings.KEY_ADVANCE_BUTTON, value)
        }.commit()
        return ReaderSettings(context).advanceButton
    }

    @Test
    fun theAButtonUnlessAnotherIsChosen() {
        assertEquals(Console.BUTTON_YES, saved(null))
        assertEquals(Console.BUTTON_YES, saved("2"))
        assertEquals(Console.BUTTON_NO, saved("1"))
        assertEquals(3, saved("3")) // Start
    }

    @Test
    fun aValueOfThePspsIsTheButtonItMeant() {
        assertEquals(Console.BUTTON_YES, saved("13")) // circle: yes
        assertEquals(Console.BUTTON_NO, saved("14")) // cross: no
        assertEquals(Console.BUTTON_YES, saved("nonsense"))
    }

    @Test
    fun theChoicesAreTheConsolesButtons() {
        val values = context.resources.getStringArray(R.array.reader_advance_button_values).toList()
        assertEquals(listOf("2", "1", "10", "9", "3"), values)
    }
}
