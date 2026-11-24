// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.CheckBox
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import com.google.android.material.color.MaterialColors
import io.github.sommepo.furoshiki.reader.ReaderCatalog
import io.github.sommepo.furoshiki.reader.ReaderNative
import io.github.sommepo.furoshiki.reader.ReaderSession
import io.github.sommepo.furoshiki.reader.ScriptStore
import io.github.sommepo.furoshiki.reader.dict.DictionaryStore
import io.github.sommepo.furoshiki.reader.dict.NamesDictionaries
import io.github.sommepo.furoshiki.reader.ui.ReaderScreen
import java.io.File
import java.util.concurrent.Executors
import org.json.JSONArray
import org.json.JSONObject

/**
 * Settings › Game scripts, every console's games at once (the maintainer, 2026-11-23: "top level
 * game scripts in settings, in the same style as the new dictionaries page, which also shows status
 * of names pulled from vndb AND box art/spine art ... and the ability to pull if not", then "extend
 * the work to android"): the desktop's page (`home/src/scripts_panel.*`) for a finger and a
 * controller. Each console's published list, the user's and the app's own scripts, the library's
 * games, by console: each game's script, its names dictionary, its cover and spine. A tap marks a
 * game, a long press offers its own actions; the bar at the foot acts on the marked ones. In the
 * reader's process, as the reader's settings and dictionaries are kept there.
 */
class GameScriptsActivity : ReaderScreen() {
    override val titleRes = R.string.reader_games

    private val worker = Executors.newSingleThreadExecutor()
    private lateinit var folder: ScriptStore.Folder
    private lateinit var list: ListView
    private lateinit var status: TextView
    private lateinit var marks: TextView
    private lateinit var chips: LinearLayout
    private lateinit var states: LinearLayout
    private lateinit var search: EditText

    private enum class Script(val said: String) {
        NONE("No script"), NOT_INSTALLED("Not installed"), INSTALLED("Installed"), BUILT_IN("Built-in"),
        UPDATE("Update"), OWN("Custom"), TOO_NEW("Update app")
    }
    private enum class Names(val said: String) { NONE("None"), AVAILABLE("Available"), READY("Ready"), INSTALLED("Installed") }
    private enum class Filter(val said: String) {
        ANY("All"), LIBRARY("In library"), NOT_LIBRARY("Not in library"), NO_SCRIPT("Script missing"),
        NO_NAMES("Names missing"), NO_ART("Box art missing"), UPDATES("Updates")
    }

    private data class Game(
        val id: String,
        val title: String,
        val console: String,
        val script: Script,
        val names: Names,
        val cover: Boolean,
        val spine: Boolean,
        val inLibrary: Boolean,
        val entry: ScriptStore.Entry?,
        val path: String,
        val reads: String,
        /** Every disc's ID: a game of several discs is one row (the maintainer, 2026-11-23). */
        val ids: List<String> = listOf(id),
        val english: String = "",
    )

    private val lists = HashMap<String, List<ScriptStore.Entry>>()
    private var games: List<Game> = emptyList()
    private var shown: List<Game> = emptyList()
    private val marked = LinkedHashSet<String>()
    private var console = ""
    private var filter = Filter.ANY
    private var busy = false

