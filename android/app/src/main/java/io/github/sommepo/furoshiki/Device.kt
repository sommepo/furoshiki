// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.BatteryManager
import android.os.Environment
import android.os.StatFs
import android.provider.Settings
import org.json.JSONArray
import org.json.JSONObject

/**
 * The device as Furoshiki's own pages show it while it is the home screen ([HomeScreen]): the
 * battery, the network, the volume and the brightness, the other apps, and the ways into
 * Android's own settings. Nothing here needs more than an ordinary app may do, but the
 * brightness, which Android lets an app change once the user has allowed it.
 */
object Device {
    /** What the head of a page says: `{battery, charging, wifi, online, volume, brightness}`. */
    fun status(context: Context): JSONObject {
        val battery = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val level = battery?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale = battery?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
        val plugged = (battery?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0) != 0
        val network = context.getSystemService(ConnectivityManager::class.java)
        val now = runCatching { network.getNetworkCapabilities(network.activeNetwork) }.getOrNull()
        return JSONObject()
            .put("battery", if (level < 0 || scale <= 0) -1 else level * 100 / scale)
            .put("charging", plugged)
            .put("wifi", now?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true)
            .put("online", now?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) == true)
            .put("volume", volume(context))
            .put("brightness", brightness(context))
    }

    // ---- The volume of games and the brightness, each 0 to 100 ----

    fun volume(context: Context): Int {
        val audio = context.getSystemService(AudioManager::class.java)
        val most = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC).coerceAtLeast(1)
        return audio.getStreamVolume(AudioManager.STREAM_MUSIC) * 100 / most
    }

    /** One of the device's own steps up or down. */
    fun stepVolume(context: Context, direction: Int): Int {
        val audio = context.getSystemService(AudioManager::class.java)
        val most = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val next = (audio.getStreamVolume(AudioManager.STREAM_MUSIC) + direction).coerceIn(0, most)
        runCatching { audio.setStreamVolume(AudioManager.STREAM_MUSIC, next, 0) }
        return volume(context)
    }

    fun mayDim(context: Context): Boolean = Settings.System.canWrite(context)

    /** The screen's brightness as Android keeps it (1 to 255), or -1 where it cannot be read. */
    fun brightness(context: Context): Int {
        val raw = runCatching { Settings.System.getInt(context.contentResolver, Settings.System.SCREEN_BRIGHTNESS) }.getOrNull()
            ?: return -1
        return (raw * 100 / 255).coerceIn(0, 100)
    }

    /** A tenth up or down; by hand from then on, not by the light in the room. */
    fun stepBrightness(context: Context, direction: Int): Int {
        if (!mayDim(context)) return brightness(context)
        val next = ((brightness(context).coerceAtLeast(0) + direction * 10 + 5) / 10 * 10).coerceIn(5, 100)
        runCatching {
            Settings.System.putInt(context.contentResolver, Settings.System.SCREEN_BRIGHTNESS_MODE, Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL)
            Settings.System.putInt(context.contentResolver, Settings.System.SCREEN_BRIGHTNESS, (next * 255 / 100).coerceIn(1, 255))
        }
        return brightness(context)
    }

    /** Free and whole, in bytes, of the device and of each card: `[{name, free, total}]`. */
    fun storage(context: Context): JSONArray {
        val list = JSONArray()
        val seen = HashSet<String>()
        for (folder in context.getExternalFilesDirs(null).filterNotNull()) {
            // .../Android/data/<package>/files: the volume is four folders up.
            val volume = generateSequence(folder) { it.parentFile }.elementAtOrNull(4) ?: continue
            if (!seen.add(volume.path)) continue
            val space = runCatching { StatFs(volume.path) }.getOrNull() ?: continue
            val name = if (Environment.isExternalStorageRemovable(folder)) "Card" else "Device"
            list.put(JSONObject().put("name", name).put("free", space.availableBytes).put("total", space.totalBytes))
        }
        return list
    }

    // ---- The other apps ----

    /**
     * Every app with a way in from a launcher, but this one: `[{label, package, activity, game}]`,
     * by name. `game`: what Android itself calls a game (its category, or the older flag), for
     * the list an added game's app is chosen from.
     */
    fun apps(context: Context): JSONArray {
        val packages = context.packageManager
        val launcher = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val found = packages.queryIntentActivities(launcher, 0)
            .filter { it.activityInfo.packageName != context.packageName }
            .sortedBy { it.loadLabel(packages).toString().lowercase() }
        val list = JSONArray()
        for (one in found) {
            val app = one.activityInfo.applicationInfo
            list.put(
                JSONObject().put("label", one.loadLabel(packages).toString()).put("package", one.activityInfo.packageName)
                    .put("activity", one.activityInfo.name).put("game", isGame(app?.category ?: -1, app?.flags ?: 0))
            )
        }
        return list
    }

    /** Whether Android marks an app as a game: its category, or the flag older apps set. */
    @Suppress("DEPRECATION")
    fun isGame(category: Int, flags: Int): Boolean =
        category == android.content.pm.ApplicationInfo.CATEGORY_GAME || flags and android.content.pm.ApplicationInfo.FLAG_IS_GAME != 0

    fun openApp(context: Context, name: String, activity: String) {
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
            .setComponent(ComponentName(name, activity))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED)
        runCatching { context.startActivity(intent) }
    }

    // ---- Android's own screens ----

    private val screens = mapOf(
        "wifi" to Settings.ACTION_WIFI_SETTINGS,
        "bluetooth" to Settings.ACTION_BLUETOOTH_SETTINGS,
        "display" to Settings.ACTION_DISPLAY_SETTINGS,
        "sound" to Settings.ACTION_SOUND_SETTINGS,
        "storage" to Settings.ACTION_INTERNAL_STORAGE_SETTINGS,
        "date" to Settings.ACTION_DATE_SETTINGS,
        "battery" to Intent.ACTION_POWER_USAGE_SUMMARY,
        "android" to Settings.ACTION_SETTINGS,
        "home" to Settings.ACTION_HOME_SETTINGS,
    )

    /** One of Android's screens by a name of ours; `app:<package>` is that app's page there. */
    fun open(context: Context, what: String) {
        val intent = when {
            what.startsWith("app:") ->
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + what.substringAfter(':')))
            what == "dim" -> Intent(Settings.ACTION_MANAGE_WRITE_SETTINGS, Uri.parse("package:" + context.packageName))
            what == "notifications" || what == "quick" -> {
                panel(context, if (what == "quick") "expandSettingsPanel" else "expandNotificationsPanel")
                return
            }
            else -> Intent(screens[what] ?: return)
        }
        runCatching { context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
    }

    /** The system's notifications or its quick settings, pulled down for a hand that has no bar to pull. */
    private fun panel(context: Context, method: String) {
        runCatching {
            val bar = context.getSystemService("statusbar") ?: return
            bar.javaClass.getMethod(method).invoke(bar)
        }
    }
}
