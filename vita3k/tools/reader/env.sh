# Source this file: where the toolchains are, for every script in tools/reader. As in the other
# forks: a file private/env.sh in the fork (not in Git) is read first, for a machine's own
# settings, and each path below is a variable.
_reader_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck disable=SC1091
[ -f "$_reader_root/private/env.sh" ] && . "$_reader_root/private/env.sh"
unset _reader_root

export READER_HOME="${READER_HOME:-$HOME/.local/share/vita3k-reader}"
# The compilers: a GCC with its own C library, cmake, ninja, ccache, pkg-config (conda-forge).
export READER_TOOLCHAINS="${READER_TOOLCHAINS:-$READER_HOME}"
export READER_HOST_PREFIX="$READER_TOOLCHAINS/host"
# X11's headers for that GCC, and a software Vulkan driver.
export READER_EDEN_TOOLCHAINS="${READER_EDEN_TOOLCHAINS:-$READER_HOME}"
export READER_VULKAN_PREFIX="${READER_VULKAN_PREFIX:-$READER_EDEN_TOOLCHAINS/vulkan}"
export READER_CMAKE="${READER_CMAKE:-$READER_HOST_PREFIX/bin/cmake}"
export CCACHE_DIR="${CCACHE_DIR:-$READER_HOME/ccache}"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-8G}"
# Android: a JDK and the SDK, with the NDK Vita3K names.
export JAVA_HOME="${JAVA_HOME:-$READER_TOOLCHAINS/jdk}"
export ANDROID_HOME="${ANDROID_HOME:-$READER_TOOLCHAINS/android-sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"
# Parallel compile jobs.
export READER_JOBS="${READER_JOBS:-8}"

# The Windows core is cross-compiled as PCSX2 Reader's: a Clang, and Microsoft's C runtime and
# Windows SDK as xwin lays them out.
export READER_CLANG_PREFIX="${READER_CLANG_PREFIX:-$HOME/.local/share/ps2-reader/host}"
export READER_WINDOWS_HOME="${READER_WINDOWS_HOME:-$HOME/.local/share/ps2-reader/windows}"
export READER_XWIN="${READER_XWIN:-$READER_WINDOWS_HOME/xwin-sdk}"
