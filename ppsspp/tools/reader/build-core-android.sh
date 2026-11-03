#!/usr/bin/env bash
# Configure and build PPSSPP as a core of Furoshiki for Android, for the one app (android/ at the
# top of the repository): build-core-android/<abi>/lib/libreader_core_ppsspp.so.
#   tools/reader/build-core-android.sh [abi] [ninja targets...]
# abi: arm64-v8a by default; x86_64 for an emulator.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
abi="${1:-arm64-v8a}"; shift || true
build="$root/build-core-android/$abi"
ndk="$ANDROID_HOME/ndk/${READER_NDK:-28.2.13676358}"
cmake_home="$ANDROID_HOME/cmake/3.31.6/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$cmake_home/cmake" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$cmake_home/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DLIBRETRO=ON -DREADER_CORE=ON -DUSING_QT_UI=OFF -DHEADLESS=OFF -DUNITTEST=OFF -DATLAS_TOOL=OFF \
    -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF -DUSE_CCACHE=OFF
fi
export TMPDIR="$build"
[ $# -gt 0 ] || set -- ppsspp_libretro
exec "${limit[@]}" "$cmake_home/ninja" -C "$build" "$@"
