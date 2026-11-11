// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.view.Surface
import androidx.annotation.Keep

/**
 * The native side of playing (app/src/main/cpp/furoshiki_jni.cpp): one core in this process
 * (cores/reader_core.h). Only a process that plays or looks through games loads it, never the
 * page's.
 */
object GameNative {
    init {
        System.loadLibrary("furoshiki")
    }

    /** Loads and starts the core in [corePath] with these folders; what went wrong, or null. */
    external fun open(corePath: String, data: String, saves: String, states: String, system: String): String?

    /** What the core says of itself, as JSON: key, name, system, short, types, settings. */
    external fun info(): String?

    /** The ID and the title of the game in [path], or null when it is no game of this core's. */
    external fun identify(path: String): Array<String>?

    /** Where to draw; null while there is no surface. */
    /** The art a game's file carries, by the core that reads it. */
    external fun gameArt(path: String, to: String): Boolean

    /** What the core said last that went wrong: why a game ended that nobody ended. */
    external fun lastError(): String?

    external fun setSurface(surface: Surface?, width: Int, height: Int, scale: Float)

    /** Where a console's lower screen goes on a device with two displays; null puts it back. */
    external fun setSecondSurface(surface: Surface?, width: Int, height: Int)

    /** Where the two screens are, x, y, width and height of each, upper first; each in its own window. */
    external fun screenRects(): IntArray?

    /** A finger on [screen]: [x] and [y] from 0 to 1 across it. */
    external fun pointer(screen: Int, x: Float, y: Float, pressed: Boolean)

    /** Starts the game; what went wrong, or null. */
    external fun load(path: String): String?

    external fun pause(paused: Boolean)

    external fun stop()

    /** The controller: buttons by the core's bits, sticks from -32768 to 32767, triggers to 255. */
    external fun setPad(buttons: Long, leftX: Int, leftY: Int, rightX: Int, rightY: Int, leftTrigger: Int, rightTrigger: Int)

    /** The core's features (reader_core.h's READER_CORE_HAS_*): 2 discs, 16 savestates told. */
    external fun features(): Int

    /** How many savestate slots the core has, numbered from 1. */
    external fun stateSlots(): Int

    /** When the running game's savestate [slot] was written (ms): 0 none, -1 the core does not tell. */
    external fun stateSaved(slot: Int): Long

    external fun discCount(): Int
    external fun discCurrent(): Int
    external fun discLabel(index: Int): ByteArray
    external fun insertDisc(index: Int): Boolean

    /** The patches the core has for a game, as JSON: [{"name","about","on"}]. */
    external fun patches(gameId: String): ByteArray

    external fun saveState(slot: Int): Boolean

    external fun loadState(slot: Int): Boolean

    /** Hands the core the CRT sliders when they changed. */
    external fun sendCrt()

    external fun applySetting(key: String, value: String)

    /** Save sync for a console: its saves' folder, a folder of save sync's own, this device's name. */
    external fun startSaveSync(key: String, savedata: String, data: String, device: String)

    /** What save sync has to say to the user: shown by whoever is in front ([say]). */
    @Volatile
    var say: ((String) -> Unit)? = null

    @Keep
    @JvmStatic
    fun onSay(text: ByteArray) {
        say?.invoke(String(text, Charsets.UTF_8))
    }
}
