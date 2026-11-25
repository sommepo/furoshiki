// Furoshiki for Android: one app in place of the seven reader apps (home/docs/android-one-app.md).
// The reader's Kotlin, tests and resources are the shared ones (../../shared/android), the
// page is home/themes and home/fonts with assets of its own, and the emulators are cores built
// by each fork (tools/reader/build-core-android.sh), which come into the APK as native libraries.
import java.net.URI
import java.security.MessageDigest

plugins {
    id("com.android.application")
}

val repository = rootDir.parentFile
/** The processors to package: arm64-v8a, or x86_64 for an emulator (-PREADER_ABI=...). */
val readerAbis = ((project.findProperty("READER_ABI") as String?) ?: "arm64-v8a").split(",")

/** The cores this build carries: each fork's Android build, under the name the app looks for. */
val coreBuilds = mapOf(
    "reader_core_ppsspp" to "ppsspp/build-core-android/%s/libreader_core_ppsspp_android.so",
    "reader_core_flycast" to "flycast/build-core-android/%s/libreader_core_flycast.so",
    "reader_core_azahar" to "azahar/build-core-android/%s/bin/RelWithDebInfo/libreader_core_azahar.so",
    "reader_core_station" to "station/build-core-android/%s/bin/libreader_core_station.so",
    "reader_core_armsx2" to "armsx2/build-core-android/%s/bin/libreader_core_armsx2.so",
    "reader_core_vita3k" to "vita3k/build-core-android/%s/bin/libreader_core_vita3k.so",
    "reader_core_melonds" to "melonds/build-core-android/%s/bin/libreader_core_melonds.so",
)

android {
    namespace = "io.github.sommepo.furoshiki"
    compileSdk = 36
    ndkVersion = (project.findProperty("READER_NDK") as String?) ?: "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.sommepo.furoshiki"
        minSdk = 29
        targetSdk = 35
        versionCode = 130
        // A release is called what its tag is (-PFUROSHIKI_VERSION, furoshiki-release.sh): an
        // updater that compares the two (Obtainium) then sees the release as installed.
        versionName = (project.findProperty("FUROSHIKI_VERSION") as String?) ?: "0.7.35"
        // False in a published build (-PreaderSaveSync=false): no save sync, and no statistics
        // sent to its service.
        buildConfigField("boolean", "READER_SAVE_SYNC",
            (providers.gradleProperty("readerSaveSync").orNull != "false").toString())
        // The release this build is made for ("0.2.6", -PfuroshikiRelease= from
        // tools/publish/furoshiki-release.sh), compared with GitHub's latest release by the in-app
        // updater (Updater.kt). Empty in any other build, which then has no GitHub updater.
        buildConfigField("String", "FUROSHIKI_RELEASE",
            "\"" + (providers.gradleProperty("furoshikiRelease").orNull ?: "").filter { it.isLetterOrDigit() || it in ".-" } + "\"")
        ndk {
            abiFilters += readerAbis
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_static"
                if (providers.gradleProperty("readerSaveSync").orNull == "false") {
                    arguments += "-DREADER_SAVE_SYNC=OFF"
                }
                (project.findProperty("READER_CMAKE_ARGS") as String?)?.split(" ")
                    ?.filter { it.isNotBlank() }?.forEach { arguments += it }
            }
        }
    }

    buildFeatures {
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    // A core is dlopened by its path, so it must lie unpacked in the app's library folder.
    packaging {
        jniLibs.useLegacyPackaging = true
    }

    testOptions {
        unitTests {
            isIncludeAndroidResources = true
            // The OCR models, for the test that reads a real picture with them (skipped without them).
            all { it.systemProperty("ocr.models", File(rootDir, ".gradle/ocr-models").path) }
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.directories.add(layout.buildDirectory.dir("generated/cores").get().asFile.path)
            assets.directories.add(layout.buildDirectory.dir("generated/front").get().asFile.path)
            assets.directories.add(layout.buildDirectory.dir("generated/ocr").get().asFile.path)
        }
    }

    signingConfigs {
        getByName("debug") {
            storeFile = file("../../flycast/shell/android-studio/debug.keystore")
        }
    }
}

val onnxRuntime = "1.22.0"

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.documentfile:documentfile:1.1.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.preference:preference:1.2.1")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.core:core:1.13.1")
    // OCR on the device (ocr/MeikiOcr.kt): ONNX Runtime (MIT) runs meikiocr's models. The tests run
    // the same code on this machine's processor with the runtime's desktop build.
    implementation("com.microsoft.onnxruntime:onnxruntime-android:$onnxRuntime")
    testImplementation("com.microsoft.onnxruntime:onnxruntime:$onnxRuntime")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
    testImplementation("org.robolectric:robolectric:4.14.1")
    testImplementation("androidx.test:core:1.6.1")
}

