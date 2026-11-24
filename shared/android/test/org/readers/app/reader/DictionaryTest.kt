// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.readers.app.reader.dict.DictionaryDatabase
import org.readers.app.reader.dict.DictionaryImporter
import org.readers.app.reader.dict.DictionaryLookup
import org.readers.app.reader.dict.Frequency
import org.readers.app.reader.dict.GlossaryRenderer
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** Dictionary import and lookup against small archives written for the test (no real data). */
@RunWith(RobolectricTestRunner::class)
class DictionaryTest {
    @get:Rule
    val folder = TemporaryFolder()

    private lateinit var db: DictionaryDatabase
    private lateinit var importer: DictionaryImporter
    private lateinit var lookup: DictionaryLookup

    private val structured = """{"type":"structured-content","content":[
        {"tag":"div","style":{"fontWeight":"bold"},"data":{"content":"gloss"},"content":"food"},
        {"tag":"ul","content":[{"tag":"li","content":"item one"},
          {"tag":"li","content":["item ",{"tag":"ruby","content":["二",{"tag":"rt","content":"に"}]}]}]},
        {"tag":"img","path":"x.png"}]}"""

    private val terms = """[
        ["食べる","たべる","v1","v1",100,["to eat","to live on"],1,""],
        ["読む","よむ","v5","v5",90,["to read"],2,""],
        ["高い","たかい","adj-i","adj-i",80,["high","expensive"],3,""],
        ["これ","","pn","",70,["this"],4,""],
        ["食べ物","たべもの","n","",60,[$structured],5,""],
        ["名","な","n","",50,["name"],6,""],
        ["名乗る","なのる","v5","v5",50,["to give one's name"],7,""],
        ["マリア","","n","",1,["Maria <the name>"],8,""],
        ["飼う","かう","v5","v5",40,["to keep (a pet)"],9,""],
        ["買う","かう","v5","v5",95,["to buy"],10,""]
    ]"""

    private fun archive(name: String, files: Map<String, String>): File {
        val file = File(folder.root, name) // written afresh each time it is asked for
        ZipOutputStream(file.outputStream()).use { zip ->
            for ((path, content) in files) {
                zip.putNextEntry(ZipEntry(path))
                zip.write(content.toByteArray(Charsets.UTF_8))
                zip.closeEntry()
            }
        }
        return file
    }

    private fun mainArchive() = archive(
        "main.zip",
        mapOf(
            // Deliberately not first in the archive: the importer must not rely on entry order.
            "term_bank_1.json" to terms,
            "index.json" to """{"title":"Test Dict","revision":"r1","format":3,"author":"tests"}""",
            "tag_bank_1.json" to """[["n","partOfSpeech",0,"noun",0]]"""
        )
    )

    private fun frequencyArchive() = archive(
        "freq.zip",
        mapOf(
            "index.json" to """{"title":"Freq","revision":"1","format":3,"frequencyMode":"rank-based"}""",
            "term_meta_bank_1.json" to """[
                ["食べる","freq",1500],
                ["読む","freq",{"reading":"よむ","frequency":{"value":800,"displayValue":"800㋕"}}],
                ["読む","freq",{"reading":"どく","frequency":5}],
                ["読む","pitch",{"reading":"よむ","pitches":[{"position":1},{"position":0}]}],
                ["買う","freq","rank 200"]
            ]"""
        )
    )

    /** A monolingual-style dictionary: no word classes at all. */
    private fun monoArchive() = archive(
        "mono.zip",
        mapOf(
            "index.json" to """{"title":"Mono","revision":"1","format":3}""",
            "term_bank_1.json" to """[["食べる","たべる","","",0,["食物を口に入れる。"],0,""],
                ["た","","","",0,["助動詞。"],0,""]]"""
        )
    )

    @Before
    fun setUp() {
        db = DictionaryDatabase(ApplicationProvider.getApplicationContext<Context>(), null)
        importer = DictionaryImporter(db)
        lookup = DictionaryLookup(db)
    }

    @After
    fun tearDown() {
        db.close()
    }

    @Test
    fun importsTermsAndReportsProgress() {
        val steps = ArrayList<Pair<Int, Int>>()
        val result = importer.import(mainArchive(), { done, total, _ -> steps.add(done to total) })
        assertEquals("Test Dict", result.title)
        assertEquals(10, result.terms)
        assertEquals(0, result.metas)
        assertEquals(listOf(0 to 1, 1 to 1), steps)
        val info = db.dictionaries().single()
        assertEquals("Test Dict", info.title)
        assertEquals("r1", info.revision)
        assertEquals(10, info.termCount)
        assertTrue(info.enabled)
    }