    private val pick = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) installChosen(uris)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        console = intent.getStringExtra(EXTRA_CONSOLE).orEmpty()
        folder = ScriptStore.Folder(File(getExternalFilesDir(null), ReaderNative.PROFILE_USER_DIR))
        val pad = dp(12f)
        search = EditText(this).apply {
            hint = "Search games"
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                override fun afterTextChanged(s: Editable?) = show()
            })
        }
        content.addView(search, LinearLayout.LayoutParams(MATCH, WRAP).apply { setMargins(pad, 0, pad, 0) })
        chips = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        content.addView(HorizontalScrollView(this).apply { addView(chips); setPadding(pad, dp(4f), pad, 0) })
        states = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val top = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        top.addView(states, LinearLayout.LayoutParams(0, WRAP, 1f))
        content.addView(HorizontalScrollView(this).apply { addView(top); setPadding(pad, 0, pad, 0) })
        val tools = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        tools.addView(action("Update all") { installAll() })
        tools.addView(action("From file…") { pick.launch(arrayOf("*/*")) })
        tools.addView(action("Refresh") { fetchLists() })
        tools.addView(action("Box art sources…") { sourcesDialog() })
        content.addView(HorizontalScrollView(this).apply { addView(tools); setPadding(pad, dp(2f), pad, dp(2f)) })
        status = note("").apply { setPadding(pad, dp(2f), pad, dp(4f)) }
        content.addView(status)
        list = ListView(this).apply {
            divider = null
            itemsCanFocus = false
            adapter = rows
            setOnItemClickListener { _, _, at, _ -> toggle(shown[at].id) }
            setOnItemLongClickListener { _, _, at, _ -> actions(listOf(shown[at].id), shown[at].title); true }
        }
        content.addView(list, LinearLayout.LayoutParams(MATCH, 0, 1f))
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(pad, dp(4f), pad, dp(6f))
        }
        marks = note("").apply { setPadding(0, 0, dp(8f), 0) }
        bar.addView(marks)
        bar.addView(action("Install scripts") { install(markedOrAsk() ?: return@action) })
        bar.addView(action("Install names") { names(markedOrAsk() ?: return@action) })
        bar.addView(action("Find box art") { art(markedOrAsk() ?: return@action) })
        bar.addView(action("Remove scripts…") { remove(markedOrAsk() ?: return@action) })
        bar.addView(action("Mark all") { markAll() })
        content.addView(HorizontalScrollView(this).apply { addView(bar) })
        build()
        fetchLists()
    }

    override fun onDestroy() {
        worker.shutdown()
        super.onDestroy()
    }

    private fun colour(attr: Int) = MaterialColors.getColor(content, attr)
    private val ink get() = colour(com.google.android.material.R.attr.colorOnSurface)
    private val muted get() = colour(com.google.android.material.R.attr.colorOnSurfaceVariant)
    private val accent get() = colour(com.google.android.material.R.attr.colorPrimary)

    private fun action(text: String, onClick: () -> Unit) = TextView(this).apply {
        this.text = text.uppercase()
        typeface = Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        letterSpacing = 0.08f
        setTextColor(ink)
        setPadding(dp(10f), dp(8f), dp(10f), dp(8f))
        isFocusable = true
        isClickable = true
        background = android.graphics.drawable.GradientDrawable().apply {
            setStroke(dp(1f), muted)
        }
        setOnClickListener { onClick() }
        layoutParams = LinearLayout.LayoutParams(WRAP, WRAP).apply { marginEnd = dp(6f) }
    }

    private fun chip(text: String, on: Boolean, onClick: () -> Unit) = TextView(this).apply {
        this.text = text.uppercase()
        typeface = Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
        letterSpacing = 0.06f
        setTextColor(if (on) ink else muted)
        setPadding(dp(4f), dp(8f), dp(12f), dp(8f))
        if (on) paintFlags = paintFlags or android.graphics.Paint.UNDERLINE_TEXT_FLAG
        isFocusable = true
        isClickable = true
        setOnClickListener { onClick() }
    }

    private fun submit(task: () -> Unit) {
        if (!worker.isShutdown && !isDestroyed) runCatching { worker.execute(task) }
    }

    /** Every console's list, one after the other, then the rows again. */
    private fun fetchLists() {
        if (busy) return
        busy = true
        status.text = "Loading script lists…"
        submit {
            val problems = ArrayList<String>()
            for ((key, folderName) in LISTS) {
                runCatching { ScriptStore.parseIndex(ScriptStore.fetcher("$ROOT/$folderName/index.json")) }
                    .onSuccess { lists[key] = it }
                    .onFailure { problems.add("${Places.shortName(key)}: ${it.message ?: it.javaClass.simpleName}") }
            }
            runOnUiThread {
                if (isDestroyed || isFinishing) return@runOnUiThread
                busy = false
                status.text = if (problems.isEmpty()) "" else {
                    val count = if (problems.size == 1) "1 script list" else "${problems.size} script lists"
                    "Couldn't load $count: " + problems.joinToString("; ")
                }
                build()
            }
        }
    }

    private fun bundledScript(id: String): ByteArray? = try {
        assets.open("${ReaderNative.PROFILE_ASSET_DIR}/$id.lua").use { it.readBytes() }
    } catch (_: Exception) {
        null
    }

    /** Reads everything again and lays the rows out. */
    private fun build() {
        submit {
            val built = runCatching { JSONObject(assets.open("front/titles.json").bufferedReader().use { it.readText() }) }.getOrDefault(JSONObject())
            val packs = NamesDictionaries.find(NamesDictionaries.folders(applicationContext, folder.dir))
            val inDictionaries = runCatching {
                DictionaryStore.get(applicationContext).database.dictionaries().flatMap { it.scope }.map { it.uppercase() }.toSet()
            }.getOrDefault(emptySet())
            val library = HashMap<String, Triple<String, String, String>>() // UPPER_ID -> (originalId, console, path)
            val libraryTitles = HashMap<String, String>()
            for (key in Places.CORES.keys) {
                val listed = runCatching { JSONArray(Places.gamesFile(this, key).readText()) }.getOrNull() ?: continue
                for (i in 0 until listed.length()) {
                    val one = listed.optJSONObject(i) ?: continue
                    val rawId = one.optString("id")
                    val upper = rawId.uppercase()
                    library[upper] = Triple(rawId, key, one.optString("path"))
                    libraryTitles[upper] = one.optString("title")
                }
            }
            fun names(id: String, listed: Boolean) = when {
                id.uppercase() in inDictionaries -> Names.INSTALLED
                NamesDictionaries.forGame(id.uppercase(), packs) != null -> Names.READY
                listed -> Names.AVAILABLE
                else -> Names.NONE
            }
            fun art(id: String, key: String): Pair<Boolean, Boolean> {
                val safe = Covers.safeId(id)
                val record = built.optJSONObject(id)
                val cover = !record?.optString("cover").isNullOrEmpty() ||
                    TYPES.any { File(Places.console(this, key, "covers"), "$safe.$it").isFile } ||
                    library[id.uppercase()]?.third?.takeIf { it.isNotEmpty() }?.let { Covers.of(this, key, id, File(it)) } != null
                val spine = !record?.optString("spine").isNullOrEmpty() || Covers.spineOf(this, key, id) != null
                return cover to spine
            }
            val byId = LinkedHashMap<String, Game>()
            for ((key, entries) in lists) {
                for (entry in entries) {
                    val upper = entry.id.uppercase()
                    val user = folder.script(entry.id)
                    val bundled = bundledScript(entry.id)
                    val script = when (ScriptStore.stateOf(entry, user, bundled, folder.recorded(entry.id))) {
                        ScriptStore.State.NOT_INSTALLED -> Script.NOT_INSTALLED
                        ScriptStore.State.INSTALLED -> if (user != null) Script.INSTALLED else Script.BUILT_IN
                        ScriptStore.State.UPDATE -> Script.UPDATE
                        ScriptStore.State.OWN -> Script.OWN
                        ScriptStore.State.TOO_NEW -> Script.TOO_NEW
                    }
                    val (cover, spine) = art(entry.id, key)
                    byId[upper] = Game(entry.id, entry.title, key, script, names(entry.id, entry.names != null), cover, spine,
                        upper in library, entry, library[upper]?.third.orEmpty(), entry.reads)
                }
            }
            for (game in ReaderCatalog.games(applicationContext)) {
                val upper = game.titleId.uppercase()
                if (upper in byId) continue
                val key = library[upper]?.second ?: consoleOfId(game.titleId)
                val (cover, spine) = art(game.titleId, key)
                byId[upper] = Game(game.titleId, game.title, key, if (game.own) Script.OWN else Script.BUILT_IN,
                    names(game.titleId, false), cover, spine, upper in library, null, library[upper]?.third.orEmpty(), game.reads)
            }
            for ((upper, where) in library) {
                if (upper in byId) continue
                val (rawId, key, path) = where
                val (cover, spine) = art(rawId, key)
                byId[upper] = Game(rawId, libraryTitles[upper].orEmpty().ifEmpty { rawId }, key, Script.NONE, names(rawId, false),
                    cover, spine, true, null, path, "")
            }
            val order = Places.CORES.keys.toList()
            val each = byId.values.map { it.copy(english = built.optJSONObject(it.id)?.optString("english").orEmpty()) }
            val merged = mergeDiscs(each)
            val sorted = merged.sortedWith(compareBy<Game>({ order.indexOf(it.console).let { at -> if (at < 0) 99 else at } }, { it.title.lowercase() }))
            runOnUiThread {
                if (isDestroyed || isFinishing) return@runOnUiThread
                discGames = each.associateBy { it.id }
                games = sorted
                marked.retainAll(games.map { it.id }.toSet())
                show()
            }
        }
    }

    /**
     * A game of several discs as one row: the same title on the same console once "(Disc 2)" or
     * "ディスク2" is set aside. The row's state is what wants doing first; its art the library disc's,
     * else the first disc's that has some.
     */
    private fun mergeDiscs(games: List<Game>): List<Game> {
        val rank = listOf(Script.UPDATE, Script.NOT_INSTALLED, Script.OWN, Script.INSTALLED, Script.BUILT_IN, Script.TOO_NEW, Script.NONE)
        val namesRank = listOf(Names.AVAILABLE, Names.READY, Names.INSTALLED, Names.NONE)
        return games.groupBy {
            val base = it.title.replace(DISC_END, "").trim().ifEmpty { it.id }
            it.console + "|" + base.lowercase()
        }.values.map { discs ->
            if (discs.size == 1) return@map discs[0]
            val sorted = discs.sortedBy { it.id }
            val face = sorted.firstOrNull { it.inLibrary } ?: sorted.firstOrNull { it.cover } ?: sorted[0]
            face.copy(
                title = face.title.replace(DISC_END, "").trim().ifEmpty { face.title },
                script = discs.minByOrNull { rank.indexOf(it.script) }!!.script,
                names = discs.minByOrNull { namesRank.indexOf(it.names) }!!.names,
                cover = face.cover,
                spine = face.spine,
                inLibrary = discs.any { it.inLibrary },
                ids = sorted.map { it.id },
            )
        }
    }

    /** The rows the filters let through, and the filters themselves. */
    private fun show() {
        val wanted = search.text.toString().trim().split(' ').filter { it.isNotEmpty() }
        shown = games.filter { game ->
            (console.isEmpty() || game.console == console) &&
                when (filter) {
                    Filter.ANY -> true
                    Filter.LIBRARY -> game.inLibrary
                    Filter.NOT_LIBRARY -> !game.inLibrary
                    Filter.NO_SCRIPT -> game.script == Script.NONE || game.script == Script.NOT_INSTALLED || game.script == Script.TOO_NEW
                    Filter.NO_NAMES -> game.names == Names.AVAILABLE || game.names == Names.READY
                    Filter.NO_ART -> !game.cover || !game.spine
                    Filter.UPDATES -> game.script == Script.UPDATE
                } &&
                wanted.all { word -> listOf(game.title, game.english, game.reads, game.script.said, *game.ids.toTypedArray()).any { it.contains(word, ignoreCase = true) } }
        }
        chips.removeAllViews()
        chips.addView(chip("All ${games.size}", console.isEmpty()) { console = ""; show() })
        for (key in Places.CORES.keys) {
            val n = games.count { it.console == key }
            if (n > 0 || console == key) chips.addView(chip("${Places.shortName(key)} $n", console == key) { console = key; show() })
        }
        states.removeAllViews()
        for (one in Filter.values()) states.addView(chip(one.said, filter == one) { filter = one; show() })
        val updates = games.count { it.script == Script.UPDATE }
        val updateWord = if (updates == 1) "1 UPDATE" else "$updates UPDATES"
        marks.text = if (marked.isEmpty()) "${shown.size} SHOWN · $updateWord" else "${marked.size} MARKED"
        marks.setTextColor(if (marked.isEmpty()) muted else accent)
        rows.notifyDataSetChanged()
    }

    private val rows = object : BaseAdapter() {
        override fun getCount() = shown.size
        override fun getItem(position: Int) = shown[position]
        override fun getItemId(position: Int) = position.toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val game = shown[position]
            val row = (convertView as? LinearLayout) ?: LinearLayout(this@GameScriptsActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(12f), dp(6f), dp(12f), dp(6f))
                addView(CheckBox(context).apply { isClickable = false; isFocusable = false })
                addView(LinearLayout(context).apply {
                    orientation = LinearLayout.VERTICAL
                    addView(TextView(context).apply { setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f); maxLines = 2; ellipsize = android.text.TextUtils.TruncateAt.END })
                    addView(TextView(context).apply { setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f); maxLines = 2; ellipsize = android.text.TextUtils.TruncateAt.END })
                    addView(TextView(context).apply {
                        setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                        typeface = Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL)
                        letterSpacing = 0.04f
                    })
                }, LinearLayout.LayoutParams(0, WRAP, 1f))
            }
            val check = row.getChildAt(0) as CheckBox
            val text = row.getChildAt(1) as LinearLayout
            val title = text.getChildAt(0) as TextView
            val reads = text.getChildAt(1) as TextView
            val line = text.getChildAt(2) as TextView
            check.isChecked = game.id in marked
            // The console's name at the head of its games: a list grouped by console.
            val first = position == 0 || shown[position - 1].console != game.console
            title.text = game.title
            title.setTextColor(if (game.script == Script.NONE || game.script == Script.NOT_INSTALLED) muted else ink)
            reads.text = game.reads
            reads.setTextColor(muted)
            reads.visibility = if (game.reads.isNotEmpty()) View.VISIBLE else View.GONE
            val art = listOf(if (game.cover) "COVER" else "NO COVER", if (game.spine) "SPINE" else "NO SPINE").joinToString(" ")
            line.text = listOf(
                if (first) Places.shortName(game.console).uppercase() else "",
                if (game.ids.size > 1) "${game.id} +${game.ids.size - 1}" else game.id,
                if (game.inLibrary) "IN LIBRARY" else "",
                "SCRIPT " + game.script.said.uppercase(), "NAMES " + game.names.said.uppercase(), art,
            ).filter { it.isNotEmpty() }.joinToString(" · ")
            line.setTextColor(if (game.script == Script.UPDATE || game.names == Names.AVAILABLE || (game.inLibrary && (!game.cover || !game.spine))) accent else muted)
            row.setBackgroundColor(if (game.id in marked) (accent and 0x00FFFFFF) or 0x28000000 else 0)
            return row
        }
    }

    private fun toggle(id: String) {
        if (!marked.remove(id)) marked.add(id)
        show()
    }

    private fun markAll() {
        if (shown.all { it.id in marked }) shown.forEach { marked.remove(it.id) } else shown.forEach { marked.add(it.id) }
        show()
    }

    private fun markedOrAsk(): List<String>? {
        if (marked.isNotEmpty()) return games.filter { it.id in marked }.map { it.id }
        status.text = "Tap games to mark them, or long-press a game for its actions"
        return null
    }

    private fun actions(ids: List<String>, title: String) {
        val said = listOf("Install or update script", "Install names", "Find box art", "Remove script…",
            if (ids.all { it in marked }) "Unmark" else "Mark")
        AlertDialog.Builder(this).setTitle(title).setItems(said.toTypedArray()) { _, at ->
            when (at) {
                0 -> install(ids)
                1 -> names(ids)
                2 -> art(ids)
                3 -> remove(ids)
                4 -> ids.forEach { toggle(it) }
            }
        }.show()
    }

    /** The rows of [ids]; [discs] expands each to its discs' own rows (scripts and names are each disc's). */
    private fun gamesOf(ids: List<String>) = games.filter { it.id in ids }

    private fun discsOf(ids: List<String>): List<Game> = gamesOf(ids).flatMap { row ->
        if (row.ids.size == 1) listOf(row) else row.ids.map { id -> discGames[id] ?: row.copy(id = id) }
    }

    /** Each disc's own row, before discs were made one row. */
    private var discGames: Map<String, Game> = emptyMap()

    private fun install(ids: List<String>) {
        val wanted = discsOf(ids).filter { it.entry != null && it.script != Script.TOO_NEW }
        if (wanted.isEmpty()) {
            status.text = "No downloadable scripts for the selected games"
            return
        }
        runWork("Installing", wanted) { game ->
            val entry = game.entry!!
            val base = "$ROOT/${LISTS.getValue(game.console)}/index.json"
            val files = (listOf(entry.script) + entry.data).associate { it.file to ScriptStore.fetcher(ScriptStore.urlOf(it.file, base)) }.toMutableMap()
            entry.names?.let { names ->
                runCatching { ScriptStore.fetcher(ScriptStore.urlOf(names.file, base)) }.getOrNull()?.let { files[names.file] = it }
            }
            folder.install(entry, files)
        }
    }

    private fun installAll() {
        install(games.filter { it.script == Script.UPDATE }.map { it.id }.ifEmpty {
            status.text = "No updates"
            return
        })
    }

    private fun names(ids: List<String>) {
        val wanted = discsOf(ids).filter { it.names == Names.AVAILABLE || it.names == Names.READY }
        if (wanted.isEmpty()) {
            status.text = "No names dictionaries available to install"
            return
        }
        val fetchedNames = HashSet<String>()
        var importedAny = false
        runWork(
            "Installing names",
            wanted,
            after = { if (importedAny) runCatching { DictionaryStore.get(applicationContext).database.compact() } }
        ) { game ->
            game.entry?.takeIf { it.names != null && folder.needsNames(it) }?.let { entry ->
                val namesFile = entry.names!!.file
                if (fetchedNames.add(namesFile)) {
                    val base = "$ROOT/${LISTS.getValue(game.console)}/index.json"
                    folder.installNames(entry, ScriptStore.fetcher(ScriptStore.urlOf(namesFile, base)))
                }
            }
            val packs = NamesDictionaries.find(NamesDictionaries.folders(applicationContext, folder.dir))
            NamesDictionaries.forGame(game.id.uppercase(), packs)?.let {
                if (NamesDictionaries.ensure(DictionaryStore.get(applicationContext).database, it)) importedAny = true
            }
        }
    }

    private fun art(ids: List<String>) {
        val wanted = gamesOf(ids).filter { !it.cover || !it.spine }
        if (wanted.isEmpty()) {
            status.text = "Selected games already have box art"
            return
        }
        var found = 0
        runWork("Finding box art", wanted, done = { "Box art found for $found of ${wanted.size}" }) { game ->
            val wantCover = !game.cover
            val wantSpine = !game.spine
            val cover = Covers.fetch(this, game.console, game.id, File(game.path.ifEmpty { "/" + game.id }), game.title,
                wantCover = wantCover, wantSpine = wantSpine)
            val spine = if (wantSpine) Covers.spineOf(this, game.console, game.id) else null
            if ((wantCover && cover != null) || (wantSpine && spine != null)) found++
            // The library's list says what was found, so that the shelf shows it without a scan.
            if (game.inLibrary) relist(game.console, game.id, cover, spine)
        }
    }

    /** A game's cover and spine written into its console's list (ScanService's file). */
    private fun relist(key: String, id: String, cover: File?, spine: File?) {
        val file = Places.gamesFile(this, key)
        val listed = runCatching { JSONArray(file.readText()) }.getOrNull() ?: return
        for (i in 0 until listed.length()) {
            val one = listed.optJSONObject(i) ?: continue
            if (one.optString("id") != id) continue
            cover?.let { one.put("cover", it.path) }
            spine?.let { one.put("spine", it.path) }
        }
        val partial = File(file.path + ".new")
        partial.writeText(listed.toString())
        if (!partial.renameTo(file)) partial.delete()
        sendBroadcast(Intent(Places.SCANNED).setPackage(packageName).putExtra(ScanService.EXTRA_LISTED, true))
    }

    private fun remove(ids: List<String>) {
        val own = discsOf(ids).filter { it.script == Script.INSTALLED || it.script == Script.OWN || it.script == Script.UPDATE }
        if (own.isEmpty()) {
            status.text = "Only installed or custom scripts can be removed"
            return
        }
        AlertDialog.Builder(this)
            .setMessage(if (own.size == 1) "Remove script for ${own[0].title}?" else "Remove ${own.size} scripts?")
            .setPositiveButton("Remove") { _, _ -> runWork("Removing", own) { folder.remove(it.id) } }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun installChosen(uris: List<Uri>) {
        submit {
            val files = uris.mapNotNull { uri ->
                runCatching {
                    val name = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                        if (it.moveToFirst()) it.getString(0) else null
                    } ?: uri.lastPathSegment.orEmpty().substringAfterLast('/')
                    name to (contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: ByteArray(0))
                }.getOrNull()
            }
            val (ids, problems) = folder.installFiles(files)
            runOnUiThread {
                if (isDestroyed || isFinishing) return@runOnUiThread
                val summary = if (ids.size == 1) "Installed 1 custom script" else "Installed ${ids.size} custom scripts"
                status.text = (listOf(summary) + problems).joinToString("\n")
                changed(ids)
            }
        }
    }

    /** One thing done to each game in turn, in the background, its count and its problems said. */
    private fun runWork(doing: String, wanted: List<Game>, done: (() -> String)? = null, after: (() -> Unit)? = null, each: (Game) -> Unit) {
        if (busy) return
        busy = true
        submit {
            val problems = ArrayList<String>()
            wanted.forEachIndexed { i, game ->
                runOnUiThread { if (!isDestroyed && !isFinishing) status.text = "$doing · ${i + 1} of ${wanted.size} · ${game.title}" }
                runCatching { each(game) }.onFailure { problems.add("${game.title}: ${it.message ?: it.javaClass.simpleName}") }
            }
            after?.let { runCatching(it) }
            runOnUiThread {
                if (isDestroyed || isFinishing) return@runOnUiThread
                busy = false
                status.text = (listOf(done?.invoke() ?: "$doing: ${wanted.size - problems.size} of ${wanted.size} done") + problems).joinToString("\n")
                changed(wanted.map { it.id })
            }
        }
    }

    /** After a change: the game list's badges, the running game's script at once, the rows again. */
    private fun changed(ids: List<String>) {
        ReaderCatalog.forget()
        if (ReaderSession.isActive && ReaderSession.gameKey in ids) runCatching { ReaderNative.reloadProfile() }
        build()
    }

    /** Where box art is looked for, and ScreenScraper's account (ArtSources). */
    private fun sourcesDialog() {
        val now = ArtSources.load(this)
        val column = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(20f), dp(8f), dp(20f), 0) }
        val vndb = CheckBox(this).apply { text = "Box art from VNDB"; isChecked = now.vndb }
        val ss = CheckBox(this).apply { text = "Box art from ScreenScraper"; isChecked = now.screenScraper }
        column.addView(vndb)
        column.addView(ss)
        column.addView(note("ScreenScraper requires your account along with a developer ID and password."))
        fun field(hint: String, value: String, secret: Boolean) = EditText(this).apply {
            this.hint = hint
            setText(value)
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or (if (secret) InputType.TYPE_TEXT_VARIATION_PASSWORD else 0)
            column.addView(this)
        }
        val user = field("Username", now.ssUser, false)
        val password = field("Password", now.ssPassword, true)
        val devId = field("Developer ID", now.ssDevId, false)
        val devPassword = field("Developer password", now.ssDevPassword, true)
        AlertDialog.Builder(this).setTitle("Box art sources").setView(column)
            .setPositiveButton("Save") { _, _ ->
                ArtSources(vndb.isChecked, ss.isChecked, user.text.toString().trim(), password.text.toString(),
                    devId.text.toString().trim(), devPassword.text.toString()).save(this)
                status.text = "Box art sources saved"
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    companion object {
        const val EXTRA_CONSOLE = "console"
        private const val ROOT = "https://raw.githubusercontent.com/sommepo/furoshiki-scripts/main/scripts"
        private val TYPES = listOf("png", "jpg", "jpeg")
        private val DISC_END = Regex("\\s*[(（\\[]?\\s*(?:disc|disk|ディスク)\\s*\\d+\\s*[)）\\]]?\\s*$", RegexOption.IGNORE_CASE)
        /** The scripts repository's folder of each console the app plays. */
        private val LISTS = linkedMapOf(
            "psp" to "psp", "dc" to "dreamcast", "3ds" to "3ds", "nds" to "nds", "ps1" to "ps1", "ps2" to "ps2", "psvita" to "vita",
        )

        /** The console a game's ID is of, where neither a list nor the library says. */
        fun consoleOfId(id: String): String {
            val i = id.uppercase()
            return when {
                Regex("^(ULJ|NPJ|UCJ|ULA)").containsMatchIn(i) -> "psp"
                i.startsWith("PCS") -> "psvita"
                Regex("^0004[0-9A-F]{12}$").matches(i) -> "3ds"
                Regex("^(SLPS-?2|SLPM-?[5-7]|SCPS-?[15-7]|SLPS-?7)").containsMatchIn(i) -> "ps2"
                Regex("^(SLPS-?[0189]|SLPM-?8|SCPS-?[14]0)").containsMatchIn(i) -> "ps1"
                Regex("^(T-?\\d|HDR-|MK-)").containsMatchIn(i) || Regex("^\\d{7}$").matches(i) -> "dc"
                Regex("^[A-Z0-9]{4}$").matches(i) -> "nds"
                else -> ""
            }
        }
    }
}
