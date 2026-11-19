package io.github.sommepo.furoshiki

import org.json.JSONObject

/**
 * What the in-app updater (`Updater`) reads from GitHub, kept apart from Android so the JVM tests
 * can reach it: the latest release's JSON and the comparison of two release versions.
 *
 * A published build knows the release it was made for (`BuildConfig.FUROSHIKI_RELEASE`, given by
 * `tools/publish/furoshiki-release.sh` as `-PfuroshikiRelease=<version>`), and that is what is
 * compared with the release's tag: the app's own versionName is a different count (app 0.5.7
 * shipped as v0.2.5) and is never compared with a tag.
 */
object Releases {
    const val REPOSITORY = "sommepo/furoshiki"
    const val LATEST = "https://api.github.com/repos/$REPOSITORY/releases/latest"

    /** One release: its tag ("v0.2.6"), the version in it ("0.2.6"), the APK's address and size, its page. */
    data class Release(val tag: String, val version: String, val apk: String, val size: Long, val page: String, val notes: String) {
        fun toJson(): JSONObject = JSONObject().put("tag", tag).put("version", version).put("apk", apk)
            .put("size", size).put("page", page).put("notes", notes)

        companion object {
            fun fromJson(json: JSONObject): Release? {
                val version = json.optString("version")
                val apk = json.optString("apk")
                if (version.isEmpty() || apk.isEmpty()) return null
                return Release(json.optString("tag"), version, apk, json.optLong("size", 0), json.optString("page"), json.optString("notes"))
            }
        }
    }

    /** The version a tag names: "v0.2.6" and "0.2.6" are both 0.2.6. */
    fun versionOf(tag: String): String = tag.trim().removePrefix("v").removePrefix("V")

    /** The APK a release carries for Android: `Furoshiki-<version>-android.apk`, else any one `.apk`. */
    private val APK = Regex("""^Furoshiki-.+-android\.apk$""")

    /**
     * The release in GitHub's answer to `releases/latest`, or null when it is not one this app can
     * install: a draft, a pre-release, or one without an Android APK among its assets.
     */
    fun parse(json: String): Release? {
        val release = runCatching { JSONObject(json) }.getOrNull() ?: return null
        if (release.optBoolean("draft") || release.optBoolean("prerelease")) return null
        val tag = release.optString("tag_name")
        if (tag.isEmpty()) return null
        val assets = release.optJSONArray("assets") ?: return null
        var chosen: JSONObject? = null
        for (i in 0 until assets.length()) {
            val asset = assets.optJSONObject(i) ?: continue
            val name = asset.optString("name")
            if (APK.matches(name)) { chosen = asset; break }
            if (chosen == null && name.endsWith(".apk")) chosen = asset
        }
        val apk = chosen?.optString("browser_download_url").orEmpty()
        if (!apk.startsWith("https://")) return null
        return Release(tag, versionOf(tag), apk, chosen!!.optLong("size", 0), release.optString("html_url"), release.optString("body"))
    }

    /**
     * Whether release version [there] is newer than [here], number by number ("0.2.10" after
     * "0.2.9", "0.3" after "0.2.9", "1.0" the same as "1.0.0"). A part with letters after its number
     * ("0.3.0-rc1") counts as before the plain one. An empty or unreadable [there] is never newer;
     * an empty [here] (a build that is no release) is never offered one.
     */
    fun newer(there: String, here: String): Boolean {
        val a = parts(versionOf(there)) ?: return false
        val b = parts(versionOf(here)) ?: return false
        for (i in 0 until maxOf(a.size, b.size)) {
            val x = a.getOrElse(i) { Part(0, "") }
            val y = b.getOrElse(i) { Part(0, "") }
            if (x.number != y.number) return x.number > y.number
            if (x.rest != y.rest) {
                // The plain number is the release; one with a suffix comes before it.
                if (x.rest.isEmpty()) return true
                if (y.rest.isEmpty()) return false
                return x.rest > y.rest
            }
        }
        return false
    }

    private data class Part(val number: Long, val rest: String)

    private fun parts(version: String): List<Part>? {
        if (version.isBlank()) return null
        // "0.3.0-rc1": the suffix belongs to the last number. Build metadata ("+abc") is not compared.
        val core = version.trim().substringBefore('+')
        val numbers = core.substringBefore('-')
        val suffix = core.drop(numbers.length).trimStart('-')
        val dotted = numbers.split('.')
        return dotted.mapIndexed { i, piece ->
            val digits = piece.takeWhile { it.isDigit() }
            if (digits.isEmpty()) return null
            val rest = piece.drop(digits.length) + if (i == dotted.lastIndex) suffix else ""
            Part(digits.toLongOrNull() ?: return null, rest)
        }
    }
}