    /** The same rows, styles and pictures whichever way the rows go into SQLite. */
    @Test
    fun aBankReadInPiecesKeepsEveryRowWhole() {
        // Strings with brackets, commas and escaped quotes in them, rows of every size.
        val rows = (1..60).map { """["語$it","]x[ \"q\" , {","n",${if (it % 7 == 0) "[1,[2,{\"a\":\"]\"}]]" else "\"\""},$it,["${"長".repeat(it)}"]]""" }
        val bank = "\uFEFF [\n" + rows.joinToString(",\n ") + "\n]\n"
        for (piece in listOf(1, 16, 200, 1 shl 20)) {
            val seen = ArrayList<String>()
            DictionaryImporter(DictionaryDatabase(ApplicationProvider.getApplicationContext<Context>(), null))
                .inPieces(bank.toByteArray().inputStream(), piece) { part ->
                    val array = org.json.JSONArray(String(part))
                    for (i in 0 until array.length()) seen.add(array.getJSONArray(i).toString())
                }
            assertEquals("piece $piece", rows.map { org.json.JSONArray(it).toString() }, seen)
        }
    }

    @Test
    fun bigBanksAreWrittenAChunkAtATimeWithTheSameRows() {
        // A bank bigger than the budget is read by the writer and written as it is read (it
        // ran a 256 MB heap out when held whole); every row comes out as the other way.
        val rows = (1..450).joinToString(",") { """["語$it","ご$it","n","",$it,["word $it"],$it,""]""" }
        val metas = (1..450).joinToString(",") { """["語$it","freq",$it]""" }
        val zip = archive(
            "big.zip",
            mapOf(
                "index.json" to """{"title":"Big","revision":"1","format":3}""",
                "term_bank_1.json" to "[$rows]", "term_bank_2.json" to """[["小","しょう","n","",1,["small"],1,""]]""",
                "term_meta_bank_1.json" to "[$metas]"
            )
        )
        val seen = ArrayList<List<String>>()
        for (held in listOf(null, 64L)) {
            val fresh = DictionaryDatabase(ApplicationProvider.getApplicationContext<Context>(), null)
            val result = DictionaryImporter(fresh).apply { jsonFunctions = true; heldBytes = held }.import(zip)
            assertEquals(451, result.terms)
            assertEquals(450, result.metas)
            seen.add(listOf("語1", "語225", "語450", "小").map { word ->
                val entry = fresh.findTerms(listOf(word)).single()
                "${entry.expression}|${entry.reading}|${entry.score}|${entry.glossaryJson}|${fresh.findMeta(listOf(word)).joinToString { it.data }}"
            })
        }
        assertEquals(seen[0], seen[1])
    }

    @Test
    fun importsStylesPicturesAndPitchPatternsEitherWay() {
        for (json in listOf(true, false)) {
            val fresh = DictionaryDatabase(ApplicationProvider.getApplicationContext<Context>(), null)
            val withJson = DictionaryImporter(fresh).apply { jsonFunctions = json }
            val styled = archive(
                "styled.zip",
                mapOf(
                    "dict/index.json" to """{"title":"Styled","revision":"1","format":3}""",
                    "dict/styles.css" to """li[data-sc-content="sense"] { color: red; }""",
                    "dict/img/a.png" to "PNGDATA",
                    "dict/term_bank_1.json" to """[["絵","え","n","",1,[{"type":"structured-content","content":[{"tag":"img","path":"img/a.png"},"picture"]}],1,""],
                        ["線","せん",null,"",2.0,["line \"quoted\" \u7dda"],2,""],
                        ["円","円","n","",3,["yen"],3,"P"]]""",
                    "dict/term_meta_bank_1.json" to """[["絵","pitch",{"reading":"え","pitches":[{"position":"HL"}]}],
                        ["線","pitch",{"reading":"せん","pitches":[{"position":"LHH"}]}],
                        ["線","freq",120],["円","freq","rank 3"]]"""
                )
            )
            val result = withJson.import(styled)
            assertEquals(3, result.terms)
            assertEquals(4, result.metas)
            assertEquals(1, result.media)
            assertTrue(fresh.styles().getValue("Styled").contains("color: red"))
            val pictured = fresh.findTerms(listOf("絵")).single()
            assertEquals("data:image/png;base64," + java.util.Base64.getEncoder().encodeToString("PNGDATA".toByteArray()),
                pictured.media["img/a.png"])
            val line = fresh.findTerms(listOf("線")).single()
            assertEquals("", line.definitionTags)
            assertEquals(2, line.score)
            assertEquals("line \"quoted\" 線", org.json.JSONArray(line.glossaryJson).getString(0))
            val yen = fresh.findTerms(listOf("円")).single()
            assertEquals("", yen.reading) // a reading that is the expression is stored empty
            assertEquals("P", yen.termTags)
            val meta = fresh.findMeta(listOf("線", "円")).associate { it.expression + it.mode to it.data }
            assertEquals("120", meta["線freq"])
            assertEquals("\"rank 3\"", meta["円freq"])
            val looked = DictionaryLookup(fresh)
            assertEquals(listOf(1), looked.lookup("絵").first().pitches.first().positions)
            assertEquals(listOf(0), looked.lookup("線").first().pitches.first().positions)
            val id = fresh.dictionaries().single().id
            fresh.delete(id)
            assertEquals(0, fresh.readableDatabase.rawQuery("SELECT COUNT(*) FROM media", emptyArray()).use { it.moveToFirst(); it.getInt(0) })
            fresh.close()
        }
    }

