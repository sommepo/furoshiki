package io.github.sommepo.furoshiki

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * The in-app updater of a published build: the latest release on GitHub (`Releases`), checked at
 * most once a day as the page comes up and never while a game plays, downloaded into the app's
 * cache and installed through `PackageInstaller`, which asks the user to confirm. Android's
 * "install unknown apps" for Furoshiki is asked for the first time it is needed.
 *
 * Only a build made for a release has it (`BuildConfig.FUROSHIKI_RELEASE` not empty): the dev
 * line's builds keep the home server's Files page (`Services.readUpdate`, dev only) instead.
 * What was found is kept in `files/release.json`: `{checked, tried, release, told}`.
 */
object Updater {
    private const val TAG = "Updater"
    private const val HOUR_MS = 60L * 60 * 1000
    private const val DAY_MS = 24 * HOUR_MS
    const val ACTION_STATUS = "io.github.sommepo.furoshiki.UPDATE_STATUS"

    val enabled: Boolean get() = BuildConfig.FUROSHIKI_RELEASE.isNotEmpty()
    val mine: String get() = BuildConfig.FUROSHIKI_RELEASE

    private fun stateFile(context: Context) = File(context.filesDir, "release.json")

    fun state(context: Context): JSONObject =
        runCatching { JSONObject(stateFile(context).readText()) }.getOrDefault(JSONObject())

    private fun save(context: Context, state: JSONObject) {
        val file = stateFile(context)
        val next = File(file.path + ".new")
        next.writeText(state.toString())
        next.renameTo(file)
    }

    /** The release found last, when it is newer than this build. */
    fun available(context: Context): Releases.Release? {
        val release = state(context).optJSONObject("release")?.let { Releases.Release.fromJson(it) } ?: return null
        return release.takeIf { Releases.newer(it.version, mine) }
    }

    /** For the Settings page's Update row: `{mine, version, newer, checked}`, or "" in a build that is no release. */
    fun row(context: Context): String {
        if (!enabled) return ""
        val state = state(context)
        val release = state.optJSONObject("release")?.let { Releases.Release.fromJson(it) }
        return JSONObject().put("mine", mine).put("version", release?.version.orEmpty())
            .put("newer", release != null && Releases.newer(release.version, mine))
            .put("checked", state.optLong("checked", 0)).toString()
    }

    fun online(context: Context): Boolean {
        val manager = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val capabilities = manager.getNetworkCapabilities(manager.activeNetwork) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }

    /** Whether the once-a-day check is due. */
    fun due(context: Context, now: Long = System.currentTimeMillis()): Boolean {
        if (!enabled) return false
        val state = state(context)
        val checked = state.optLong("checked", 0)
        val tried = state.optLong("tried", 0)
        return (now - checked >= DAY_MS || now < checked) && (now - tried >= HOUR_MS || now < tried)
    }

    /**
     * Asks GitHub for the latest release. Never on the main thread. Returns the newer release, or
     * null when none is newer; a failure when offline or GitHub did not answer (the page says
     * nothing of it unless the user asked). A failed check is tried again an hour later at most.
     */
    fun check(context: Context): Result<Releases.Release?> {
        if (!enabled) return Result.success(null)
        if (!online(context)) return Result.failure(IllegalStateException("offline"))
        save(context, state(context).put("tried", System.currentTimeMillis()))
        return runCatching {
            val release = Releases.parse(get(Releases.LATEST))
            val state = state(context).put("checked", System.currentTimeMillis())
            if (release != null) state.put("release", release.toJson()) else state.remove("release")
            save(context, state)
            val newer = release?.takeIf { Releases.newer(it.version, mine) }
            // An APK downloaded for a release that is no longer newer (it was installed) is let go.
            if (newer == null) cache(context).deleteRecursively()
            newer
        }.onFailure { Log.i(TAG, "no answer from GitHub: ${it.message}") }
    }

    /** Whether the page has already said that [release] is out (said once a release). */
    fun told(context: Context, release: Releases.Release): Boolean = state(context).optString("told") == release.tag

    fun markTold(context: Context, release: Releases.Release) = save(context, state(context).put("told", release.tag))

