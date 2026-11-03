// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceGroup
import androidx.preference.PreferenceManager
import androidx.preference.SeekBarPreference
import androidx.preference.SwitchPreferenceCompat
import androidx.recyclerview.widget.RecyclerView
import androidx.test.core.app.ApplicationProvider
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.materialswitch.MaterialSwitch
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.readers.app.R
import org.readers.app.reader.anki.CardBuilder
import org.readers.app.reader.dict.DictionaryImporter
import org.readers.app.reader.dict.DictionaryStore
import org.readers.app.reader.model.RubySpan
import org.readers.app.reader.model.TextLine
import org.readers.app.reader.stats.LineAccounting
import org.readers.app.reader.stats.ReadingCounters
import org.readers.app.reader.stats.ReadingRules
import org.readers.app.reader.ui.AnkiSetupActivity
import org.readers.app.reader.ui.DictionaryManagerActivity
import org.readers.app.reader.ui.LookupPopupView
import org.readers.app.reader.ui.ReaderBacklogActivity
import org.readers.app.reader.ui.ReaderGamesActivity
import org.readers.app.reader.ui.ReaderSettingsActivity
import org.readers.app.reader.ui.ReaderStartActivity
import org.readers.app.reader.ui.ReaderStatsActivity
import org.readers.app.reader.ui.ReaderTextView
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import org.robolectric.shadows.ShadowDialog