    @Test
    fun refusesDuplicatesAndBrokenArchives() {
        importer.import(mainArchive())
        val again = assertThrows(DictionaryImporter.ImportException::class.java) {
            importer.import(mainArchive())
        }
        assertTrue(again.message!!.contains("already imported"))
        assertThrows(DictionaryImporter.ImportException::class.java) {
            importer.import(archive("empty.zip", mapOf("readme.txt" to "hello")))
        }
        assertThrows(DictionaryImporter.ImportException::class.java) {
            importer.import(archive("noterms.zip", mapOf("index.json" to """{"title":"X","format":3}""")))
        }
        // A bank that breaks half way leaves nothing behind.
        assertThrows(DictionaryImporter.ImportException::class.java) {
            importer.import(
                archive(
                    "broken.zip",
                    mapOf(
                        "index.json" to """{"title":"Broken","format":3}""",
                        "term_bank_1.json" to """[["良い","よい","","adj-i",1,["good"],1,""],["oops"""
                    )
                )
            )
        }
        assertEquals(listOf("Test Dict"), db.dictionaries().map { it.title })
        assertTrue(lookup.lookup("良い").isEmpty())
    }

    @Test
    fun cancellingAnImportLeavesTheDatabaseUnchanged() {
        assertThrows(DictionaryImporter.ImportException::class.java) {
            importer.import(mainArchive(), keepGoing = { false })
        }
        assertTrue(db.dictionaries().isEmpty())
    }

    @Test
    fun findsInflectedWordsAndSaysWhy() {
        importer.import(mainArchive())
        val past = lookup.lookup("食べた。それから")
        assertEquals("食べる", past[0].expression)
        assertEquals("たべる", past[0].reading)
        assertEquals("食べた", past[0].matchedText)
        assertEquals(listOf("past"), past[0].reasons)

        val progressive = lookup.lookup("読んでいる")
        assertEquals("読む", progressive[0].expression)
        assertEquals("読んでいる", progressive[0].matchedText)
        assertEquals(listOf("progressive or perfect", "-te"), progressive[0].reasons)

        val imperative = lookup.lookup("名乗れ")
        assertEquals(listOf("名乗る", "名"), imperative.map { it.expression })
        assertEquals("名乗れ", imperative[0].matchedText)
        assertEquals("名", imperative[1].matchedText)
    }

    @Test
    fun theFirstHeadwordsFirstThenTheRestComeOutTheSame() {
        importer.import(mainArchive())
        val whole = lookup.lookup("食べ物を買う")
        val first = lookup.lookup("食べ物を買う", readFirst = 1)
        assertEquals(whole.map { it.expression }, first.map { it.expression })
        assertTrue(first.first().entries.all { it.glossaryJson.isNotEmpty() })
        val before = first.drop(1).flatMap { it.entries }
        assertTrue(before.isEmpty() || before.any { it.glossaryJson.isEmpty() })
        val same = first.toList()
        lookup.fillIn(first, lookup.readRest(first))
        // The same objects, now whole.
        assertTrue(same.indices.all { same[it] === first[it] })
        assertEquals(whole.map { r -> r.entries.map { it.glossaryJson } }, first.map { r -> r.entries.map { it.glossaryJson } })
        assertTrue(first.none { lookup.unread(it) })
    }

