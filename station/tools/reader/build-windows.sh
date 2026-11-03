#!/usr/bin/env bash
# Configure and build the desktop app for Windows (x86-64), cross-compiled with MinGW: the
# NoGUI frontend (SDL's window, OpenGL display) with the reader's Qt windows. It cannot be run
# on this machine (no Wine here): it is tried on a Windows computer.
#   tools/reader/build-windows.sh [ninja targets...]     (duckstation-nogui without)
# Output: build-windows/bin/duckstation-nogui.exe; tools/reader/package-windows.sh makes a
# folder of it that runs on Windows.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_WINDOWS_BUILD:-$root/build-windows}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
# The compiler was unpacked from its packages, not installed: it finds the libraries it was
# linked against here. Qt's own build tools (moc, rcc) are those of the Qt for this machine.
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu:$READER_QT_LINUX/lib:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export READER_SDL2_WINDOWS
[ -f "$READER_SDL2_WINDOWS/lib/cmake/SDL2/sdl2-config.cmake" ] || {
  echo "no SDL 2 for MinGW in $READER_SDL2_WINDOWS (SDL2-devel-2.32.10-mingw.tar.gz, unpacked)" >&2; exit 1; }
"$here/private-data.sh"
# The sources spell Windows' headers as Windows users do; MinGW on Linux has one spelling.
mkdir -p "$build"
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
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DQt6_DIR="$READER_QT_WINDOWS/lib/cmake/Qt6" \
    -DCMAKE_PREFIX_PATH="$READER_QT_WINDOWS;$READER_SDL2_WINDOWS" \
    -DQT_HOST_PATH="$READER_QT_LINUX" -DQT_HOST_PATH_CMAKE_DIR="$READER_QT_LINUX/lib/cmake" \
    -DBUILD_QT_FRONTEND=OFF -DBUILD_NOGUI_FRONTEND=ON -DENABLE_DISCORD_PRESENCE=OFF \
    -DENABLE_CHEEVOS=OFF -DREADER_QT=ON \
    ${READER_WINDOWS_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
[ $# -gt 0 ] || set -- duckstation-nogui
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
