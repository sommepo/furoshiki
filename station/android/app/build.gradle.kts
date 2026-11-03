// Reader fork: the app's build in Kotlin script for Gradle 9 and the Android plugin 9 (the
// Flycast Reader fork's), in place of upstream's Groovy file for Gradle 7. The plugin builds
// the reading layer's Kotlin itself.
plugins {
    id("com.android.application")
}

fun gitOutput(vararg args: String): String = try {
    providers.exec {
        commandLine("git", *args)
        isIgnoreExitValue = true
    }.standardOutput.asText.get().trim()
} catch (e: Exception) {
    ""
}

android {
    namespace = "com.github.stenzek.duckstation"
    // Reader fork: BuildConfig, for whether this build has save sync.
    buildFeatures {
        buildConfig = true
    }
    // Reader fork: another NDK can be named (-PREADER_NDK=...), to use one that is installed.
    ndkVersion = (project.findProperty("READER_NDK") as String?) ?: "28.2.13676358"
    compileSdk = 36

    // Reader fork: JVM tests of the reading layer see the app's resources.
    testOptions {
        unitTests {
            isIncludeAndroidResources = true
        }
    }

    defaultConfig {
        // Reader fork: an application ID and a name of its own ("Station Reader"), so that the
        // app installs beside DuckStation instead of replacing it, and is not presented as it;
        // the Java package stays com.github.stenzek.duckstation.
        applicationId = "io.github.sommepo.stationreader"
        // Android 7: the zip library (dep/minizip) uses fopen64 and its kin, which the NDK has
        // from API level 24 on.
        minSdk = 24
        // The 2021 app keeps its files in a folder of the shared storage (/sdcard/station-reader)
        // through Android's legacy storage, which only an app targeting Android 10 is given.
        targetSdk = 29
        versionCode = gitOutput("rev-list", "--first-parent", "--count", "HEAD").toIntOrNull() ?: 1
        versionName = gitOutput("describe", "--tags", "--always").ifEmpty { "0.0-unknown" }
        // Reader fork: false in a published build (-PreaderSaveSync=false): no save sync, and
        // no statistics sent to its service.
        buildConfigField("boolean", "READER_SAVE_SYNC",
            (providers.gradleProperty("readerSaveSync").orNull != "false").toString())

        externalNativeBuild {
            cmake {
                arguments += "-DCMAKE_BUILD_TYPE=Release"
                // Reader fork: the newer NDK refuses calls to functions of a later Android than
                // minSdk (cubeb's AAudio, Android 8) unless they are weak, as Flycast's build has them.
                arguments += "-DANDROID_WEAK_API_DEFS=ON"
                // cubeb names AAudio's functions only to load them itself (dlopen), on a device
                // that has them: a warning, not an error.
                cppFlags += "-Wno-error=unguarded-availability"
                // Reader fork: what tools/reader/build-android.sh asks of the native build
                // (compile jobs, the compiler cache).
                (project.findProperty("READER_CMAKE_ARGS") as String?)?.split(" ")
                    ?.filter { it.isNotBlank() }?.forEach { arguments += it }
                // A published build (-PreaderSaveSync=false) has no save sync.
                if (providers.gradleProperty("readerSaveSync").orNull == "false") {
                    arguments += "-DREADER_SAVE_SYNC=OFF"
                }
                val abis = (project.findProperty("READER_ABI") as String?)?.split(",")
                    ?: listOf("arm64-v8a", "armeabi-v7a", "x86_64")
                abiFilters += abis
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            ndk {
                debugSymbolLevel = "FULL"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.31.6"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    sourceSets {
        getByName("main").assets.directories.add("../../data")
    }
}

dependencies {
    implementation(fileTree("libs") { include("*.jar") })
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.legacy:legacy-support-v4:1.0.0")
    implementation("androidx.viewpager2:viewpager2:1.0.0")
    // Reader fork: what the reading layer's screens are made with, and its JVM tests
    // (Robolectric supplies SQLite and JsonReader).
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.preference:preference:1.2.1")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.core:core:1.13.1")
    testImplementation("org.json:json:20240303")
    testImplementation("org.robolectric:robolectric:4.14.1")
    testImplementation("androidx.test:core:1.6.1")
    testImplementation("junit:junit:4.13.2")
}

// Reader fork: the bundled text profiles (reader/profiles at the repository's root) go into the
// app as assets/reader/profiles, which is where the reading layer looks for them.
// A published build (-PreaderBundleScripts=false) comes with none: they are installed from the
// published list (Game scripts).
val readerProfiles = tasks.register<Sync>("readerProfiles") {
    from(rootProject.file("../reader/profiles")) {
        exclude("README.md")
        if (providers.gradleProperty("readerBundleScripts").orNull == "false") exclude("*")
    }
    into(layout.buildDirectory.dir("reader-assets/reader/profiles"))
}
android.sourceSets.getByName("main").assets.directories.add("build/reader-assets")
// Reader fork: the app's own launcher icon (reader/icon at the repository's root).
android.sourceSets.getByName("main").res.directories.add("../../reader/icon/android/res")
tasks.named("preBuild") { dependsOn(readerProfiles) }
// Reader fork: the reader layer's Kotlin, tests and resources every reader app shares, from the
// repository's shared/android, written into this app's package at build time.
apply(from = "../../../shared/android/reader.gradle")
