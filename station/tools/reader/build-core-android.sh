#!/usr/bin/env bash
# Configure and build Station as a core of Furoshiki for Android, for the one app (android/ at the
# top of the repository): build-core-android/<abi>/bin/libreader_core_station.so.
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
mkdir -p "$build"
export TMPDIR="$build"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$cmake_home/cmake" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$cmake_home/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DANDROID_WEAK_API_DEFS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DBUILD_QT_FRONTEND=OFF -DBUILD_NOGUI_FRONTEND=OFF -DUSE_SDL2=OFF \
    -DENABLE_DISCORD_PRESENCE=OFF -DENABLE_CHEEVOS=OFF
fi
[ $# -gt 0 ] || set -- reader_core_station
exec "${limit[@]}" "$cmake_home/ninja" -C "$build" "$@"