    private fun get(address: String): String {
        val connection = URL(address).openConnection() as HttpURLConnection
        connection.connectTimeout = 6000
        connection.readTimeout = 15000
        connection.setRequestProperty("Accept", "application/vnd.github+json")
        connection.setRequestProperty("User-Agent", "Furoshiki-Android")
        try {
            if (connection.responseCode !in 200..299) error("HTTP ${connection.responseCode}")
            return connection.inputStream.bufferedReader().use { it.readText() }
        } finally {
            connection.disconnect()
        }
    }

    private fun cache(context: Context) = File(context.cacheDir, "update")

    /** Whether Android lets Furoshiki install apps; when not, [askToInstall] opens the switch for it. */
    fun mayInstall(context: Context): Boolean = context.packageManager.canRequestPackageInstalls()

    fun askToInstall(context: Context) {
        context.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${context.packageName}"))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    /**
     * Downloads [release]'s APK into the cache (kept there: a second try does not fetch it again),
     * checks it is this app's package, and hands it to `PackageInstaller`. Never on the main thread.
     * [progress] is told the percentage now and then.
     */
    fun install(context: Context, release: Releases.Release, progress: (Int) -> Unit) {
        val folder = cache(context).apply { mkdirs() }
        val apk = File(folder, "Furoshiki-${release.version}-android.apk")
        if (!apk.isFile || (release.size > 0 && apk.length() != release.size)) {
            folder.listFiles()?.forEach { it.delete() }
            download(release, File(folder, apk.name + ".part"), progress).renameTo(apk) || error("could not keep the download")
        }
        val archive = context.packageManager.getPackageArchiveInfo(apk.path, 0)
        if (archive?.packageName != context.packageName) {
            apk.delete()
            error("the download is not Furoshiki")
        }
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
            .apply { setAppPackageName(context.packageName); setSize(apk.length()) }
        val id = installer.createSession(params)
        installer.openSession(id).use { session ->
            session.openWrite("furoshiki.apk", 0, apk.length()).use { out ->
                apk.inputStream().use { it.copyTo(out) }
                session.fsync(out)
            }
            val status = PendingIntent.getBroadcast(context, id,
                Intent(context, UpdateReceiver::class.java).setAction(ACTION_STATUS),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE)
            session.commit(status.intentSender)
        }
    }

    private fun download(release: Releases.Release, into: File, progress: (Int) -> Unit): File {
        val connection = URL(release.apk).openConnection() as HttpURLConnection
        connection.connectTimeout = 10000
        connection.readTimeout = 30000
        connection.instanceFollowRedirects = true
        connection.setRequestProperty("User-Agent", "Furoshiki-Android")
        try {
            if (connection.responseCode !in 200..299) error("HTTP ${connection.responseCode}")
            val total = connection.contentLengthLong.takeIf { it > 0 } ?: release.size
            var done = 0L
            var said = -1
            connection.inputStream.use { input ->
                into.outputStream().use { out ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        out.write(buffer, 0, n)
                        done += n
                        if (total > 0) {
                            val percent = (done * 100 / total).toInt()
                            if (percent / 10 != said / 10) { said = percent; progress(percent) }
                        }
                    }
                }
            }
            if (release.size > 0 && into.length() != release.size) {
                into.delete()
                error("the download was cut short")
            }
            return into
        } finally {
            connection.disconnect()
        }
    }
}

/** `PackageInstaller`'s answers: the confirmation it asks for is shown, a failure said. */
class UpdateReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT) ?: return
                runCatching { context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
                    .onFailure { Log.w("Updater", "could not ask to install: ${it.message}") }
            }
            PackageInstaller.STATUS_SUCCESS -> Unit // The app is replaced; nothing is left to say.
            PackageInstaller.STATUS_FAILURE_ABORTED -> Unit // The user said no.
            else -> {
                val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE).orEmpty()
                Log.w("Updater", "install failed ($status): $message")
                val said = if (status == PackageInstaller.STATUS_FAILURE_CONFLICT || status == PackageInstaller.STATUS_FAILURE_INCOMPATIBLE)
                    "Couldn't install the update: this copy of Furoshiki was signed differently. Install the new version by hand."
                else "Couldn't install the update. Try again from Settings › About."
                Toast.makeText(context, said, Toast.LENGTH_LONG).show()
            }
        }
    }
}