/**
 * The reader's own screens, started as real activities off-device with data seeded for the
 * test: each must come up, show what is stored and carry out its actions. PNGs are written
 * when READER_PREVIEW_DIR is set.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(qualifiers = "w768dp-h432dp-400dpi")
class ReaderScreensTest {
    private val context: Context get() = ApplicationProvider.getApplicationContext()
    private val game = "0004000000088A00"
    private var stub: AnkiConnectStub? = null

    @Before
    fun setUp() {
        TestSupport.installApplication()
        TestSupport.resetStores()
    }

    @After
    fun tearDown() {
        stub?.close()
        TestSupport.resetStores()
    }

    private fun root(activity: Activity): View = activity.findViewById(android.R.id.content)

    private fun text(activity: Activity) = TestSupport.allText(root(activity))

    private fun button(activity: Activity, label: String): View =
        TestSupport.find(root(activity), TextView::class.java) { it.text.toString() == label }
            ?: throw AssertionError("no \"$label\" on screen:\n${text(activity)}")

    private fun confirmDialog() {
        val dialog = ShadowDialog.getLatestDialog() as AlertDialog
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick()
        TestSupport.idle()
    }

    private fun installDictionary(title: String = "Test Dict") {
        val zip = File(context.cacheDir, "$title.zip")
        ZipOutputStream(zip.outputStream()).use { out ->
            out.putNextEntry(ZipEntry("index.json"))
            out.write("""{"title":"$title","revision":"2026-10","format":3}""".toByteArray())
            out.closeEntry()
            out.putNextEntry(ZipEntry("term_bank_1.json"))
            out.write(
                """[["見張る","みはる","v5","v5",10,["to keep watch"],1,""],
                    ["道","みち","n","",5,["road"],2,""]]""".toByteArray()
            )
            out.closeEntry()
        }
        DictionaryImporter(DictionaryStore.get(context).database).import(zip)
    }

    @Test
    fun statisticsShowTotalsAndDeleteASession() {
        val db = ReaderSession.database(context)
        val today = ReadingRules.readingDay(System.currentTimeMillis())
        db.setGameTitle(game, "真・女神転生IV")
        val older = db.createSession(game, 1_758_000_000_000)
        db.addActivity(older, game, "2026-09-16", ReadingCounters(1_800_000, 6000, 5500, 500, 210, 40), 1_758_003_600_000)
        db.closeSession(older, 1_758_003_600_000)
        val current = db.createSession(game, System.currentTimeMillis() - 600_000)
        db.addActivity(current, game, today, ReadingCounters(600_000, 2468, 2468, 0, 80, 0), System.currentTimeMillis())

        val activity = Robolectric.buildActivity(ReaderStatsActivity::class.java).setup().get()
        TestSupport.await("statistics") { text(activity).contains("真・女神転生IV") }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-statistics")
        val shown = text(activity)
        assertTrue(shown, shown.contains("2,468")) // today
        assertTrue(shown, shown.contains("8,468")) // all games
        assertTrue(shown, shown.contains("40m 00s")) // 30 + 10 minutes
        assertTrue(shown, shown.contains("12,702")) // 8468 characters in 40 minutes, per hour
        assertTrue(shown, shown.contains("09-16"))

        // Deleting the older session removes exactly its share.
        val row = TestSupport.find(root(activity), TextView::class.java) { it.text.startsWith("2025-09-16") || it.text.startsWith("2026-09-16") }
        assertNotNull(shown, row)
        (row!!.parent as View).performClick()
        confirmDialog()
        TestSupport.await("totals after deletion") { !text(activity).contains("8,468") }
        assertEquals(2468L, db.totals(game).characters)
        assertEquals(listOf(current), db.sessions(game).map { it.id })
    }

    @Test
    fun statisticsSayWhenThereIsNothingYet() {
        val activity = Robolectric.buildActivity(ReaderStatsActivity::class.java).setup().get()
        TestSupport.await("empty message") { text(activity).contains("No reading recorded yet.") }
    }

    @Test
    fun dictionaryManagerListsReordersAndDeletes() {
        installDictionary("Test Dict")
        installDictionary("Second Dict")
        val store = DictionaryStore.get(context)
        val activity = Robolectric.buildActivity(DictionaryManagerActivity::class.java).setup().get()
        TestSupport.await("dictionary list") { text(activity).contains("Second Dict") }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-dictionaries")
        assertTrue(text(activity).contains("2 terms"))
        assertTrue(text(activity).contains("2026-10"))

        // Switch the first one off.
        TestSupport.find(root(activity), MaterialSwitch::class.java)!!.performClick()
        TestSupport.await("switch stored") { !store.database.dictionaries().first { it.title == "Test Dict" }.enabled }

        // Move the second above the first.
        val up = ArrayList<View>()
        fun collect(v: View) {
            if (v.contentDescription == "Move up") up.add(v)
            if (v is android.view.ViewGroup) for (i in 0 until v.childCount) collect(v.getChildAt(i))
        }
        collect(root(activity))
        assertFalse(up[0].isEnabled)
        up[1].performClick()
        TestSupport.await("new order") { store.database.dictionaries().map { it.title } == listOf("Second Dict", "Test Dict") }

        // Delete asks first.
        TestSupport.await("list redrawn") {
            TestSupport.find(root(activity), TextView::class.java)?.let { text(activity).indexOf("Second Dict") < text(activity).indexOf("Test Dict") } == true
        }
        TestSupport.find(root(activity), View::class.java) { it.contentDescription == "Delete" }!!.performClick()
        confirmDialog()
        TestSupport.await("deletion") { store.database.dictionaries().map { it.title } == listOf("Test Dict") }
    }

    @Test
    fun theSetupScreensReaderPagesKnowWhenTheyAreDone() {
        // A dictionary: one is installed.
        assertFalse(ReaderSetup.hasDictionary(context))
        installDictionary("Test Dict")
        assertTrue(ReaderSetup.hasDictionary(context))

        // Anki: a deck and a note type are chosen, and cards are not switched off.
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        assertFalse(ReaderSetup.hasAnki(context))
        prefs.edit().putString(ReaderSettings.KEY_ANKI_DECK, "Mining").commit()
        assertFalse(ReaderSetup.hasAnki(context))
        prefs.edit().putString(ReaderSettings.KEY_ANKI_MODEL, "Lapis").commit()
        assertTrue(ReaderSetup.hasAnki(context))
        prefs.edit().putString(ReaderSettings.KEY_ANKI_TRANSPORT, ReaderSettings.ANKI_NONE).commit()
        assertFalse(ReaderSetup.hasAnki(context))

        // What the pages say fits a page of the setup screen.
        for (id in listOf(
            R.string.reader_setup_dictionaries_description,
            R.string.reader_setup_anki_description
        )) {
            assertTrue(context.getString(id), context.getString(id).length <= 240)
        }
    }

    @Test
    fun theFirstStartOffersTheReadersStepsOnceAndSaysWhatIsDone() {
        val app = Robolectric.buildActivity(Activity::class.java).setup().get()
        assertTrue("offered at the first start", ReaderStartActivity.offerOnce(app))
        assertEquals(
            ReaderStartActivity::class.java.name,
            org.robolectric.Shadows.shadowOf(app).nextStartedActivity.component!!.className
        )
        assertFalse("and only then", ReaderStartActivity.offerOnce(app))

        val controller = Robolectric.buildActivity(ReaderStartActivity::class.java).setup()
        val activity = controller.get()
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-start")
        assertEquals(
            listOfNotNull("Save sync".takeIf { org.readers.app.BuildConfig.READER_SAVE_SYNC }, "Dictionaries", "Anki cards"),
            activity.stepsShown().map { it.first }
        )
        assertTrue(activity.stepsShown().none { it.second })
        assertTrue(text(activity), text(activity).contains("Install dictionaries"))
        // Back from the dictionaries' screen with one installed: that step says it is done.
        installDictionary()
        controller.pause().resume()
        assertTrue(text(activity), text(activity).contains("Dictionaries: done"))
        activity.findViewById<Button>(R.id.reader_start_done).performClick()
        assertTrue(activity.isFinishing)

        // An install that reads already is not asked at all.
        PreferenceManager.getDefaultSharedPreferences(context).edit().remove("reader_start_offered").commit()
        assertFalse(ReaderStartActivity.offerOnce(app))
    }

    @Test
    fun dictionaryManagerExplainsAnEmptyList() {
        val activity = Robolectric.buildActivity(DictionaryManagerActivity::class.java).setup().get()
        TestSupport.await("empty hint") { text(activity).contains("No dictionaries yet") }
    }

    @Test
    fun ankiSetupConnectsSuggestsFieldsAndSaves() {
        val server = AnkiConnectStub().also { stub = it }
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        prefs.edit()
            .putString(ReaderSettings.KEY_ANKI_TRANSPORT, ReaderSettings.ANKI_ANKICONNECT)
            .putString(ReaderSettings.KEY_ANKICONNECT_URL, server.url)
            .apply()

        val activity = Robolectric.buildActivity(AnkiSetupActivity::class.java).setup().get()
        TestSupport.await("connection") { text(activity).contains("Connected: 2 decks, 2 note types") }
        val spinners = ArrayList<Spinner>()
        fun collect(v: View) {
            if (v is Spinner) spinners.add(v)
            if (v is android.view.ViewGroup) for (i in 0 until v.childCount) collect(v.getChildAt(i))
        }
        collect(root(activity))
        assertEquals(2, spinners.size)
        spinners[0].setSelection(1) // 日本語::Mining
        spinners[1].setSelection(1) // Lapis
        TestSupport.await("fields of the note type") { text(activity).contains("Picture") }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-anki-setup")

        // Fields come pre-filled from their names; one is edited by hand.
        val editors = ArrayList<EditText>()
        fun fields(v: View) {
            if (v is EditText && v.text.toString().startsWith("{")) editors.add(v)
            if (v is android.view.ViewGroup) for (i in 0 until v.childCount) fields(v.getChildAt(i))
        }
        fields(root(activity))
        assertEquals(listOf("{expression}", "{sentence}", "{screenshot}"), editors.map { it.text.toString() })
        editors[1].setText("{sentence}<br>{audio}")

        button(activity, "Save").performClick()
        TestSupport.idle()
        val settings = ReaderSettings(context)
        assertEquals(ReaderSettings.ANKI_ANKICONNECT, settings.ankiTransport)
        assertEquals("日本語::Mining", settings.ankiDeck)
        assertEquals("Lapis", settings.ankiModel)
        assertEquals(
            mapOf("Expression" to "{expression}", "Sentence" to "{sentence}<br>{audio}", "Picture" to "{screenshot}"),
            CardBuilder.parseMapping(settings.ankiFieldMap)
        )
        // The note type's first field is what a word is recognised by in Anki later on.
        assertEquals("Expression", settings.ankiKeyField("Lapis"))
        assertEquals("it belongs to that note type alone", null, settings.ankiKeyField("Basic"))
        assertFalse("a word in another deck is not there, unless asked for", settings.ankiDuplicatesAnywhere)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun ankiSetupSaysWhenAnkiDroidIsMissing() {
        val activity = Robolectric.buildActivity(AnkiSetupActivity::class.java).setup().get()
        TestSupport.await("AnkiDroid status") { text(activity).contains("AnkiDroid is not installed.") }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-anki-no-ankidroid")
    }

    // ---- The reader's page: statistics, the supported games, a page per section ----

    private fun settingsPage(activity: ReaderSettingsActivity): PreferenceFragmentCompat {
        activity.supportFragmentManager.executePendingTransactions()
        TestSupport.idle()
        return activity.supportFragmentManager.findFragmentById(R.id.reader_fragment_container)
            as PreferenceFragmentCompat
    }

    /** Everything on a page in its order; an entry of a group follows the group's heading. */
    private fun entries(group: PreferenceGroup): List<Preference> =
        // What the page shows: a setting this app does not have is hidden.
        (0 until group.preferenceCount).map(group::getPreference).filter { it.isVisible }.flatMap {
            if (it is PreferenceGroup) listOf(it) + entries(it) else listOf(it)
        }

    private fun heading(activity: Activity) =
        TestSupport.find(root(activity), MaterialToolbar::class.java)!!.title.toString()

    @Test
    fun theReadersPageHasStatisticsAndTheGamesFirstThenAnEntryPerSection() {
        val activity = Robolectric.buildActivity(ReaderSettingsActivity::class.java).setup().get()
        val page = settingsPage(activity)
        TestSupport.await("what was read today") { text(activity).contains("Nothing read today") }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-settings")
        assertEquals("Reader", heading(activity))
        assertEquals(
            listOf(
                "Statistics", "Game scripts", "Settings", "Looking words up", "Cursor", "Reader panel",
                "Dictionaries", "Anki and cards", "Statistics options"
            ) + listOfNotNull("Save sync".takeIf { org.readers.app.BuildConfig.READER_SAVE_SYNC }) + listOf("Text output", "CRT picture") +
                listOfNotNull(context.getString(R.string.reader_pref_category_console).takeIf { Console.CONSOLE_PAGE }),
            entries(page.preferenceScreen).map { it.title.toString() }
        )
        val count = ReaderCatalog.games(context).size
        assertEquals(
            context.resources.getQuantityString(R.plurals.reader_games_count, count, count),
            page.findPreference<Preference>("reader_open_games")!!.summary.toString()
        )
        // No setting is on this page: each is one tap further, on its section's page.
        assertTrue(entries(page.preferenceScreen).none { it is SwitchPreferenceCompat || it is SeekBarPreference })
    }

    @Test
    fun theReadersPageSaysWhatWasReadToday() {
        val db = ReaderSession.database(context)
        val now = System.currentTimeMillis()
        db.setGameTitle(game, "Made-up Game")
        db.addActivity(
            db.createSession(game, now - 600_000), game, ReadingRules.readingDay(now),
            ReadingCounters(activeMs = 600_000, characters = 1234, uniqueCharacters = 1234, lines = 40), now
        )
        val activity = Robolectric.buildActivity(ReaderSettingsActivity::class.java).setup().get()
        TestSupport.await("what was read today") { text(activity).contains("Today: 1,234字 · 7,400/h · 10m") }
    }

    @Test
    fun aSectionOpensAsAPageOfItsOwnAndBackReturnsToTheFirst() {
        val activity = Robolectric.buildActivity(ReaderSettingsActivity::class.java).setup().get()
        val first = settingsPage(activity)
        val entry = first.findPreference<Preference>("reader_section_cursor")!!
        entry.onPreferenceClickListener!!.onPreferenceClick(entry)

        val cursor = settingsPage(activity)
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-settings-cursor")
        assertEquals(ReaderSettingsActivity.Section.CURSOR, activity.openSection)
        assertEquals("Cursor", heading(activity))
        assertEquals(
            listOfNotNull(
                "reader_cursor_button", "reader_next_choice_button".takeIf { Console.HAS_NEXT_CHOICE },
                "reader_cursor_toggle", "reader_cursor_frame",
                "reader_popup_hide_on_exit", "reader_cursor_home",
                "reader_second_screen".takeIf { Console.SECOND_DISPLAY_FREE }, "reader_cursor_speed"
            ),
            entries(cursor.preferenceScreen).mapNotNull { it.key }
        )
        assertTrue(text(activity), text(activity).contains("Press once instead of holding"))

        activity.onBackPressedDispatcher.onBackPressed()
        settingsPage(activity)
        assertNull(activity.openSection)
        assertEquals("Reader", heading(activity))
        assertFalse("still open", activity.isFinishing)
    }

    @Test
    fun everySettingIsOnOnePageAndNothingIsExplainedAtLength() {
        val activity = Robolectric.buildActivity(ReaderSettingsActivity::class.java).setup().get()
        val keys = ArrayList<String>()
        // The app's own page has settings of its own (its ConsoleTest knows them).
        val own = ArrayList<String>()
        val pages = ReaderSettingsActivity.Section.values().map { section ->
            activity.open(section)
            settingsPage(activity).also { page ->
                (if (section == ReaderSettingsActivity.Section.CONSOLE) own else keys) +=
                    entries(page.preferenceScreen).mapNotNull { it.key }
            }
        } + run {
            repeat(ReaderSettingsActivity.Section.values().size) { activity.onBackPressedDispatcher.onBackPressed() }
            settingsPage(activity)
        }
        assertEquals(
            listOfNotNull(
                ReaderSettings.KEY_SCREEN_TEXT, ReaderSettings.KEY_TOUCH_PLAY, ReaderSettings.KEY_POPUP_SCALE,
                ReaderSettings.KEY_POPUP_AUTOHIDE,
                ReaderSettings.KEY_POPUP_HEIGHT, ReaderSettings.KEY_CARD_CORNER,
                ReaderSettings.KEY_WORD_MARK, ReaderSettings.KEY_POPUP_THEME, ReaderSettings.KEY_POPUP_CSS,
                "reader_popup_import", "reader_popup_export", ReaderSettings.KEY_POPUP_MARK_UNKNOWN,
                ReaderSettings.KEY_POPUP_LOOKUPS,
                ReaderSettings.KEY_SCREEN_TEXT_OUTLINE,
                ReaderSettings.KEY_CURSOR_BUTTON, ReaderSettings.KEY_NEXT_CHOICE_BUTTON.takeIf { Console.HAS_NEXT_CHOICE },
                ReaderSettings.KEY_CURSOR_TOGGLE, ReaderSettings.KEY_CURSOR_FRAME,
                ReaderSettings.KEY_POPUP_HIDE_ON_EXIT, ReaderSettings.KEY_CURSOR_HOME,
                ReaderSettings.KEY_SECOND_SCREEN.takeIf { Console.SECOND_DISPLAY_FREE }, ReaderSettings.KEY_CURSOR_SPEED,
                ReaderSettings.KEY_PANEL_ENABLED, ReaderSettings.KEY_PANEL_AUTO_HIDE,
                ReaderSettings.KEY_PANEL_DISPLAY, ReaderSettings.KEY_PANEL_POSITION, ReaderSettings.KEY_FONT_SIZE,
                ReaderSettings.KEY_PANEL_HEIGHT, ReaderSettings.KEY_PANEL_OPACITY, ReaderSettings.KEY_FURIGANA,
                ReaderSettings.KEY_SHOW_CHOICES, ReaderSettings.KEY_REREAD_COLOUR, ReaderSettings.KEY_ADVANCE_BUTTON,
                "reader_open_anki", ReaderSettings.KEY_CARD_AUDIO, ReaderSettings.KEY_CAPTURE_AUDIO,
                ReaderSettings.KEY_AUDIO_MAX, ReaderSettings.KEY_AUDIO_LEAD, ReaderSettings.KEY_SCREENSHOT_CACHE,
                ReaderSettings.KEY_SCREENSHOT_WIDTH,
                ReaderSettings.KEY_SESSION_HUD, ReaderSettings.KEY_STATS_STYLE, ReaderSettings.KEY_STATS_PAUSED,
                ReaderSettings.KEY_STATS_BUTTON,
                ReaderSettings.KEY_IDLE_MINUTES,
                ReaderSettings.KEY_CLIPBOARD_AUTO
            ),
            keys
        )
        assertEquals(Console.CONSOLE_PAGE, own.isNotEmpty())

        // A summary says a thing in a line or two; what needs more belongs in the guide.
        for (page in pages) {
            for (entry in entries(page.preferenceScreen)) {
                val summary = entry.summary?.toString().orEmpty()
                assertTrue("${entry.key ?: entry.title}: $summary", summary.length <= 120)
            }
        }
    }

    @Test
    fun aScriptFromThePublishedListIsInstalledAndRemoved() {
        // The published list, served by a stand-in: a made-up game's script the app does not have.
        val id = Console.Test.SCRIPT_GAME
        val madeUp = "-- reader: no hooks\n-- reader: title 作り話\n-- reader: reads The message window\nlocal x = 1\n"
            .toByteArray()
        val list = org.json.JSONObject().put("format", 1).put("system", "test").put(
            "scripts", org.json.JSONArray().put(
                org.json.JSONObject()
                    .put("id", id).put("title", "作り話").put("reads", "").put("version", "").put("api", 1)
                    .put("file", "$id.lua").put("size", madeUp.size).put("sha256", ScriptStore.sha256(madeUp))
            )
        )
        val served = mapOf(
            "https://example.invalid/scripts/index.json" to list.toString().toByteArray(),
            "https://example.invalid/scripts/$id.lua" to madeUp,
        )
        val fetched = ArrayList<String>()
        ReaderCatalog.forget()
        ScriptStore.indexUrl = "https://example.invalid/scripts/index.json"
        ScriptStore.fetcher = { url -> fetched.add(url); served[url] ?: throw ScriptStore.ScriptException("404 $url") }
        try {
            val activity = Robolectric.buildActivity(ReaderGamesActivity::class.java).setup().get()
            TestSupport.await("the list") { text(activity).contains("$id · Not installed") }
            val shown = text(activity)
            assertTrue(shown, shown.contains("1 script in the published list.") || shown.contains("1 scripts in the published list."))

            val install = TestSupport.find(root(activity), android.widget.Button::class.java) { it.text == "Install" }!!
            install.performClick()
            TestSupport.await("installed") { text(activity).contains("Installed the script of 作り話.") && text(activity).contains("$id · Installed") }
            val file = java.io.File(context.getExternalFilesDir(null), "reader/profiles/$id.lua")
            assertTrue(file.readBytes().contentEquals(madeUp))
            assertTrue(fetched.contains("https://example.invalid/scripts/$id.lua"))
            assertTrue(ReaderCatalog.covers(context, id))

            // An installed script has a Remove button; removing it leaves the list's to install.
            TestSupport.find(root(activity), android.widget.Button::class.java) { it.text == "Remove" }!!.performClick()
            TestSupport.idle()
            (org.robolectric.shadows.ShadowDialog.getLatestDialog() as androidx.appcompat.app.AlertDialog)
                .getButton(android.content.DialogInterface.BUTTON_POSITIVE).performClick()
            TestSupport.await("removed") { text(activity).contains("$id · Not installed") }
            assertFalse(file.exists())
        } finally {
            ScriptStore.indexUrl = ScriptStore.DEFAULT_INDEX_URL
            ScriptStore.fetcher = { throw ScriptStore.ScriptException("no network in tests") }
            java.io.File(context.getExternalFilesDir(null), "reader/profiles").deleteRecursively()
            ReaderCatalog.forget()
        }
    }

    @Test
    fun theSupportedGamesAreListedWithWhatIsReadOfThem() {
        // A made-up game, with a profile in the app's folder as a user would put one there.
        val folder = java.io.File(context.getExternalFilesDir(null), "reader/profiles").apply { mkdirs() }
        java.io.File(folder, "${Console.Test.GAME}.lua").writeText(
            "-- reader: title 作り話 (A Made-up Game)\n-- reader: reads The story's text and choices\n" +
                "-- reader: version ${Console.Test.GAME}, disc version 1.001\n"
        )
        ReaderCatalog.forget()
        // No published list here: a stand-in that has none.
        ScriptStore.fetcher = { throw ScriptStore.ScriptException("no network in tests") }
        val activity = Robolectric.buildActivity(ReaderGamesActivity::class.java).setup().get()
        TestSupport.await("the games") { text(activity).contains("作り話") }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-games")
        val shown = text(activity)
        assertTrue(shown, shown.contains("Game scripts"))
        assertTrue(shown, shown.contains("作り話 (A Made-up Game)"))
        assertTrue(shown, shown.contains("The story's text and choices"))
        assertTrue(shown, shown.contains("${Console.Test.GAME}, disc version 1.001"))
        assertTrue(shown, shown.contains("book badge in the game list"))
        ReaderCatalog.forget()
    }

    @Test
    fun backlogListsLinesOldestFirstAndLooksWordsUp() {
        installDictionary()
        val db = ReaderSession.database(context)
        val lines = listOf(
            Triple("声", "…君は旅人だ？\n道を見張れ", listOf(RubySpan(1, 1, "きみ"), RubySpan(3, 2, "たびびと"), RubySpan(8, 1, "みち"), RubySpan(10, 2, "みは"))),
            Triple("少年", "私の名前は　まだない…", emptyList()),
            Triple("", "太郎は花子と共に\n港の朝市にやってきた", emptyList())
        )
        lines.forEachIndexed { index, (speaker, body, ruby) ->
            db.insertLine(
                game,
                TextLine(0, 0, -1, "dialogue", speaker, body, ruby, "s$index", emptyMap(), 1_759_000_000_000 + index * 60_000),
                LineAccounting(ReadingRules.countCharacters(body), index == 1, true, null)
            )
        }

        val intent = Intent(context, ReaderBacklogActivity::class.java).putExtra(ReaderBacklogActivity.EXTRA_GAME, game)
        val activity = Robolectric.buildActivity(ReaderBacklogActivity::class.java, intent).setup().get()
        val list = TestSupport.find(root(activity), RecyclerView::class.java)!!
        TestSupport.await("backlog rows") { list.adapter!!.itemCount == 3 }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-backlog")
        val shown = text(activity)
        assertTrue(shown, shown.indexOf("道を見張れ") < shown.indexOf("私の名前は"))
        assertTrue(shown, shown.indexOf("私の名前は") < shown.indexOf("港の朝市"))
        assertTrue(shown, shown.contains("声 · "))

        // A tap on 見張れ in the first row opens the popup with the dictionary entry.
        val first = TestSupport.find(list, ReaderTextView::class.java) { it.text.toString().contains("見張れ") }!!
        first.onCharacterTap!!.invoke(10, android.graphics.Rect())
        TestSupport.await("popup") {
            TestSupport.find(root(activity), LookupPopupView::class.java) { it.visibility == View.VISIBLE } != null
        }
        TestSupport.snapshot(root(activity), 1920, 1080, "screen-backlog-lookup")
        val popup = TestSupport.find(root(activity), LookupPopupView::class.java)!!
        assertTrue(TestSupport.allText(popup).contains("to keep watch"))

        // Searching narrows the list; clearing the search brings everything back.
        val search = TestSupport.find(root(activity), EditText::class.java)!!
        search.setText("朝市")
        TestSupport.await("search result") { list.adapter!!.itemCount == 1 }
        search.setText("")
        TestSupport.await("full list again") { list.adapter!!.itemCount == 3 }
    }

    @Test
    fun backlogOfAGameWithNoLinesSaysSo() {
        val intent = Intent(context, ReaderBacklogActivity::class.java).putExtra(ReaderBacklogActivity.EXTRA_GAME, game)
        val activity = Robolectric.buildActivity(ReaderBacklogActivity::class.java, intent).setup().get()
        TestSupport.await("empty backlog message") {
            TestSupport.find(root(activity), TextView::class.java) {
                it.text.toString() == "Nothing read yet in this game." && it.visibility == View.VISIBLE
            } != null
        }
    }
}
