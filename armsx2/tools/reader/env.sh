# Source this file: toolchain locations for the reader fork on this machine.
# The Android SDK and the JDK are the ones the sibling forks use (used, and added to by
# Gradle when a platform or an NDK is missing; nothing in them is changed).
export READER_TOOLCHAINS="${READER_TOOLCHAINS:-$HOME/.local/share/azahar-reader}"
export JAVA_HOME="${JAVA_HOME:-$READER_TOOLCHAINS/jdk-21}"
export ANDROID_HOME="${ANDROID_HOME:-$READER_TOOLCHAINS/android-sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$HOME/.local/share/ps2-reader/gradle}"
export READER_PREVIEW_FONT_FILE="${READER_PREVIEW_FONT_FILE:-$READER_TOOLCHAINS/fonts/NotoSansJP-Regular.otf}"
# Parallel compile jobs. This is a shared home server: stay well under 16 cores.
export READER_JOBS="${READER_JOBS:-8}"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"
