// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.net.Uri
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import org.json.JSONObject

/**
 * A game's box art. As on the desktop the user's own comes first: a picture in the console's
 * covers folder by the game's ID, or beside the game's file by its name or in a "covers" folder
 * there. Without one it is fetched once: from the public thumbnails by the file's name, whose
 * Redump names they share (covers.json for the IDs that are named otherwise), by the game's
 * serial for a PlayStation or a PS2, and a PSP game's own art from its disc. The pictures stay on
 * the device.
 */
object Covers {
    private const val THUMBNAILS = "https://thumbnails.libretro.com/"
    private const val TIMEOUT_MS = 10_000
    private val SYSTEMS = mapOf(
        "psp" to "Sony - PlayStation Portable", "dc" to "Sega - Dreamcast", "3ds" to "Nintendo - Nintendo 3DS",
        "ps1" to "Sony - PlayStation", "ps2" to "Sony - PlayStation 2", "psvita" to "Sony - PlayStation Vita",
    )
    private val BY_SERIAL = mapOf(
        "ps1" to "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/default/",
        "ps2" to "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/",
    )
    private val TYPES = listOf("png", "jpg", "jpeg")

    /** Characters the thumbnails' names have in the place of those a file name cannot. */
    private val NOT_IN_NAMES = Regex("[&*/:`<>?\\\\|\"]")

    /** The picture this game has now, or null. */
    fun of(context: Context, key: String, id: String, game: File): File? {
        val own = Places.console(context, key, "covers")
        val folder = game.parentFile
        val base = game.nameWithoutExtension
        val places = TYPES.flatMap { type ->
            listOf(File(own, "$id.$type"), File(folder, "$base.$type"), File(folder, "covers/$base.$type"), File(folder, "covers/$id.$type"))
        }
        return places.firstOrNull { it.isFile && isPicture(it) }
    }

    /** Fetches the game's art into the console's covers folder; the picture, or null. */
    fun fetch(context: Context, key: String, id: String, game: File): File? {
        val own = File(Places.console(context, key, "covers"))
        val names = try {
            JSONObject(context.assets.open("front/covers.json").bufferedReader().use { it.readText() })
        } catch (_: Exception) {
            JSONObject()
        }
        val addresses = ArrayList<String>()
        val system = SYSTEMS[key] ?: return null
        val thumbnails = THUMBNAILS + Uri.encode(system) + "/Named_Boxarts/"
        names.optString(id).takeIf { it.isNotEmpty() }?.let { addresses.add(thumbnails + Uri.encode(it) + ".png") }
        addresses.add(thumbnails + Uri.encode(game.nameWithoutExtension.replace(NOT_IN_NAMES, "_")) + ".png")
        BY_SERIAL[key]?.let { addresses.add("$it$id.jpg") }
        for (address in addresses) {
            val type = address.substringAfterLast('.')
            download(address, File(own, "$id.$type"))?.let { return it }
        }
        if (key == "psp" && game.extension.equals("iso", ignoreCase = true)) return fromPspDisc(game, File(own, "$id.png"))
        if (key == "psvita") return fromVitaGame(context, id, File(own, "$id.png"))
        return null
    }

    private fun download(address: String, to: File): File? {
        val partial = File(to.path + ".new")
        return try {
            val connection = URL(address).openConnection() as HttpURLConnection
            connection.connectTimeout = TIMEOUT_MS
            connection.readTimeout = TIMEOUT_MS
            connection.setRequestProperty("User-Agent", "Furoshiki")
            try {
                if (connection.responseCode != HttpURLConnection.HTTP_OK) return null
                connection.inputStream.use { input -> partial.outputStream().use { input.copyTo(it) } }
            } finally {
                connection.disconnect()
            }
            if (partial.renameTo(to)) to else null
        } catch (e: Exception) {
            Log.i("Furoshiki", "No cover from $address: ${e.message}")
            partial.delete()
            null
        }
    }

    /**
     * The art a Vita game carries for its bubble, from its installed copy: a dumped game's own
     * files are encrypted until the core has installed it at its first start, so a game has its
     * art from the look after that.
     */
    private fun fromVitaGame(context: Context, id: String, to: File): File? {
        val game = File(Places.console(context, "psvita", "data"), "fs/ux0/app/$id/sce_sys")
        val art = listOf("pic0.png", "livearea/contents/bg.png", "icon0.png").map { File(game, it) }
            .firstOrNull { isPicture(it) } ?: return null
        return art.copyTo(to, overwrite = true)
    }

    /** Whether a file begins as a PNG or a JPEG does. */
    private fun isPicture(file: File): Boolean = try {
        file.inputStream().use { input ->
            val head = ByteArray(3)
            input.read(head) == 3 && (head.contentEquals(byteArrayOf(0x89.toByte(), 0x50, 0x4E)) ||
                head.contentEquals(byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte())))
        }
    } catch (_: Exception) {
        false
    }

    /** The art a PSP disc carries for its menu: its large picture, else its icon. */
    private fun fromPspDisc(disc: File, to: File): File? = try {
        RandomAccessFile(disc, "r").use { file ->
            val sector = ByteArray(SECTOR)
            file.seek(16L * SECTOR)
            file.readFully(sector)
            val root = entry(sector, 156)
            val game = find(file, root, "PSP_GAME") ?: return null
            val art = find(file, game, "PIC1.PNG") ?: find(file, game, "ICON0.PNG") ?: return null
            val bytes = ByteArray(art.size)
            file.seek(art.sector * SECTOR.toLong())
            file.readFully(bytes)
            to.writeBytes(bytes)
            to
        }
    } catch (e: Exception) {
        Log.i("Furoshiki", "No art on ${disc.name}: ${e.message}")
        null
    }

    private class Entry(val sector: Long, val size: Int, val name: String)

    private fun int32(bytes: ByteArray, at: Int): Long =
        (0..3).fold(0L) { value, i -> value or ((bytes[at + i].toLong() and 0xFF) shl (8 * i)) }

    /** An ISO 9660 directory record. */
    private fun entry(bytes: ByteArray, at: Int): Entry {
        val length = bytes[at + 32].toInt() and 0xFF
        val name = String(bytes, at + 33, length, Charsets.ISO_8859_1).substringBefore(';')
        return Entry(int32(bytes, at + 2), int32(bytes, at + 10).toInt(), name)
    }

    private fun find(file: RandomAccessFile, folder: Entry, name: String): Entry? {
        val bytes = ByteArray(folder.size)
        file.seek(folder.sector * SECTOR.toLong())
        file.readFully(bytes)
        var at = 0
        while (at < bytes.size) {
            val length = bytes[at].toInt() and 0xFF
            if (length == 0) {
                at = (at / SECTOR + 1) * SECTOR // records do not cross a sector
                continue
            }
            val found = entry(bytes, at)
            if (found.name == name) return found
            at += length
        }
        return null
    }

    private const val SECTOR = 2048
}
