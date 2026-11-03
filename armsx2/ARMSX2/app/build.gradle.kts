plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.compose.compiler)
}

android {
    namespace = "com.armsx2"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        // Reader fork: an app of its own, beside an ARMSX2 on the same device.
        applicationId = "io.github.sommepo.armsx2reader"
        // Reader fork: false in a published build (-PreaderSaveSync=false): no statistics sent
        // to a save sync service.
        buildConfigField("boolean", "READER_SAVE_SYNC",
            (providers.gradleProperty("readerSaveSync").orNull != "false").toString())
        // The same for the native side: no save sync, each game on the settings' memory cards.
        if (providers.gradleProperty("readerSaveSync").orNull == "false") {
            externalNativeBuild { cmake { arguments += "-DREADER_SAVE_SYNC=OFF" } }
        }
        minSdk = 26
        targetSdk = 36
        versionCode = 15
        versionName = "1.0-overlay"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Sign release with the debug keystore so it's installable on-device
            // without a separate signing config. NOT for distribution — the debug
            // keystore is well-known and not secure for Play Store uploads.
            // Replace with a real release signingConfig before publishing.
            signingConfig = signingConfigs.getByName("debug")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            externalNativeBuild {
                cmake {
                    arguments += "-DANDROID=true"
                    arguments += "-DANDROID_STL=c++_static"
                    arguments += "-DCMAKE_BUILD_TYPE=Release"
                    arguments += "-DLTO_PCSX2_CORE=ON"
                    arguments += "-DCMAKE_C_FLAGS=-O3 -g"
                    arguments += "-DCMAKE_CXX_FLAGS=-O3 -g"
                }
            }
        }
        debug {
            // Keep PCSX2_DEBUG/VIXL_DEBUG defines (via CMAKE_BUILD_TYPE=Debug)
            // but compile at -O3 to match release's
            // codegen. -O0 was exposing a JIT-adjacent crash in MGS2 that
            // -O3 release didn't hit, which narrows the cause to stack/
            // uninitialised-local fragility rather than the debug defines.
            // -ffp-contract=off was previously kept for VU1 bit-exactness
            // but only affects C/C++ FP code, not JIT-emitted FMUL/FADD.
            // Removing it lets the compiler fuse a*b+c → FMADD in counters,
            // GS software renderer, SPU2 audio mixing, IPU, VIF unpack —
            // significant FP-heavy paths. JIT'd VU FMAC semantics are
            // unaffected because the recompiler emits explicit Fmul+Fadd.
            externalNativeBuild {
                cmake {
                    arguments += "-DANDROID=true"
                    arguments += "-DANDROID_STL=c++_static"
                    arguments += "-DCMAKE_BUILD_TYPE=Debug"
                    arguments += "-DCMAKE_C_FLAGS=-O3 -g"
                    arguments += "-DCMAKE_CXX_FLAGS=-O3 -g"
                }
            }
        }
    }
    // Reader fork: a published build (-PreaderBundleScripts=false) comes with no game scripts;
    // they are installed from the published list (System Configuration, Game Scripts). The
    // patterns before the last are aapt's own defaults.
    if (providers.gradleProperty("readerBundleScripts").orNull == "false") {
        androidResources {
            ignoreAssetsPattern = "!.svn:!.git:!.ds_store:!*.scc:.*:!CVS:!thumbs.db:!picasa.ini:!*~:!<dir>profiles"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
    // Reader fork: tests see the app's resources, and are told where to write pictures.
    testOptions {
        unitTests {
            isIncludeAndroidResources = true
            all {
                it.systemProperty("reader.preview.dir", System.getenv("READER_PREVIEW_DIR") ?: "")
                it.systemProperty("robolectric.graphicsMode", "NATIVE")
                it.systemProperty("robolectric.pixelCopyRenderMode", "hardware")
            }
        }
    }
    buildFeatures {
        // Generated BuildConfig.DEBUG used by Main.kt's debug-only auto-boot
        // path. AGP 8 made this opt-in.
        buildConfig = true
    }

    packaging {
        jniLibs {
            // libadrenotools requires `useLegacyPackaging = true` so the
            // hook .so files (hook_impl, main_hook, file_redirect_hook,
            // gsl_alloc_hook) get extracted to ApplicationInfo.nativeLibraryDir
            // at install time. Without this, AGP leaves them inside the apk
            // and adrenotools' linker-namespace bypass can't find them by
            // path — the custom Vulkan driver load silently falls back to
            // the system loader.
            useLegacyPackaging = true
        }
    }
}

// Android Studio's "Build > Clean Project" runs the `clean` task, but AGP
// leaves `app/.cxx/` (the CMake/Ninja workspace) in place. Stale .cxx state
// can lead to ghost builds — old object files linking against newer headers,
// or vice versa. Wire `cleanCxx` into `clean` so the native build workspace
// gets wiped too.
tasks.register<Delete>("cleanCxx") {
    delete(layout.projectDirectory.dir(".cxx"))
}
tasks.named("clean") {
    dependsOn("cleanCxx")
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)

    //AndroidX Compose
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.material3)
    implementation(libs.composeIcons.fontAwesome)
    implementation(libs.composeIcons.lineAwesome)

    implementation(libs.kotlin.reflect)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.foundation)
    implementation(libs.androidx.documentfile)
    implementation(libs.coil.compose)
    // Reader fork: the reading layer's settings pages and lists.
    implementation("androidx.preference:preference:1.2.1")
    implementation("androidx.recyclerview:recyclerview:1.3.2")

    testImplementation(libs.junit)
    // Reader fork: the reading layer's tests run off the device, and draw its screens there.
    testImplementation("org.robolectric:robolectric:4.14.1")
    testImplementation("org.json:json:20240303")
    testImplementation("androidx.test:core:1.6.1")
    testImplementation(platform(libs.androidx.compose.bom))
    testImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    debugImplementation(libs.androidx.compose.ui.tooling)
}
// Reader fork: the reader layer's Kotlin, tests and resources every reader app shares, from the
// repository's shared/android, written into this app's package at build time.
apply(from = "../../../shared/android/reader.gradle")
