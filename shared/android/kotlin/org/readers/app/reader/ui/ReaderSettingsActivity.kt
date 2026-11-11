// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.text.InputType
import android.widget.LinearLayout
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.FragmentContainerView
import androidx.preference.EditTextPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import java.util.concurrent.Executors
import org.readers.app.R
import org.readers.app.reader.ReaderCatalog
import org.readers.app.reader.ReaderSession
import org.readers.app.reader.ReaderSettings
import org.readers.app.reader.stats.ReadingRules
import org.readers.app.reader.stats.SessionSummary

/**
 * The reader's own page of the app: statistics and the supported games first, then one entry
 * per page of settings, the pages Furoshiki has on a computer (Reading, Popup, Cards, Sources,
 * Controller). A page shows its few most changed settings and keeps the rest behind "More";
 * "Find a setting" on the first page finds any of them by its name. Back returns to the first.
 */
class ReaderSettingsActivity : ReaderScreen() {
    override val titleRes = R.string.reader_title

    /** A page of settings: the entry of the first page that opens it, its heading, its content. */
    enum class Section(val key: String, val title: Int, val preferences: Int) {
        READING("reader_section_reading", R.string.reader_section_reading, R.xml.reader_prefs_reading),
        POPUP("reader_section_popup", R.string.reader_section_popup, R.xml.reader_prefs_popup),
        CARDS("reader_section_cards", R.string.reader_section_cards, R.xml.reader_prefs_cards),
        SOURCES("reader_section_sources", R.string.reader_section_sources, R.xml.reader_prefs_sources),
        CONTROLLER("reader_section_controller", R.string.reader_section_controller, R.xml.reader_prefs_controller),

        /** The app's own page, where it has one (Console.CONSOLE_PAGE; reader_prefs_console.xml). */
        CONSOLE("reader_section_console", R.string.reader_pref_category_console, R.xml.reader_prefs_console)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val container = FragmentContainerView(this).apply { id = R.id.reader_fragment_container }
        content.addView(container, LinearLayout.LayoutParams(MATCH, MATCH))
        if (savedInstanceState == null) {
            // Asked for one section (the app's setup screen asks for Save sync): its page
            // alone, and Back leaves.
            val first = directSection?.let { SectionPage.of(it) } ?: FirstPage()
            supportFragmentManager.beginTransaction()
                .replace(R.id.reader_fragment_container, first)
                .commit()
        }
        // The arrow goes where Back goes: from a section to the first page, from there out.
        toolbar.setNavigationOnClickListener { onBackPressedDispatcher.onBackPressed() }
        supportFragmentManager.addOnBackStackChangedListener { showTitle() }
        showTitle()
    }

    override fun onPause() {
        // The game screen may not be attached to hear preference changes: apply them here too.
        ReaderSession.onSettingsChanged()
        super.onPause()
    }

    /** The section this screen was opened for, when it was opened for one. */
    private val directSection: Section? get() = section(intent?.getStringExtra(SECTION))

    /** The section whose page is open, or null on the first page. */
    val openSection: Section?
        get() = supportFragmentManager.run {
            if (backStackEntryCount == 0) directSection else section(getBackStackEntryAt(backStackEntryCount - 1).name)
        }

    /** Opens a page; with [focus], all of it (its "More" opened) and at that setting. */
    fun open(section: Section, focus: String? = null) {
        supportFragmentManager.beginTransaction()
            .replace(R.id.reader_fragment_container, SectionPage.of(section, focus))
            .addToBackStack(section.name)
            .commit()
    }

    private fun showTitle() {
        toolbar.setTitle(openSection?.title ?: titleRes)
    }

