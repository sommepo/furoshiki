#!/usr/bin/env bash
# Configure and build ARMSX2 as a core of Furoshiki for Android, for the one app (android/ at the
# top of the repository): build-core-android/<abi>/bin/libreader_core_armsx2.so. arm64-v8a only:
# the emulator's recompilers are for that.
#   tools/reader/build-core-android.sh [abi] [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
abi="${1:-arm64-v8a}"; shift || true
build="$root/build-core-android/$abi"
ndk="$ANDROID_HOME/ndk/${READER_NDK:-28.2.13676358}"
cmake_home="$ANDROID_HOME/cmake/3.31.6/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-14G}")
mkdir -p "$build"
export TMPDIR="$build"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$cmake_home/cmake" -S "$root/ARMSX2/app/src/main/cpp" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$cmake_home/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID=true -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release -DLTO_PCSX2_CORE=OFF \
    -DCMAKE_C_FLAGS="-O3 -g" -DCMAKE_CXX_FLAGS="-O3 -g" \
    -DCMAKE_C_COMPILER_LAUNCHER="$READER_TOOLCHAINS/host/bin/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$READER_TOOLCHAINS/host/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON
fi
[ $# -gt 0 ] || set -- reader_core_armsx2
exec "${limit[@]}" "$cmake_home/ninja" -C "$build" "$@"
