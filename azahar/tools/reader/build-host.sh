#!/usr/bin/env bash
# Configure and build the headless reader harness for this machine.
#   tools/reader/build-host.sh [extra ninja targets...]
# Output: build-host/bin/Release/azahar-reader-harness
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="$root/build-host"
P="$READER_HOST_PREFIX/bin"
# Compilers run under a memory limit like everything else here (see limit.sh).
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}")
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$ANDROID_HOME/cmake/3.31.6/bin/cmake" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$P/x86_64-conda-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="$P/x86_64-conda-linux-gnu-g++" \
    -DCMAKE_AR="$P/x86_64-conda-linux-gnu-gcc-ar" \
    -DCMAKE_RANLIB="$P/x86_64-conda-linux-gnu-gcc-ranlib" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_READER_HARNESS=ON \
    -DENABLE_QT=OFF -DENABLE_SDL2=OFF -DENABLE_TESTS=ON \
    -DENABLE_ROOM=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF \
    -DENABLE_LIBUSB=OFF -DENABLE_OPENGL=OFF -DENABLE_VULKAN=OFF \
    -DENABLE_SOFTWARE_RENDERER=ON -DENABLE_LTO=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF
fi
exec "${limit[@]}" "$P/ninja" -C "$build" "${@:-reader_harness}"