    @Test
    fun prefersTheLongestMatch() {
        importer.import(mainArchive())
        val results = lookup.lookup("食べ物を買う")
        assertEquals("食べ物", results[0].expression)
        // The shorter reading of the same text is still offered after it.
        assertTrue(results.any { it.expression == "食べる" && it.matchedText == "食べ" })
    }

    @Test
    fun matchesKanaByReadingAndKatakanaAsWritten() {
        importer.import(mainArchive())
        val byReading = lookup.lookup("たかくない")
        assertEquals("高い", byReading[0].expression)
        assertEquals("たかくない", byReading[0].matchedText)
        // Two words share the reading かう: both are offered, the higher score first.
        assertEquals(listOf("買う", "飼う"), lookup.lookup("かった").map { it.expression })
        assertEquals("マリア", lookup.lookup("マリアだ")[0].expression)
        assertEquals("", lookup.lookup("マリアだ")[0].displayReading)
        assertEquals("これ", lookup.lookup("これは")[0].expression)
    }

    @Test
    fun stopsAtPunctuationAndFindsNothingInNonWords() {
        importer.import(mainArchive())
        assertTrue(lookup.lookup("").isEmpty())
        assertTrue(lookup.lookup("、食べる").isEmpty())
        assertTrue(lookup.lookup("…").isEmpty())
        // 食べる。読む: the scan must not run across the full stop.
        assertEquals("食べる", lookup.lookup("食べる。読む")[0].matchedText)
    }

    @Test
    fun wordClassRulesOutFalseDeinflections() {
        importer.import(mainArchive())
        // 名た would be a "past" of an ichidan 名る; 名 is a noun, so only the plain 名 matches.
        val results = lookup.lookup("名た")
        assertEquals(listOf("名"), results.map { it.expression })
        assertEquals("名", results[0].matchedText)
        assertTrue(results[0].reasons.isEmpty())
    }

    @Test
    fun dictionariesWithoutWordClassesJoinConfirmedHeadwords() {
        importer.import(mainArchive())
        importer.import(monoArchive())
        val results = lookup.lookup("食べた")
        assertEquals("食べる", results[0].expression)
        assertEquals(listOf("Test Dict", "Mono"), results[0].entries.map { it.dictionary })
        // On its own, the class-less dictionary cannot confirm an inflection...
        db.setEnabled(db.dictionaries().first { it.title == "Test Dict" }.id, false)
        assertTrue(lookup.lookup("食べた").none { it.expression == "食べる" })
        // ...but still matches what is written.
        assertEquals("食べる", lookup.lookup("食べる")[0].expression)
    }

    @Test
    fun attachesFrequenciesAndPitchForTheRightReading() {
        importer.import(mainArchive())
        importer.import(frequencyArchive())
        val yomu = lookup.lookup("読む")[0]
        assertEquals(listOf(Frequency("Freq", "800㋕", 800.0, true)), yomu.frequencies)
        assertEquals(listOf(1, 0), yomu.pitches.single().positions)
        val taberu = lookup.lookup("食べる")[0]
        assertEquals(listOf(Frequency("Freq", "1500", 1500.0, true)), taberu.frequencies)
        assertTrue(taberu.pitches.isEmpty())
        assertEquals(listOf(Frequency("Freq", "rank 200", null, true)), lookup.lookup("買う")[0].frequencies)
        assertEquals(1500L, DictionaryLookup.harmonicRank(taberu.frequencies))
        assertNull(DictionaryLookup.harmonicRank(lookup.lookup("買う")[0].frequencies))
        assertEquals(
            1043L,
            DictionaryLookup.harmonicRank(listOf(Frequency("a", "800", 800.0, true), Frequency("b", "1500", 1500.0, true)))
        )
    }

    @Test
    fun orderEnablingAndDeletionAreRespected() {
        importer.import(mainArchive())
        importer.import(monoArchive())
        val (main, mono) = db.dictionaries().let { it[0] to it[1] }
        db.setOrder(listOf(mono.id, main.id))
        assertEquals(listOf("Mono", "Test Dict"), db.dictionaries().map { it.title })
        assertEquals(listOf("Mono", "Test Dict"), lookup.lookup("食べる")[0].entries.map { it.dictionary })
        db.setEnabled(mono.id, false)
        assertEquals(listOf("Test Dict"), lookup.lookup("食べる")[0].entries.map { it.dictionary })
        assertTrue(db.hasEnabledDictionaries())
        db.delete(main.id)
        assertTrue(lookup.lookup("食べる").isEmpty())
        assertFalse(db.hasEnabledDictionaries())
        assertEquals(listOf("Mono"), db.dictionaries().map { it.title })
    }

