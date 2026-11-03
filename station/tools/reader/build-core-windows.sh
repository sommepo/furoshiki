#!/usr/bin/env bash
# Configure and build Station as a core of Reader Home for Windows (x86-64), cross-compiled
# with MinGW: build-core-windows/bin/reader_core_station.dll, the module Reader Home's Windows
# build loads (reader/core_library). It cannot be run on this machine (no Wine here).
#   tools/reader/build-core-windows.sh [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_WINDOWS_BUILD:-$root/build-core-windows}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export READER_SDL2_WINDOWS READER_QT_WINDOWS
# The sources spell Windows' headers as Windows users do; MinGW on Linux has one spelling.
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
python3 "$here/win_headers.py" "$READER_MINGW/share/mingw-w64/include" "$build/win-include" \
  "$root/src" "$root/dep" "$root/reader" >/dev/null
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/mingw-toolchain.cmake" \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_C_FLAGS="-isystem $root/reader/windows/include -isystem $build/win-include" \
    -DCMAKE_CXX_FLAGS="-isystem $root/reader/windows/include -isystem $build/win-include" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc -static" \
    -DCMAKE_MODULE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -static" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DBUILD_QT_FRONTEND=OFF -DBUILD_NOGUI_FRONTEND=OFF -DUSE_SDL2=OFF \
    -DENABLE_DISCORD_PRESENCE=OFF -DENABLE_CHEEVOS=OFF \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
exec "${limit[@]}" "$P/ninja" -C "$build" reader_core_station "$@"
