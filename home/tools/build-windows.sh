#!/usr/bin/env bash
# Configure and build Furoshiki for Windows (x86-64), cross-compiled with MinGW. It cannot
# be run on this machine: it is tried on a Windows computer.
#   tools/build-windows.sh [ninja targets...]
# Output: build-windows/reader-home.exe; tools/package-windows.sh makes a folder of it.
# READER_HOME_PUBLIC=1: a published build (HOME_PUBLIC in CMakeLists.txt) in
# build-windows-public: no game scripts, character tables or recorded sounds built in.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/.." && pwd)"
public="${READER_HOME_PUBLIC:-0}"
if [ "$public" = 1 ]; then
  build="${READER_HOME_WINDOWS_BUILD:-$root/build-windows-public}"
else
  build="${READER_HOME_WINDOWS_BUILD:-$root/build-windows}"
fi
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-6G}")
# SDL 2's MinGW package for the program's controllers (HOME_CORES): Station Reader's copy on
# this machine unless the fork's env.sh names one that is there.
[ -f "$READER_SDL2_WINDOWS/lib/cmake/SDL2/sdl2-config.cmake" ] ||
  READER_SDL2_WINDOWS="$HOME/.local/share/station-reader/sdl2-mingw/SDL2-2.32.10/x86_64-w64-mingw32"
export READER_SDL2_WINDOWS
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu:$READER_QT_LINUX/lib:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The game scripts' data files that are not in Git, beside the scripts the program bundles
# (a published build bundles none).
if [ "$public" != 1 ]; then
  "$root/../station/tools/reader/private-data.sh"
  "$root/../pcsx2/tools/reader/private-data.sh"
fi
mkdir -p "$build"
want="$([ "$public" = 1 ] && echo ON || echo OFF)"
if [ -f "$build/CMakeCache.txt" ]; then
  have="$(sed -n 's/^HOME_PUBLIC:BOOL=//p' "$build/CMakeCache.txt")"
  [ "${have:-OFF}" = "$want" ] ||
    { echo "$build is a build with HOME_PUBLIC=${have:-OFF}, not $want: use another folder" >&2; exit 1; }
fi
export TMPDIR="${TMPDIR:-$build}"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/mingw-toolchain.cmake" \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DQt6_DIR="$READER_QT_WINDOWS/lib/cmake/Qt6" \
    -DCMAKE_PREFIX_PATH="$READER_QT_WINDOWS;$READER_SDL2_WINDOWS" \
    -DSDL2_DIR="$READER_SDL2_WINDOWS/lib/cmake/SDL2" -DHOME_CORES=ON \
    -DHOME_PUBLIC="$want" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DQT_HOST_PATH="$READER_QT_LINUX" -DQT_HOST_PATH_CMAKE_DIR="$READER_QT_LINUX/lib/cmake" \
    -DCMAKE_FIND_ROOT_PATH="$READER_MINGW/x86_64-w64-mingw32;$READER_QT_WINDOWS;$READER_SDL2_WINDOWS"
fi
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
