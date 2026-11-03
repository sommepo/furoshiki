#!/usr/bin/env bash
# Configure and build Vita3K as a core of Furoshiki for Android, for the one app (android/ at the
# top of the repository): build-core-android/<abi>/bin/libreader_core_vita3k.so. As the Android
# app's build, no vcpkg: Boost is in external/boost, OpenSSL the Android build Eden's CMake keeps,
# curl's source the one the desktop core's configure fetched.
#   tools/reader/build-core-android.sh [abi] [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
abi="${1:-arm64-v8a}"; shift || true
build="$root/build-core-android/$abi"
: "${READER_ANDROID_OPENSSL:=$root/../eden/.cache/cpm/openssl/android-aarch64-3.6.0-1cb0d36b39}"
: "${READER_ANDROID_CURL:=$root/build-core/_deps/curl-src}"
ndk="$ANDROID_HOME/ndk/29.0.14206865"
sysroot="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-14G}")
mkdir -p "$build"
export TMPDIR="$build"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$READER_HOST_PREFIX/bin/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release -DUSE_LTO=NEVER \
    -DCMAKE_C_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_ANDROID=ON -DREADER_CORE=ON -DUSE_DISCORD_RICH_PRESENCE=OFF \
    -DOPENSSL_ROOT_DIR="$READER_ANDROID_OPENSSL" -DOPENSSL_USE_STATIC_LIBS=ON \
    -DFETCHCONTENT_SOURCE_DIR_CURL="$READER_ANDROID_CURL" -DBUILD_TESTING=OFF -DBUILD_CURL_EXE=OFF \
    -DZLIB_INCLUDE_DIR="$sysroot/include" -DZLIB_LIBRARY="$sysroot/lib/aarch64-linux-android/libz.a"
fi
[ $# -gt 0 ] || set -- reader_core_vita3k
exec "${limit[@]}" "$READER_HOST_PREFIX/bin/ninja" -C "$build" "$@"
