// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the ARMSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

/**
 * Going on to the next choice: for someone who has read this far on another copy of the game
 * and wants to give it the same choices. The game runs at full speed with yes pressed until
 * its profile reports a choice; the text on the way is not read (reader/core/next_choice.h has
 * it). Started from the app's in-game menu or with the button chosen in the reader's settings;
 * a press of any of the game's buttons stops it. Only where the emulator has it
 * ([Console.HAS_NEXT_CHOICE]).
 */
object NextChoice {
    /** While it runs. Main thread. */
    var going = false
        private set

    private val listeners = ArrayList<(Boolean) -> Unit>()

    /** Told when it starts or ends (main thread); returns what removes the listener. */
    fun listen(listener: (Boolean) -> Unit): () -> Unit {
        listeners.add(listener)
        return { listeners.remove(listener) }
    }

    /** Starts it, or stops it when it runs. Main thread. */
    fun toggle() {
        if (!Console.HAS_NEXT_CHOICE) return
        try {
            ReaderNative.toggleNextChoice()
        } catch (_: UnsatisfiedLinkError) {
            // No emulator behind this (tests).
        }
    }

    /** The emulator says on the game's picture why it stopped (reader/core/emulator.cpp). */
    internal fun onChange(running: Boolean, @Suppress("UNUSED_PARAMETER") end: Int,
                          @Suppress("UNUSED_PARAMETER") passed: Long) {
        going = running
        listeners.toList().forEach { it(running) }
    }
}
