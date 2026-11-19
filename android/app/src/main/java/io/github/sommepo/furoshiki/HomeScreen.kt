// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.ActivityManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import java.io.File
import org.json.JSONObject

/**
 * Furoshiki as the device's home screen (a handheld with nothing else to be: the AYN Thor).
 * Off unless switched on (Settings, *Home screen*): the two ways in for Android's Home key are
 * components that are disabled in the manifest, [HOME] for the first display and
 * [LowerActivity] for a second one, so a phone is never asked whether Furoshiki is its launcher.
 * Switching it on enables both; Android then asks which home app to use, once.
 *
 * As the home screen the page is alone in its task: a game and every screen of the reader are
 * started in a task of their own (the manifest's `.play` affinity), so the Home key always
 * shows the shelf and a game stays where it was behind it.
 */
object HomeScreen {
    private const val HOME = "io.github.sommepo.furoshiki.ConsoleHome"

    /** An intent to the page that only asks it to the front: the keys go back to its display. */
    const val ACTION_FOCUS = "io.github.sommepo.furoshiki.FOCUS"

    /** An intent to the page that switches the offer, with the boolean extra "on". */
    const val ACTION_OFFER = "io.github.sommepo.furoshiki.HOME_SCREEN"

    private fun components(context: Context) =
        listOf(ComponentName(context.packageName, HOME), ComponentName(context, LowerActivity::class.java))

    /** Switched on in Furoshiki (Android may still have another home app chosen). */
    fun offered(context: Context): Boolean =
        context.packageManager.getComponentEnabledSetting(components(context)[0]) ==
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED

    fun offer(context: Context, on: Boolean) {
        val state = if (on) PackageManager.COMPONENT_ENABLED_STATE_ENABLED else PackageManager.COMPONENT_ENABLED_STATE_DEFAULT
        for (component in components(context)) {
            context.packageManager.setComponentEnabledSetting(component, state, PackageManager.DONT_KILL_APP)
        }
    }

    /** Furoshiki is what the Home key shows. */
    fun chosen(context: Context): Boolean {
        if (!offered(context)) return false
        val home = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME)
        val found = context.packageManager.resolveActivity(home, PackageManager.MATCH_DEFAULT_ONLY)
        return found?.activityInfo?.packageName == context.packageName
    }

    /** An intent for a game or a screen of the reader, in its own task while this is the home screen. */
    fun apart(context: Context, intent: Intent): Intent {
        if (chosen(context)) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        return intent
    }

    /** The Home key's intent: the shelf, from anywhere. */
    fun home(): Intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

    // ---- The game in play, said by its process to the page's (a file: two processes) ----

    private fun playingFile(context: Context) = File(context.filesDir, "playing.json")
    private fun nextFile(context: Context) = File(context.filesDir, "play-next.json")

    private fun game(path: String, console: String, game: String) =
        JSONObject().put("path", path).put("console", console).put("game", game).toString()

    /** Called in the game's process as a game starts, and with null as it ends. */
    fun setPlaying(context: Context, path: String?, console: String = "", game: String = "") {
        runCatching {
            // With the process that plays it: the game's process also runs other work (the first
            // start's setup, AppSetupService), so its being there does not say a game is.
            if (path == null) playingFile(context).delete()
            else playingFile(context).writeText(JSONObject(game(path, console, game)).put("pid", android.os.Process.myPid()).toString())
        }
    }

    /** Whether the game's process is there (it also runs the reader's screens, with no game). */
    fun gameProcess(context: Context): Boolean {
        val running = context.getSystemService(ActivityManager::class.java).runningAppProcesses.orEmpty()
        return running.any { it.processName == context.packageName + ":game" }
    }

    /** The game in play as `{path, console, game}`, or null. */
    fun playing(context: Context): JSONObject? {
        val file = playingFile(context)
        if (!file.isFile) return null
        val kept = runCatching { JSONObject(file.readText()) }.getOrNull()
        val pid = kept?.optInt("pid", 0) ?: 0
        val running = context.getSystemService(ActivityManager::class.java).runningAppProcesses.orEmpty()
            .any { it.processName == context.packageName + ":game" && it.pid == pid }
        if (kept == null || !running) {
            // It ended without saying so (a user's "is running" for a game long gone, 2026-11-19).
            file.delete()
            return null
        }
        return kept
    }

    /** The game to start once the one in play has ended: written by the game's process as it leaves for it. */
    fun setNext(context: Context, path: String, console: String, game: String) {
        runCatching { nextFile(context).writeText(game(path, console, game)) }
    }

    /** Taken once. */
    fun takeNext(context: Context): JSONObject? {
        val file = nextFile(context)
        if (!file.isFile) return null
        val next = runCatching { JSONObject(file.readText()) }.getOrNull()
        file.delete()
        return next
    }
}
