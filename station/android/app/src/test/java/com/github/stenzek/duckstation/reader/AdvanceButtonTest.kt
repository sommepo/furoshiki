// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package com.github.stenzek.duckstation.reader

import androidx.preference.PreferenceManager
import com.github.stenzek.duckstation.R
import org.robolectric.RuntimeEnvironment
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** The button that "Next line" and touch play press: the emulator's number for one of the controller's. */
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
    fun circleUnlessAnotherIsChosen() {
        assertEquals(Console.BUTTON_YES, saved(null))
        assertEquals(Console.BUTTON_YES, saved("13"))
        assertEquals(Console.BUTTON_NO, saved("14"))
        assertEquals(3, saved("3")) // Start
    }

    @Test
    fun aValueThatIsNoButtonIsCircle() {
        assertEquals(Console.BUTTON_YES, saved("nonsense"))
        assertEquals(Console.BUTTON_YES, saved("16"))
        assertEquals(Console.BUTTON_YES, saved("-1"))
    }

    @Test
    fun theChoicesAreTheConsolesButtons() {
        val values = context.resources.getStringArray(R.array.reader_advance_button_values).toList()
        assertEquals(listOf("13", "14", "12", "15", "3"), values)
        assertEquals(Console.ADVANCE_DEFAULT, context.getString(R.string.reader_advance_button_default))
    }
}