// The cores, each fork's build, under the names GameNative looks for.
val readerCores = tasks.register<Sync>("readerCores") {
    into(layout.buildDirectory.dir("generated/cores"))
    for (abi in readerAbis) {
        for ((name, build) in coreBuilds) {
            val file = File(repository, build.format(abi))
            if (file.isFile) {
                from(file) {
                    into(abi)
                    rename { "lib$name.so" }
                }
            } else {
                logger.warn("No $name for $abi: ${file.path} (its tools/reader/build-core-android.sh makes it)")
            }
        }
    }
}

// The game scripts of the consoles this app has a core for. A published build bundles none
// (-PreaderBundleScripts=false, in its gradle.properties): it installs them from each console's
// published list, as the desktop program does.
val bundleScripts = providers.gradleProperty("readerBundleScripts").orNull != "false"
val scriptFolders = listOf(
    "ppsspp/assets/reader/profiles", "flycast/reader/profiles", "station/reader/profiles",
    "azahar/src/android/app/src/main/assets/reader/profiles", "armsx2/ARMSX2/app/src/main/assets/reader/profiles",
    "vita3k/reader/profiles", "melonds/reader/profiles",
)

// The games' names dictionaries (tools/titles.py: docs/titles.md), made from titles/, which the
// reader installs as a game starts (NamesDictionaries, assets reader/names/). Bundled with the
// scripts: a published build has none, its names come with each script from the list.
val readerNames = tasks.register<Exec>("readerNames") {
    val out = layout.buildDirectory.dir("generated/names").get().asFile
    inputs.dir(File(repository, "titles"))
    inputs.file(File(repository, "tools/titles.py"))
    outputs.dir(out)
    doFirst { out.deleteRecursively() }
    commandLine("python3", File(repository, "tools/titles.py").path, "build", "--quiet", "--out", out.path)
}

// The title records as the page reads them (tools/titles-index.py, docs/titles.md): a title's
// English name for its spine, its people for its stage, its deck on Jiten for the study tracker's
// figures, and its box's spine and front where this checkout has them (titles/build, not in Git).
val readerTitles = tasks.register<Exec>("readerTitles") {
    val out = layout.buildDirectory.dir("generated/titles").get().asFile
    inputs.dir(File(repository, "titles"))
    inputs.file(File(repository, "android/tools/titles-index.py"))
    // Not in a published checkout: the box pictures are then left out (titles-index.py).
    File(repository, "tools/titles.py").takeIf { it.isFile }?.let { inputs.file(it) }
    outputs.dir(out)
    doFirst { out.deleteRecursively() }
    commandLine("python3", File(repository, "android/tools/titles-index.py").path, out.path)
}

// The licence texts the app carries (assets/licenses, shown by Settings › About › Open-source
// licences): each core's licence and its fork's bundled libraries' (third-party/), the fonts'
// OFL, the Apache licence, ONNX Runtime's, the LGPL of meikiocr's models, the names data's ODbL
// (home/tools/licences.py, which home/publish/NOTICES.md lists).
val readerLicences = tasks.register<Exec>("readerLicences") {
    val out = layout.buildDirectory.dir("generated/licences").get().asFile
    val script = File(repository, "home/tools/licences.py")
    inputs.file(script)
    inputs.dir(File(repository, "home/publish/licenses"))
    inputs.dir(File(repository, "home/fonts"))
    inputs.file(File(repository, "titles/LICENSE"))
    outputs.dir(out)
    doFirst { out.deleteRecursively() }
    // A release (-PfuroshikiRelease) must have every text; a dev build or a test run in a checkout
    // without every submodule only reports what is missing.
    val strict = providers.gradleProperty("furoshikiRelease").isPresent
    // Where the forks' bundled libraries are: a release builds from an export, which has no
    // submodules, and names the checkout it was made from (-PreaderForks, furoshiki-release.sh).
    val forks = providers.gradleProperty("readerForks").orNull ?: repository.path
    // The cores this build carries (coreBuilds): Eden's where the build has it.
    val cores = coreBuilds.keys.filter { it.startsWith("reader_core_") }.joinToString(",") { it.removePrefix("reader_core_") }
    commandLine(listOf("python3", script.path, "--platform", "android", "--root", repository.path,
        "--forks", forks, "--out", File(out, "licenses").path, "--cores", cores) + if (strict) emptyList() else listOf("--lenient"))
}

