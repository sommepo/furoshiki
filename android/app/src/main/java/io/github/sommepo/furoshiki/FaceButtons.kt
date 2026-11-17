// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.os.Build
import android.provider.Settings
import android.view.KeyEvent
import io.github.sommepo.furoshiki.reader.ButtonMap
import io.github.sommepo.furoshiki.reader.ReaderSettings

/**
 * The face buttons by where they are, whatever their letters. Android's key codes are places
 * (BUTTON_A the bottom one, B the right, X the left, Y the top), and Furoshiki's button map,
 * its popup and its cores take them so. An AYN handheld in its Nintendo style (the quick
 * settings' controller style: `temp_abxy_layout_mode` 1) sends its built-in buttons by their
 * letters instead, B at the bottom and A on the right (the maintainer, 2026-11-17, the Thor:
 * "bottom button is B, left is Y, right is A, top is X"): those keys are turned back into places
 * as they come, so the defaults are right there too. Other controllers (the Steam Controller, one
 * plugged in) are left as they are.
 */
object FaceButtons {
    /** Whether this device's own controller sends its face buttons by their Nintendo letters. */
    fun nintendo(context: Context): Boolean =
        Build.MANUFACTURER.equals("AYN", ignoreCase = true) &&
            runCatching { Settings.System.getInt(context.contentResolver, "temp_abxy_layout_mode", 0) }.getOrDefault(0) == 1

    private val swap = mapOf(
        KeyEvent.KEYCODE_BUTTON_A to KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_B to KeyEvent.KEYCODE_BUTTON_A,
        KeyEvent.KEYCODE_BUTTON_X to KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Y to KeyEvent.KEYCODE_BUTTON_X,
    )

    /** [event] with its face button by place, where [nintendo] and it comes from the device's own controller. */
    fun byPlace(event: KeyEvent, nintendo: Boolean): KeyEvent {
        if (!nintendo) return event
        val code = swap[event.keyCode] ?: return event
        val device = event.device ?: return event
        if (!builtIn(device)) return event
        return KeyEvent(event.downTime, event.eventTime, event.action, code, event.repeatCount, event.metaState,
            event.deviceId, event.scanCode, event.flags, event.source)
    }

    /**
     * The device's own controller: AYN's ("Odin Controller", vendor 0x2020, on the Thor and the Odins).
     * Android lists it as external (it is a USB device inside the handheld), so that cannot tell it.
     */
    private fun builtIn(device: android.view.InputDevice): Boolean =
        !device.isVirtual && (device.vendorId == AYN_VENDOR || device.name.startsWith("Odin", ignoreCase = true))

    private const val AYN_VENDOR = 0x2020

    /**
     * The map the maintainer made by hand before this (A and B, X and Y swapped for every console)
     * is now what the device gives by itself: taken out once, or it would swap them back.
     */
    fun forgetHandSwap(context: Context) {
        if (!nintendo(context)) return
        val prefs = ReaderSettings.fresh(context)
        if (prefs.getBoolean(DONE, false)) return
        val swapped = mapOf("south" to "east", "east" to "south", "west" to "north", "north" to "west")
        val edit = prefs.edit().putBoolean(DONE, true)
        if (swapped.all { (button, input) -> prefs.getString(ButtonMap.PAD_PREFIX + button, null) == input }) {
            for (button in swapped.keys) edit.remove(ButtonMap.PAD_PREFIX + button)
        }
        edit.commit()
    }

    private const val DONE = "pad_face_by_place"
}