    /** Statistics and the supported games, then the sections. */
    class FirstPage : PreferenceFragmentCompat() {
        private val worker = Executors.newSingleThreadExecutor()

        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.reader_preferences, rootKey)
            link("reader_open_stats", ReaderStatsActivity::class.java)
            link("reader_open_games", ReaderGamesActivity::class.java)
            link("reader_open_crt", CrtSettingsActivity::class.java)
            link("reader_open_dictionaries", DictionaryManagerActivity::class.java)
            findPreference<Preference>("reader_find_setting")?.setOnPreferenceClickListener {
                findDialog()
                true
            }
            if (!org.readers.app.reader.Console.CONSOLE_PAGE) {
                findPreference<Preference>(Section.CONSOLE.key)?.isVisible = false
            }
            for (section in Section.values()) {
                findPreference<Preference>(section.key)?.setOnPreferenceClickListener {
                    (activity as? ReaderSettingsActivity)?.open(section)
                    true
                }
            }
        }

        override fun onResume() {
            super.onResume()
            // What was read today, and how many games can be read, where they are opened from.
            val context = requireContext().applicationContext
            ReaderSession.flush {
                worker.execute {
                    val today = ReaderSession.database(context)
                        .dayTotals(ReadingRules.readingDay(System.currentTimeMillis()))
                    val games = ReaderCatalog.games(context).size
                    activity?.runOnUiThread {
                        if (!isAdded) return@runOnUiThread
                        findPreference<Preference>("reader_open_stats")?.summary = if (today.isEmpty) {
                            getString(R.string.reader_statistics_nothing_today)
                        } else {
                            getString(R.string.reader_statistics_today, SessionSummary.format(today))
                        }
                        findPreference<Preference>("reader_open_games")?.summary =
                            resources.getQuantityString(R.plurals.reader_games_count, games, games)
                    }
                }
            }
        }

        override fun onDestroy() {
            worker.shutdown()
            super.onDestroy()
        }

        /** What is typed finds settings by name; one tapped opens its page at it. */
        private fun findDialog() {
            val context = requireContext()
            val field = android.widget.EditText(context).apply {
                hint = getString(R.string.reader_find_setting)
                inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                isSingleLine = true
            }
            val list = android.widget.ListView(context)
            var found = emptyList<Found>()
            val adapter = android.widget.ArrayAdapter<String>(context, android.R.layout.simple_list_item_1)
            list.adapter = adapter
            val box = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                val pad = (16 * resources.displayMetrics.density).toInt()
                setPadding(pad, pad / 2, pad, 0)
                addView(field)
                addView(list)
            }
            val dialog = androidx.appcompat.app.AlertDialog.Builder(context)
                .setView(box)
                .setNegativeButton(android.R.string.cancel, null)
                .create()
            field.addTextChangedListener(object : android.text.TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
                override fun afterTextChanged(s: android.text.Editable?) {
                    found = find(context, s?.toString().orEmpty())
                    adapter.clear()
                    adapter.addAll(found.map { "${it.title}  ·  ${getString(it.section.title)}" })
                }
            })
            list.setOnItemClickListener { _, _, position, _ ->
                val chosen = found.getOrNull(position) ?: return@setOnItemClickListener
                dialog.dismiss()
                (activity as? ReaderSettingsActivity)?.open(chosen.section, chosen.key)
            }
            dialog.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE)
            dialog.show()
            field.requestFocus()
        }

        private fun link(key: String, screen: Class<*>) {
            findPreference<Preference>(key)?.setOnPreferenceClickListener {
                startActivity(Intent(requireContext(), screen))
                true
            }
        }
    }

    /** One section's settings. */
    class SectionPage : PreferenceFragmentCompat() {
        // A popup style file (PopupStyle), in and out.
        private val importStyle = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@registerForActivityResult
            val context = requireContext()
            val failure = runCatching {
                val text = context.contentResolver.openInputStream(uri)!!.bufferedReader().use { it.readText() }
                PopupStyle.import(org.json.JSONObject(text), androidx.preference.PreferenceManager.getDefaultSharedPreferences(context))
            }.getOrElse { it.message ?: it.javaClass.simpleName }
            Toast.makeText(
                context,
                if (failure == null) getString(R.string.reader_popup_style_imported) else getString(R.string.reader_popup_style_failed, failure),
                Toast.LENGTH_LONG
            ).show()
            if (failure == null) {
                // The page shows the imported values.
                onCreatePreferences(null, null)
            }
        }
        private val exportStyle = registerForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
            if (uri == null) return@registerForActivityResult
            val context = requireContext()
            runCatching {
                context.contentResolver.openOutputStream(uri)!!.use {
                    it.write(PopupStyle.export(ReaderSettings(context)).toString(2).toByteArray())
                }
            }.onSuccess {
                Toast.makeText(context, R.string.reader_popup_style_exported, Toast.LENGTH_SHORT).show()
            }
        }

        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            val section = section(requireArguments().getString(SECTION)) ?: Section.READING
            // Play by touch's first value depends on the device: kept before its switch shows it.
            ReaderSettings(requireContext()).keepTouchPlayDefault()
            setPreferencesFromResource(section.preferences, rootKey)
            // Come to from "Find a setting": the whole page, at that setting.
            requireArguments().getString(FOCUS)?.let { focus ->
                preferenceScreen.initialExpandedChildrenCount = Int.MAX_VALUE
                scrollToPreference(focus)
            }
            // Going on to the next choice, only where the emulator has it.
            if (!org.readers.app.reader.Console.HAS_NEXT_CHOICE) {
                findPreference<Preference>(ReaderSettings.KEY_NEXT_CHOICE_BUTTON)?.isVisible = false
            }
            // The second display as the reader's trackpad, only where the emulator leaves it free.
            if (!org.readers.app.reader.Console.SECOND_DISPLAY_FREE) {
                findPreference<Preference>(ReaderSettings.KEY_SECOND_SCREEN)?.isVisible = false
            }
            findPreference<EditTextPreference>(ReaderSettings.KEY_POPUP_CSS)?.setOnBindEditTextListener {
                it.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                    InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                it.minLines = 6
                it.typeface = android.graphics.Typeface.MONOSPACE
            }
            findPreference<Preference>("reader_popup_import")?.setOnPreferenceClickListener {
                importStyle.launch(arrayOf("application/json", "text/plain", "application/octet-stream"))
                true
            }
            findPreference<Preference>("reader_popup_export")?.setOnPreferenceClickListener {
                exportStyle.launch("popup-style.json")
                true
            }
            findPreference<Preference>("reader_open_anki")?.setOnPreferenceClickListener {
                startActivity(Intent(requireContext(), AnkiSetupActivity::class.java))
                true
            }
            findPreference<EditTextPreference>("reader_texthook_port")?.apply {
                setOnBindEditTextListener { it.inputType = InputType.TYPE_CLASS_NUMBER }
                setOnPreferenceChangeListener { _, value ->
                    (value as? String)?.toIntOrNull()?.let { it in 1024..65535 } == true
                }
            }
        }


        override fun onResume() {
            super.onResume()
        }

        override fun onPause() {
            super.onPause()
        }

        companion object {
            fun of(section: Section, focus: String? = null) = SectionPage().apply {
                arguments = Bundle().apply {
                    putString(SECTION, section.name)
                    putString(FOCUS, focus)
                }
            }
        }
    }

    companion object {
        private const val SECTION = "section"
        private const val FOCUS = "focus"

        /** A setting as "Find a setting" lists it: its name, and the page it is on. */
        data class Found(val section: Section, val key: String, val title: String)

        /**
         * Every setting of every page whose name holds [text] (a word of it beginning so first),
         * those the app does not show left out.
         */
        fun find(context: Context, text: String): List<Found> {
            val wanted = text.trim()
            if (wanted.isEmpty()) return emptyList()
            val manager = androidx.preference.PreferenceManager(context)
            val starts = ArrayList<Found>()
            val holds = ArrayList<Found>()
            for (section in Section.values()) {
                if (section == Section.CONSOLE && !org.readers.app.reader.Console.CONSOLE_PAGE) continue
                val screen = manager.inflateFromResource(context, section.preferences, null)
                for (index in 0 until screen.preferenceCount) {
                    val preference = screen.getPreference(index)
                    val key = preference.key ?: continue
                    val title = preference.title?.toString().orEmpty()
                    if (key == ReaderSettings.KEY_NEXT_CHOICE_BUTTON && !org.readers.app.reader.Console.HAS_NEXT_CHOICE) continue
                    if (key == ReaderSettings.KEY_SECOND_SCREEN && !org.readers.app.reader.Console.SECOND_DISPLAY_FREE) continue
                    val found = Found(section, key, title)
                    when {
                        title.split(' ').any { it.startsWith(wanted, ignoreCase = true) } -> starts += found
                        title.contains(wanted, ignoreCase = true) -> holds += found
                    }
                }
            }
            return starts + holds
        }


        private fun section(name: String?): Section? = Section.values().firstOrNull { it.name == name }

        /** Opens the screen at one section's page instead of its first page. */
        fun intent(context: Context, section: Section): Intent =
            Intent(context, ReaderSettingsActivity::class.java).putExtra(SECTION, section.name)
    }
}
