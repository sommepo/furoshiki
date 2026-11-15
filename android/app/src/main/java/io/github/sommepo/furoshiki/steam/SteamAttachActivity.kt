// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.steam

import android.app.Activity
import android.os.Bundle

/**
 * What the system opens when a Steam Controller is plugged in (the manifest's filter): nothing to
 * see, it closes at once. Its use is the system's question "Open Furoshiki when this is
 * connected?", which with "Always" lets Furoshiki read the controller without asking again; the
 * screen in front ([SteamUsb]) takes it from there.
 */
class SteamAttachActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        finish()
    }
}
