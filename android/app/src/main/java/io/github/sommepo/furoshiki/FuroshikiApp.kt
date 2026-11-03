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
import io.github.sommepo.furoshiki.reader.dict.SharedDictionaries
import kotlin.concurrent.thread

/** What each of the app's processes needs before its first screen. */
class FuroshikiApp : Application() {
    override fun onCreate() {
        super.onCreate()
        if (getProcessName().endsWith(":game")) {
            // The page's process has a WebView, and two processes cannot share its data folder:
            // the dictionary popup is a web page in this one.
            WebView.setDataDirectorySuffix("game")
            SharedDictionaries.plainFolder = { Places.dictionaryFolder(it) }
            AppSetup.seed(this)
            thread(name = "AppSetup") { AppSetup.anki(this) }
        }
    }

}
