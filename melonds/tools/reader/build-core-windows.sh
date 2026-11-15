#!/usr/bin/env bash
# Configure and build melonDS Reader as a core of Furoshiki for Windows (x86-64), cross-compiled
# with MinGW: build-core-windows/bin/reader_core_melonds.dll. It cannot be run on this machine
# (no Wine here).
#   tools/reader/build-core-windows.sh [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_WINDOWS_BUILD:-$root/build-core-windows}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-8G}")
# The compiler was unpacked from its packages, not installed: it finds its libraries here.
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$build"
export TMPDIR="$build"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root/reader" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/mingw-toolchain.cmake" \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DCMAKE_FIND_ROOT_PATH="$READER_MINGW/x86_64-w64-mingw32" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc -static" \
    -DCMAKE_MODULE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -static" \
    -DREADER_CORE=ON -DREADER_HARNESS=OFF
fi
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
