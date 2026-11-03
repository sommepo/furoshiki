plugins {
    alias(libs.plugins.android.application)
}

fun gitVersionName(): String {
    return providers.exec {
        commandLine("git", "describe", "--tags", "--always")
    }.standardOutput.asText.get().trim()
}

android {
    namespace = "com.flycast.emulator"
    // Reader fork: BuildConfig, for whether this build has save sync.
    buildFeatures {
        buildConfig = true
    }
    // Reader fork: another NDK can be named (-PREADER_NDK=...), to use one that is installed.
    ndkVersion = (project.findProperty("READER_NDK") as String?) ?: "29.0.14206865"

    // Reader fork: JVM tests of the reading layer see the app's resources.
    testOptions {
        unitTests {
            isIncludeAndroidResources = true
        }
    }
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        // Reader fork: an application ID of its own, so that the app installs beside Flycast
        // instead of replacing it; the Java namespace stays com.flycast.emulator.
        applicationId = "io.github.sommepo.flycastreader"
        minSdk = 21
        targetSdk = 35
        versionCode = 8
        versionName = gitVersionName()
        // Reader fork: false in a published build (-PreaderSaveSync=false): no save sync, and
        // no statistics sent to its service.
        buildConfigField("boolean", "READER_SAVE_SYNC",
            (providers.gradleProperty("readerSaveSync").orNull != "false").toString())
        vectorDrawables.useSupportLibrary = true

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_ARM_MODE=arm"
                arguments += "-DSENTRY_UPLOAD_URL=" + (System.getenv("SENTRY_UPLOAD_URL") ?: "")
                arguments += "-DUSE_OPENMP=OFF"
                arguments += "-DANDROID_WEAK_API_DEFS=ON"
                // Reader fork: what tools/reader/build-android.sh asks of the native build
                // (compile jobs, the compiler cache).
                (project.findProperty("READER_CMAKE_ARGS") as String?)?.split(" ")
                    ?.filter { it.isNotBlank() }?.forEach { arguments += it }
                (project.findProperty("READER_ABI") as String?)?.let { abiFilters += it.split(",") }
                // A published build (-PreaderSaveSync=false) has no save sync.
                if (providers.gradleProperty("readerSaveSync").orNull == "false") {
                    arguments += "-DREADER_SAVE_SYNC=OFF"
                }
            }
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    signingConfigs {
        getByName("debug") {
            storeFile = file("../debug.keystore")
        }
        create("release") {
            storeFile = file("../playstore.jks")
            storePassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
            keyAlias = "uploadkey"
            keyPassword = System.getenv("ANDROID_KEYSTORE_PASSWORD")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            signingConfig = signingConfigs.getByName("release")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt")
            version = "3.22.1"
        }
    }
    packaging {
        jniLibs {
            // not used
            excludes += "lib/*/libz.so"
            // This is necessary for libadrenotools custom driver loading
            useLegacyPackaging = true
        }
        resources {
            excludes += "META-INF/DEPENDENCIES"
        }
    }
}

dependencies {
    implementation(libs.appcompat)
    implementation(libs.commons.lang3)
    implementation(libs.httpclient5)
    implementation(libs.slf4j.android)
    implementation(fileTree("libs") { include("*.aar", "*.jar") })
    implementation(libs.documentfile)
    // Reader fork: what the reading layer's screens are made with, and its JVM tests
    // (Robolectric supplies SQLite and JsonReader).
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.preference:preference:1.2.1")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.core:core:1.13.1")
    testImplementation("org.json:json:20240303")
    testImplementation("org.robolectric:robolectric:4.14.1")
    testImplementation("androidx.test:core:1.6.1")
    testImplementation(libs.junit)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(libs.ext.junit)
}

// Reader fork: the bundled text profiles (reader/profiles at the repository's root) go into the
// app as assets/reader/profiles, which is where the reading layer looks for them.
// A published build (-PreaderBundleScripts=false) comes with none: they are installed from the
// published list (Game scripts).
val readerProfiles = tasks.register<Sync>("readerProfiles") {
    from(rootProject.file("../../reader/profiles")) {
        exclude("README.md")
        if (providers.gradleProperty("readerBundleScripts").orNull == "false") exclude("*")
    }
    into(layout.buildDirectory.dir("reader-assets/reader/profiles"))
}
android.sourceSets.getByName("main").assets.directories.add("build/reader-assets")
// Reader fork: the app's own launcher icon (reader/icon at the repository's root).
android.sourceSets.getByName("main").res.directories.add("../../../reader/icon/android/res")
tasks.named("preBuild") { dependsOn(readerProfiles) }
// Reader fork: the reader layer's Kotlin, tests and resources every reader app shares, from the
// repository's shared/android, written into this app's package at build time.
apply(from = "../../../../shared/android/reader.gradle")
