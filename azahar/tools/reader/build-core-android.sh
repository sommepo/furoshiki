#!/usr/bin/env bash
# Configure and build Azahar as a core of Furoshiki for Android, for the one app (android/ at the
# top of the repository): build-core-android/<abi>/bin/RelWithDebInfo/libreader_core_azahar.so,
# Azahar's libretro build with OpenGL ES.
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
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-14G}")
mkdir -p "$build"
export TMPDIR="$build"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$cmake_home/cmake" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$cmake_home/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DANDROID_ARM_NEON=true -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$READER_HOST_PREFIX/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_LIBRETRO=ON -DREADER_CORE=ON -DREADER_SAVE_SYNC=OFF \
    -DENABLE_QT=OFF -DENABLE_SDL2=OFF -DENABLE_TESTS=OFF -DENABLE_READER_HARNESS=OFF \
    -DENABLE_READER_DESKTOP_TESTS=OFF \
    -DENABLE_ROOM=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF \
    -DENABLE_LIBUSB=OFF -DENABLE_OPENGL=ON -DENABLE_VULKAN=OFF \
    -DENABLE_SOFTWARE_RENDERER=OFF -DENABLE_LTO=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF
fi
[ $# -gt 0 ] || set -- citra_libretro
exec "${limit[@]}" "$cmake_home/ninja" -C "$build" "$@"