// The page's themes and font, the desktop's (home/themes, home/fonts); the game scripts (above);
// PPSSPP's assets, which its core reads from the system folder; the licence and notices, as the
// Windows package has them (a release writes SOURCE.txt beside them, tools/publish).
val readerFront = tasks.register<Sync>("readerFront") {
    into(layout.buildDirectory.dir("generated/front"))
    from(File(repository, "home/COPYING")) { rename { "COPYING.txt" } }
    from(File(repository, "home/publish/NOTICES.md")) { rename { "NOTICES.txt" } }
    from(readerLicences)
    if (bundleScripts) {
        for (folder in scriptFolders) {
            from(File(repository, folder)) {
                into("reader/profiles")
                // An exported tree's folders also hold the names dictionaries (one title's in two
                // consoles' folders); they come once, from readerNames, below.
                exclude("index.json", "README.md", "names-*.zip")
            }
        }
        from(readerNames) { into("reader/names") }
    }
    from(readerTitles)
    // The controllers drawn and the actions a button can be given (the mapping screen, map.js), the desktop's alike.
    from(File(repository, "shared/controllers")) {
        into("front")
        include("controllers.json")
    }
    // The looks of Furoshiki 3's shelf (Kon, Mono, Paper, Solarized): six colours each.
    from(File(repository, "home/shelf")) { into("front/themes") }
    from(File(repository, "home/fonts")) {
        into("front/fonts")
        include("*.ttf")
    }
    // The maintainer's own settings, when this machine has them: nothing of this is in Git.
    from(File(repository, "android/private")) { include("setup.json") }
    from(File(repository, "vita3k/data")) { into("cores/vita3k/data") }
    from(File(repository, "vita3k/vita3k/shaders-builtin")) { into("cores/vita3k/shaders-builtin") }
    from(File(repository, "armsx2/ARMSX2/app/src/main/assets/resources")) {
        into("cores/pcsx2")
        exclude(
            "icons/**", "fullscreenui/**", "sounds/**", "cover-placeholder.png", "game_controller_db.txt",
            "RedumpDatabase.yaml", "fonts/NotoColorEmoji*", "fonts/Twemoji*",
        )
    }
    from(File(repository, "ppsspp/assets")) {
        into("cores/ppsspp")
        exclude("debugger/**", "reader/**", "lang/**", "*.ttf", "gamecontrollerdb.txt", "redump.csv")
    }
    from(File(repository, "ppsspp/assets/lang")) {
        into("cores/ppsspp/lang")
        include("ja_JP.ini", "en_US.ini")
    }
}

// meikiocr's models for OCR on the device (ocr/MeikiOcr.kt; rtr46's, LGPL-3.0, from Hugging Face
// at the commits named, each checked against its SHA-256): text lines found, then read, lying and
// upright. Not in Git: fetched once into .gradle/ocr-models, or copied from READER_OCR_MODELS (a
// folder that has the three files). -PreaderOcrModels=false builds without them: the app then
// has only the OCR service.
val ocrModels = listOf(
    Triple("detect.onnx", "rtr46/meiki.text.detect.v0/resolve/a9cffa4f60cbf72ddb87edf19c6f98a01cd042e6/meiki.text.detect.v0.1.960x544.onnx",
        "40b6a016667745cae7d3055929ae3b8b1e7716aac795f5904cd3c2c7c3b8404b"),
    Triple("read.onnx", "rtr46/meiki.txt.recognition.v0/resolve/a28cf5874dc2438ebb1c86336be26bcec51e3375/meiki.text.rec.v0.960x32.onnx",
        "3e96bc772fbee9717e536a6353032bb944c3382dd2f6960ef4890decda43b000"),
    Triple("read-upright.onnx", "rtr46/meiki.txt.recognition.v0/resolve/a28cf5874dc2438ebb1c86336be26bcec51e3375/meiki.text.rec.v0.vertical.32x480.onnx",
        "2c2a83a23bc3b7e6c63962175f507ecc6c5e85cc174f17bdec37d9bbd0bf895a"),
)
val bundleOcrModels = providers.gradleProperty("readerOcrModels").orNull != "false"
val ocrModelsKept = File(rootDir, ".gradle/ocr-models")
val readerOcrModels = tasks.register("readerOcrModels") {
    val out = layout.buildDirectory.dir("generated/ocr").get().asFile
    val bundle = bundleOcrModels
    val kept = ocrModelsKept
    val models = ocrModels
    val given = providers.environmentVariable("READER_OCR_MODELS").orNull
    inputs.property("models", models.map { it.third } + bundle)
    outputs.dir(out)
    doLast {
        fun sha(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input ->
                val block = ByteArray(1 shl 16)
                while (true) {
                    val read = input.read(block)
                    if (read < 0) break
                    digest.update(block, 0, read)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }
        out.deleteRecursively()
        File(out, "ocr").mkdirs()
        if (bundle) {
            kept.mkdirs()
            for ((name, address, sum) in models) {
                val file = File(kept, name)
                if (!file.isFile || sha(file) != sum) {
                    val own = given?.let { File(it, address.substringAfterLast('/')) }?.takeIf { it.isFile }
                    if (own != null) {
                        own.copyTo(file, overwrite = true)
                    } else {
                        logger.lifecycle("Fetching $name (meikiocr, from Hugging Face)")
                        val part = File(kept, "$name.part")
                        URI("https://huggingface.co/$address").toURL().openStream().use { input ->
                            part.outputStream().use { input.copyTo(it) }
                        }
                        part.renameTo(file)
                    }
                    if (sha(file) != sum) {
                        file.delete()
                        throw GradleException("$name is not the file expected (its SHA-256 differs)")
                    }
                }
                file.copyTo(File(out, "ocr/$name"))
            }
        }
    }
}

tasks.named("preBuild") { dependsOn(readerCores, readerFront, readerOcrModels) }

apply(from = "../../shared/android/reader.gradle")
