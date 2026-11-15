// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager

/**
 * Android's audio focus, held by the activity in front on the main display while a game plays
 * (and by the page while nothing else plays).
 *
 * Why (the user, 2026-11-15: no sound at all on the AYN Thor, the menu's included, while YouTube
 * played): the Thor's game assistant lists in `sys.audio.uids` every app with a window on the
 * second display, which Furoshiki always has (its page down there), and its audio mixer plays the
 * apps so listed at the second display's volume, which came out at nothing. The one way off that
 * list is to hold audio focus asked for from the main display: the assistant then takes that app's
 * uid out of it (`MultiDisplayVolumeControl` in com.odin.gameassistant, read on the device). Holding
 * focus while a game plays is what a game should do anyway.
 */
object AudioFocus {
    private var request: AudioFocusRequest? = null

    private fun audio(context: Context) = context.getSystemService(AudioManager::class.java)

    /**
     * Asks for focus. A game ([game]) takes it whole; the page only when no other app is playing,
     * so that the home screen never stops the user's music.
     *
     * Android on the Thor files a request under the display whose window has the window manager's
     * focus at that moment (MediaFocusControl's mCurrWMFocusActivityInfo), and the assistant keeps
     * an app asked for from the second display on its list. In onResume that focus may still be the
     * second display's page, so the activity asks [again] once its own window has the focus: the
     * request is then filed under the first display, and the volume buttons are the game's
     * (the user, 2026-11-15: "bottom screen is the audio focus. So volume button does not work").
     */
    fun take(context: Context, game: Boolean, again: Boolean = false) {
        val audio = audio(context) ?: return
        if (request != null && !again) return
        request?.let { held -> runCatching { audio.abandonAudioFocusRequest(held) } }
        request = null
        if (!game && audio.isMusicActive) return
        val asked = AudioFocusRequest.Builder(if (game) AudioManager.AUDIOFOCUS_GAIN else AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build(),
            )
            .setWillPauseWhenDucked(false)
            .setOnAudioFocusChangeListener { change ->
                // Taken by another app for good: let it be, and ask again when this one is back in front.
                if (change == AudioManager.AUDIOFOCUS_LOSS) request = null
            }
            .build()
        if (runCatching { audio.requestAudioFocus(asked) }.getOrDefault(AudioManager.AUDIOFOCUS_REQUEST_FAILED) ==
            AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        ) {
            request = asked
        }
    }

    fun give(context: Context) {
        val held = request ?: return
        request = null
        runCatching { audio(context)?.abandonAudioFocusRequest(held) }
    }
}
