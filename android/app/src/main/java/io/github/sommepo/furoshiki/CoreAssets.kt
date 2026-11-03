// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import java.io.File

/** A core's own files, which it reads as files: out of the APK once for each build of the app. */
object CoreAssets {
    /** What a console's core wants in its system folder before it starts. */
    fun forConsole(context: Context, key: String) {
        val system = File(Places.system(context, key))
        when (key) {
            "psp" -> unpack(context, "cores/${Places.SYSTEM_PSP}", File(system, "PPSSPP"))
            "ps2" -> {
                unpack(context, "cores/pcsx2", File(system, "PCSX2/resources"))
                copyBios(context, key, system)
            }
            "psvita" -> {
                unpack(context, "cores/vita3k", File(system, "vita3k"))
                copyBios(context, key, system)
            }
        }
    }

    /** The user's files in the core's folder: PCSX2 writes beside its BIOS, Vita3K installs its firmware from there, Eden wants its keys there. */
    private fun copyBios(context: Context, key: String, system: File) {
        Places.bios(context, key)?.listFiles()?.filter { it.isFile && it.extension != "zip" }?.forEach { file ->
            val copy = File(system, file.name)
            if (!copy.isFile || copy.length() != file.length()) file.copyTo(copy, overwrite = true)
        }
    }

    fun unpack(context: Context, asset: String, to: File) {
        val done = File(to, ".unpacked")
        val build = context.packageManager.getPackageInfo(context.packageName, 0).lastUpdateTime.toString()
        if (done.isFile && done.readText() == build) return
        copy(context, asset, to)
        done.writeText(build)
    }

    private fun copy(context: Context, asset: String, to: File) {
        val inside = context.assets.list(asset).orEmpty()
        if (inside.isEmpty()) {
            to.parentFile?.mkdirs()
            context.assets.open(asset).use { input -> to.outputStream().use { input.copyTo(it) } }
            return
        }
        to.mkdirs()
        for (name in inside) copy(context, "$asset/$name", File(to, name))
    }
}
