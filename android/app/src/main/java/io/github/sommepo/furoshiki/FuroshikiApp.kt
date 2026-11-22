// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Application
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.webkit.WebView
import android.widget.Toast
import io.github.sommepo.furoshiki.reader.ReaderNative
import io.github.sommepo.furoshiki.reader.ReaderSettings
import io.github.sommepo.furoshiki.reader.dict.SharedDictionaries
import kotlin.concurrent.thread

/** What each of the app's processes needs before its first screen. */
class FuroshikiApp : Application() {
    override fun onCreate() {
        super.onCreate()
        // A card's tag is the app's, not the running core's key ("ppsspp-reader").
        ReaderSettings.ankiTagsDefault = "furoshiki"
        if (getProcessName().endsWith(":game")) {
            // The page's process has a WebView, and two processes cannot share its data folder:
            // the dictionary popup is a web page in this one.
            WebView.setDataDirectorySuffix("game")
            // The native library before anything of the reader's: a screen here that is no game's
            // (another app's companion) calls ReaderNative first, and only touching GameNative loads
            // it. On the dev line save sync below touched it; a published build has no save sync,
            // and the companion died at its first call there (a public user's Thor, 2026-11-19:
            // UnsatisfiedLinkError, ReaderNative.setAudioCapture).
            GameNative.ensureLoaded()
            SharedDictionaries.plainFolder = { Places.dictionaryFolder(it) }
            AppSetup.seed(this)
            thread(name = "AppSetup") { AppSetup.anki(this) }
        }
    }

}
