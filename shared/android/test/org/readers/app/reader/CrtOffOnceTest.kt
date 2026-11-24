// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.Context
import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.readers.app.reader.ui.CrtSettings
import org.robolectric.RobolectricTestRunner

/** The CRT picture switched off once for everyone (2026-11-24), and left on when switched on after. */
@RunWith(RobolectricTestRunner::class)
class CrtOffOnceTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val prefs get() = PreferenceManager.getDefaultSharedPreferences(context)

    @Test
    fun aKeptPictureThatIsOnIsSwitchedOffOnceItsOtherValuesKept() {
        prefs.edit().putString(CrtSettings.KEY, "enabled=1.000;warp=0.300;frame=2.000").commit()
        assertTrue(CrtSettings.switchOffOnce(context))
        assertEquals("enabled=0.000;warp=0.300;frame=2.000", prefs.getString(CrtSettings.KEY, null))
        // Switched on again by the user: it stays on.
        prefs.edit().putString(CrtSettings.KEY, "enabled=1.000;warp=0.300;frame=2.000").commit()
        assertFalse(CrtSettings.switchOffOnce(context))
        assertEquals("enabled=1.000;warp=0.300;frame=2.000", prefs.getString(CrtSettings.KEY, null))
    }

    @Test
    fun nothingKeptOrAlreadyOffIsLeftAsItIs() {
        assertFalse(CrtSettings.switchOffOnce(context))
        assertFalse(prefs.contains(CrtSettings.KEY))
        assertTrue(prefs.getBoolean(CrtSettings.KEY_SWITCHED_OFF, false))
        assertEquals("enabled=0.000;warp=0.300", CrtSettings.off("enabled=0.000;warp=0.300"))
        assertEquals("warp=0.300", CrtSettings.off("warp=0.300"))
    }
}