    /** A names dictionary for two games (tools/titles.py makes them: docs/titles.md). */
    private fun namesArchive() = archive(
        "names.zip",
        mapOf(
            "index.json" to """{"title":"名前 · Test","revision":"t-1","format":3,
                "furoshikiScope":["test-001","TEST-002"],"furoshikiTitle":"test"}""",
            "term_bank_1.json" to """[["マリアさん","","名前","",25,["マリア"],1,""],
                ["マリア","","名前","",27,["マリア","Also: 姫"],1,""],
                ["名","めい","名前","",20,["名（めい）"],2,""]]"""
        )
    )

    @Test
    fun namesDictionaryIsUsedOnlyForItsGamesAndFirst() {
        importer.import(mainArchive())
        importer.import(namesArchive())
        val all = db.dictionaries()
        assertTrue(all[0].scope.isEmpty())
        assertEquals(listOf("TEST-001", "TEST-002"), all[1].scope)

        // No game, or another game: as if the names were not there.
        for (game in listOf("", "OTHER-1", "TEST-00")) {
            val none = lookup.lookup("マリアさん", game = game)
            assertEquals(listOf("マリア"), none.map { it.expression })
            assertEquals(listOf("Test Dict"), none[0].entries.map { it.dictionary })
            assertFalse(none[0].entries[0].scoped)
        }

        // Its game (in any case): the names, before the dictionary above them in the order.
        val named = lookup.lookup("マリアさん", game = "test-001")
        assertEquals(listOf("マリアさん", "マリア"), named.map { it.expression })
        assertTrue(named[0].entries[0].scoped)
        assertEquals(listOf("名前 · Test", "Test Dict"), named[1].entries.map { it.dictionary })

        // A name of the same length as another word comes first; a longer word still wins.
        val same = lookup.lookup("名は", game = "TEST-002")
        assertEquals(listOf("めい", "な"), same.map { it.reading })
        assertEquals("名乗る", lookup.lookup("名乗る", game = "TEST-002").first().expression)

        assertEquals("|A-1|B|", DictionaryDatabase.scopeText(listOf(" a-1 ", "A-1", "b")))
        assertEquals("", DictionaryDatabase.scopeText(emptyList()))
    }

    @Test
    fun rendersGlossariesForCardsAndForThePopup() {
        importer.import(mainArchive())
        val plain = lookup.lookup("食べる")[0].entries[0].glossaryJson
        assertEquals(listOf("to eat", "to live on"), GlossaryRenderer.definitions(plain, GlossaryRenderer.Mode.FULL))
        assertEquals("<ul><li>to eat</li><li>to live on</li></ul>", GlossaryRenderer.toHtml(plain, GlossaryRenderer.Mode.FULL))
        assertEquals("• to eat<br>• to live on", GlossaryRenderer.toHtml(plain, GlossaryRenderer.Mode.SIMPLE))
        assertEquals("to eat; to live on", GlossaryRenderer.toPlainText(plain))

        val rich = lookup.lookup("食べ物")[0].entries[0].glossaryJson
        assertEquals(
            "<div style=\"font-weight: bold;\" data-sc-content=\"gloss\">food</div>" +
                "<ul><li>item one</li><li>item <ruby>二<rt>に</rt></ruby></li></ul>",
            GlossaryRenderer.toHtml(rich, GlossaryRenderer.Mode.FULL)
        )
        assertEquals(
            "<b>food</b><br>• item one<br>• item 二",
            GlossaryRenderer.toHtml(rich, GlossaryRenderer.Mode.SIMPLE)
        )
        // Text from a dictionary is escaped, never passed through as markup.
        val name = lookup.lookup("マリア")[0].entries[0].glossaryJson
        assertEquals("Maria &lt;the name&gt;", GlossaryRenderer.toHtml(name, GlossaryRenderer.Mode.FULL))
        assertEquals("Maria <the name>", GlossaryRenderer.toPlainText(name))
    }

    @Test
    fun glossariesSurviveCompression() {
        val long = "[\"" + "定義".repeat(400) + "\"]"
        val short = "[\"a\"]"
        for (json in listOf(long, short, "[]")) {
            assertEquals(json, DictionaryDatabase.unpack(DictionaryDatabase.pack(json)))
        }
        assertTrue(DictionaryDatabase.pack(long).size < long.toByteArray().size / 4)
    }
}
