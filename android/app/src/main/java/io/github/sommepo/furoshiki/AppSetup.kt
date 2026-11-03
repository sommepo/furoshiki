// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.IBinder
import android.util.Log
import io.github.sommepo.furoshiki.reader.ReaderSettings
import io.github.sommepo.furoshiki.reader.anki.AnkiDroidClient
import io.github.sommepo.furoshiki.reader.anki.AnkiSearch
import io.github.sommepo.furoshiki.reader.anki.CardBuilder
import io.github.sommepo.furoshiki.reader.dict.DictionaryImporter
import io.github.sommepo.furoshiki.reader.dict.DictionaryStore
import io.github.sommepo.furoshiki.reader.dict.SharedDictionaries
import java.io.File
import org.json.JSONObject

/**
 * An install set up by itself, in the process that holds the reader's settings. The maintainer's
 * own builds carry android/private/setup.json (not in Git, and so in no public build): the
 * settings a device starts with, save sync's address and code among them, taken once. And when
 * AnkiDroid lets this app in, cards are set up from its own decks and note types.
 */
object AppSetup {
    private const val KEY_SEEDED = "furoshiki_setup_seeded"

    /** Field names of note types in use that the reader's first guess does not know. */
    private val MORE_FIELDS = mapOf("worddictionaryform" to "{expression}", "pitchaccent" to "{pitch-positions}")

    /** The private settings, before anything of the reader reads them. */
    fun seed(context: Context) {
        val settings = ReaderSettings(context)
        if (settings.preferences.getBoolean(KEY_SEEDED, false)) return
        val edit = settings.preferences.edit().putBoolean(KEY_SEEDED, true)
        try {
            val json = context.assets.open("setup.json").bufferedReader().use { it.readText() }
            val values = JSONObject(json)
            for (key in values.keys()) {
                when (val value = values.get(key)) {
                    is Boolean -> edit.putBoolean(key, value)
                    is Int -> edit.putInt(key, value)
                    else -> edit.putString(key, value.toString())
                }
            }
        } catch (_: java.io.IOException) {
            // A build without it: a device is set up by hand.
        }
        edit.apply()
    }

    /** Off the main thread: what is new in the dictionaries folder (Games/Dictionaries) is imported. */
    fun dictionaries(context: Context) {
        val fresh = SharedDictionaries.newFiles(context)
        if (fresh.isEmpty()) return
        val importer = DictionaryImporter(DictionaryStore.get(context).database)
        val folder = File(context.cacheDir, "reader/import").apply { mkdirs() }
        for (file in fresh) {
            val copy = File(folder, "dictionary.zip")
            try {
                context.contentResolver.openInputStream(file.uri)!!.use { input ->
                    copy.outputStream().use { input.copyTo(it) }
                }
                val result = importer.import(copy, progress = { _, _, _ -> }, keepGoing = { true })
                Log.i("Furoshiki", "Dictionary imported: ${result.title}")
                SharedDictionaries.markSeen(context, file)
            } catch (e: DictionaryImporter.ImportException) {
                // One that is there already, from another file of it, counts as done.
                if (e.alreadyThere) SharedDictionaries.markSeen(context, file)
                Log.w("Furoshiki", "${file.name}: ${e.message}")
            } catch (e: Exception) {
                Log.w("Furoshiki", "${file.name}: ${e.message}")
            } finally {
                copy.delete()
            }
        }
    }

    /** Off the main thread: AnkiDroid is asked. The deck and note type it finds, when none is chosen. */
    fun anki(context: Context) {
        val settings = ReaderSettings(context)
        if (settings.ankiModel.isNotEmpty() || settings.ankiTransport != ReaderSettings.ANKI_ANKIDROID) return
        if (context.checkSelfPermission(AnkiDroidClient.PERMISSION) != PackageManager.PERMISSION_GRANTED) return
        try {
            val client = AnkiDroidClient(context)
            val decks = client.deckNames()
            val models = client.modelNames()
            Log.i("Furoshiki", "AnkiDroid has decks $decks and note types $models")
            // The deck cards are mined into, and in it the note type most of its cards are.
            val deck = decks.firstOrNull { "mining" in it.lowercase() }
                ?: decks.firstOrNull { it != "Default" && it != "デフォルト" } ?: decks.firstOrNull() ?: return
            val quoted = AnkiSearch.escape(deck)
            val counts = models.associateWith { model ->
                client.findNotesWhere("\"deck:$quoted\" \"note:${AnkiSearch.escape(model)}\"").size
            }
            Log.i("Furoshiki", "In $deck: $counts")
            val model = counts.filterValues { it > 0 }.maxByOrNull { it.value }?.key ?: return
            val fields = client.fieldNames(model)
            val mapping = CardBuilder.suggestMapping(fields).mapValues { (field, guess) ->
                guess.ifEmpty { MORE_FIELDS[field.lowercase()].orEmpty() }
            }
            if (mapping.values.none { "{expression}" in it }) return
            val first = fields.firstOrNull() ?: return
            settings.preferences.edit()
                .putString(ReaderSettings.KEY_ANKI_DECK, deck)
                .putString(ReaderSettings.KEY_ANKI_MODEL, model)
                .putString(ReaderSettings.KEY_ANKI_FIELDS, CardBuilder.mappingToJson(mapping))
                .putString(ReaderSettings.KEY_ANKI_KEY_FIELD, ReaderSettings.keyFieldValue(model, first))
                .apply()
            Log.i("Furoshiki", "Cards go to deck $deck as $model: $mapping")
        } catch (e: Exception) {
            Log.w("Furoshiki", "AnkiDroid could not be asked: ${e.message}")
        }
    }
}

/** [AppSetup]'s slow part from the page, in the process that keeps the reader's data. */
class AppSetupService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Thread({
            AppSetup.anki(this)
            AppSetup.dictionaries(this)
            stopSelf()
        }, "AppSetup").start()
        return START_NOT_STICKY
    }
}
