package io.github.sommepo.furoshiki

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The in-app updater's reading of GitHub's latest release and its comparison of versions (Releases.kt). */
class ReleasesTest {
    private fun release(
        tag: String = "v0.2.6",
        draft: Boolean = false,
        prerelease: Boolean = false,
        assets: String = """
            {"name": "Furoshiki-0.2.6-windows.zip", "size": 1000, "browser_download_url": "https://github.com/sommepo/furoshiki/releases/download/v0.2.6/Furoshiki-0.2.6-windows.zip"},
            {"name": "Furoshiki-0.2.6-android.apk", "size": 123456789, "browser_download_url": "https://github.com/sommepo/furoshiki/releases/download/v0.2.6/Furoshiki-0.2.6-android.apk"},
            {"name": "Furoshiki-ocr-helper.zip", "size": 10, "browser_download_url": "https://github.com/sommepo/furoshiki/releases/download/v0.2.6/Furoshiki-ocr-helper.zip"}
        """,
    ) = """
        {"url": "https://api.github.com/repos/sommepo/furoshiki/releases/1", "html_url": "https://github.com/sommepo/furoshiki/releases/tag/$tag",
         "tag_name": "$tag", "name": "Furoshiki 0.2.6", "draft": $draft, "prerelease": $prerelease,
         "body": "Fixes.", "assets": [$assets]}
    """

    @Test fun readsTheAndroidApk() {
        val found = Releases.parse(release())
        assertNotNull(found)
        found!!
        assertEquals("v0.2.6", found.tag)
        assertEquals("0.2.6", found.version)
        assertEquals("https://github.com/sommepo/furoshiki/releases/download/v0.2.6/Furoshiki-0.2.6-android.apk", found.apk)
        assertEquals(123456789L, found.size)
        assertEquals("https://github.com/sommepo/furoshiki/releases/tag/v0.2.6", found.page)
        assertEquals("Fixes.", found.notes)
    }

    @Test fun anyApkWhenNoneIsNamedSo() {
        val found = Releases.parse(release(assets = """{"name": "app-debug.apk", "size": 5, "browser_download_url": "https://example.org/app-debug.apk"}"""))
        assertEquals("https://example.org/app-debug.apk", found?.apk)
    }

    @Test fun noReleaseWithoutAnApk() {
        assertNull(Releases.parse(release(assets = """{"name": "Furoshiki-0.2.6-windows.zip", "size": 5, "browser_download_url": "https://example.org/w.zip"}""")))
        assertNull(Releases.parse(release(assets = "")))
    }

    @Test fun noDraftOrPreRelease() {
        assertNull(Releases.parse(release(draft = true)))
        assertNull(Releases.parse(release(prerelease = true)))
    }

    @Test fun noPlainAddresses() {
        assertNull(Releases.parse(release(assets = """{"name": "Furoshiki-0.2.6-android.apk", "size": 5, "browser_download_url": "http://example.org/a.apk"}""")))
    }

    @Test fun nothingFromWhatIsNoRelease() {
        assertNull(Releases.parse(""))
        assertNull(Releases.parse("not json"))
        assertNull(Releases.parse("""{"message": "Not Found", "documentation_url": "https://docs.github.com"}"""))
        assertNull(Releases.parse("""{"message": "API rate limit exceeded"}"""))
    }

    @Test fun keptAndReadBack() {
        val found = Releases.parse(release())!!
        assertEquals(found, Releases.Release.fromJson(found.toJson()))
    }

    @Test fun tagsAndVersions() {
        assertEquals("0.2.6", Releases.versionOf("v0.2.6"))
        assertEquals("0.2.6", Releases.versionOf("0.2.6"))
        assertEquals("0.2.6", Releases.versionOf(" V0.2.6 "))
    }

    @Test fun newerByNumbers() {
        assertTrue(Releases.newer("v0.2.6", "0.2.5"))
        assertTrue(Releases.newer("0.2.10", "0.2.9"))
        assertTrue(Releases.newer("0.3", "0.2.9"))
        assertTrue(Releases.newer("1.0.0", "0.99.99"))
        assertTrue(Releases.newer("0.2.5.1", "0.2.5"))
        assertFalse(Releases.newer("v0.2.5", "0.2.5"))
        assertFalse(Releases.newer("0.2.5", "0.2.6"))
        assertFalse(Releases.newer("1.0", "1.0.0"))
        assertFalse(Releases.newer("0.2.9", "0.2.10"))
    }

    @Test fun suffixesComeFirst() {
        assertTrue(Releases.newer("0.3.0", "0.3.0-rc1"))
        assertFalse(Releases.newer("0.3.0-rc1", "0.3.0"))
        assertTrue(Releases.newer("0.3.0-rc2", "0.3.0-rc1"))
        assertTrue(Releases.newer("0.3.0-rc1", "0.2.9"))
        assertFalse(Releases.newer("0.2.6+abc", "0.2.6"))
    }

    @Test fun noVersionIsNeverNewer() {
        // A build made for no release (the dev line's, a build from source) is offered nothing.
        assertFalse(Releases.newer("0.2.6", ""))
        assertFalse(Releases.newer("", "0.2.5"))
        assertFalse(Releases.newer("latest", "0.2.5"))
        assertFalse(Releases.newer("v", "0.2.5"))
    }

    @Test fun theAppsOwnVersionIsNotTheReleases() {
        // App 0.5.7 shipped as v0.2.5: the updater compares release with release, never with versionName.
        assertTrue(Releases.newer("v0.2.6", "0.2.5"))
        assertFalse(Releases.newer("v0.2.6", "0.5.7"))
    }
}
