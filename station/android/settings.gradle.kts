// Reader fork: Gradle 9 and the Android plugin 9, which builds Kotlin itself (the reading
// layer is Kotlin); the build was Groovy for Gradle 7 before.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "StationReader"
include(":app")
