# Source this file: toolchain locations for the reader fork on this machine.
# Everything lives outside the repository in a path without spaces (NDK and
# conda toolchains are unreliable under paths containing spaces).
export READER_TOOLCHAINS="${READER_TOOLCHAINS:-$HOME/.local/share/azahar-reader}"
# A JDK 21: the toolchains' own (on the maintainer's machine a link to the JDK it was first
# taken from), unless JAVA_HOME names one.
export JAVA_HOME="${JAVA_HOME:-$READER_TOOLCHAINS/jdk-21}"
export ANDROID_HOME="$READER_TOOLCHAINS/android-sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export READER_HOST_PREFIX="$READER_TOOLCHAINS/host"
export CCACHE_DIR="$READER_TOOLCHAINS/ccache"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-8G}"
export NDK_CCACHE="$READER_HOST_PREFIX/bin/ccache"
# Reverse-engineering helpers for writing text profiles (see docs/reader/harness.md):
# a Python with the Capstone disassembler and Pillow, clang-format, and a Ghidra install.
# Capstone's Python module needs CONDA_PREFIX to find its library.
export READER_RE_PREFIX="$READER_TOOLCHAINS/re"
export READER_RE_PYTHON="$READER_RE_PREFIX/bin/python"
export READER_CLANG_FORMAT="$READER_RE_PREFIX/bin/clang-format"
export READER_GHIDRA="$READER_TOOLCHAINS/ghidra_12.1.4_PUBLIC"
export READER_PREVIEW_FONT_FILE="$READER_TOOLCHAINS/fonts/NotoSansJP-Regular.otf"
# Release builds are signed with this fork's own key. Without the keystore the build falls
# back to the debug key. Keep both keystores: an update signed with a different key cannot
# be installed over the app, and uninstalling loses the reader's database.
if [ -f "$READER_TOOLCHAINS/signing/reader-release.jks" ]; then
  export ANDROID_KEYSTORE_FILE="$READER_TOOLCHAINS/signing/reader-release.jks"
  export ANDROID_KEYSTORE_PASS="$(cat "$READER_TOOLCHAINS/signing/password")"
  export ANDROID_KEY_ALIAS="reader"
fi
# Parallel compile jobs. This is a shared home server: stay well under 16 cores.
export READER_JOBS="${READER_JOBS:-8}"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmake/3.31.6/bin:$PATH"

# The desktop app (Qt): a build for Linux, which is what the desktop reader layer is tried with
# here (without a display), and one for Windows, cross-compiled with MinGW. Qt for both, the
# MinGW compiler, the shader compiler and the software Vulkan driver are the ones Eden Reader's
# setup unpacked (its tools/reader/setup.sh windows desktop), used and not changed.
export READER_EDEN_TOOLCHAINS="${READER_EDEN_TOOLCHAINS:-$HOME/.local/share/eden-reader}"
export READER_QT_VERSION="${READER_QT_VERSION:-6.9.3}"
export READER_QT_LINUX="$READER_EDEN_TOOLCHAINS/qt/linux-amd64-$READER_QT_VERSION"
export READER_QT_SYSLIBS="$READER_EDEN_TOOLCHAINS/qt/syslibs/root/usr/lib/x86_64-linux-gnu"
export READER_QT_WINDOWS="$READER_EDEN_TOOLCHAINS/qt/official/$READER_QT_VERSION/mingw_64"
export READER_MINGW="$READER_EDEN_TOOLCHAINS/mingw/root/usr"
export READER_VULKAN_PREFIX="$READER_EDEN_TOOLCHAINS/vulkan"
export READER_CMAKE="${READER_CMAKE:-$ANDROID_HOME/cmake/3.31.6/bin/cmake}"
