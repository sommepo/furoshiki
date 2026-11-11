// Furoshiki for Android: one app in place of the seven reader apps (home/docs/android-one-app.md).
// The reader's Kotlin, tests and resources are the shared ones (../../shared/android), the
// page is home/themes and home/fonts with assets of its own, and the emulators are cores built
// by each fork (tools/reader/build-core-android.sh), which come into the APK as native libraries.
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
)

android {
    namespace = "io.github.sommepo.furoshiki"
    compileSdk = 36
    ndkVersion = (project.findProperty("READER_NDK") as String?) ?: "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.sommepo.furoshiki"
        minSdk = 29
        targetSdk = 35
        versionCode = 6
        versionName = "0.1.6"
        // False in a published build (-PreaderSaveSync=false): no save sync, and no statistics
        // sent to its service.
        buildConfigField("boolean", "READER_SAVE_SYNC",
            (providers.gradleProperty("readerSaveSync").orNull != "false").toString())
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
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.directories.add(layout.buildDirectory.dir("generated/cores").get().asFile.path)
            assets.directories.add(layout.buildDirectory.dir("generated/front").get().asFile.path)
        }
    }

    signingConfigs {
        getByName("debug") {
            storeFile = file("../../flycast/shell/android-studio/debug.keystore")
        }
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.documentfile:documentfile:1.1.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.preference:preference:1.2.1")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.core:core:1.13.1")
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
    "vita3k/reader/profiles",
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

// The page's themes and font, the desktop's (home/themes, home/fonts); the game scripts (above);
// PPSSPP's assets, which its core reads from the system folder; the licence and notices, as the
// Windows package has them (a release writes SOURCE.txt beside them, tools/publish).
val readerFront = tasks.register<Sync>("readerFront") {
    into(layout.buildDirectory.dir("generated/front"))
    from(File(repository, "home/COPYING")) { rename { "COPYING.txt" } }
    from(File(repository, "home/publish/NOTICES.md")) { rename { "NOTICES.txt" } }
    if (bundleScripts) {
        for (folder in scriptFolders) {
            from(File(repository, folder)) {
                into("reader/profiles")
                exclude("index.json", "README.md")
            }
        }
        from(readerNames) { into("reader/names") }
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

tasks.named("preBuild") { dependsOn(readerCores, readerFront) }

apply(from = "../../shared/android/reader.gradle")
