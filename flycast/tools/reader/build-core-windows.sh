#!/usr/bin/env bash
# Configure and build Flycast as a core of Reader Home for Windows (x86-64), cross-compiled with
# MinGW: build-core-windows/reader_core_flycast.dll (reader/core_library). It cannot be run on
# this machine (no Wine here).
#   tools/reader/build-core-windows.sh [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_WINDOWS_BUILD:-$root/build-core-windows}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
# The compiler was unpacked from its packages, not installed: it finds the libraries it was
# linked against here. Qt's own build tools (moc, rcc) are those of the Qt for this machine.
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu:$READER_QT_LINUX/lib:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The sources spell Windows' headers as Windows users do; MinGW on Linux has one spelling.
mkdir -p "$build"
python3 "$here/win_headers.py" "$READER_MINGW/share/mingw-w64/include" "$build/win-include" \
  "$root/core" "$root/shell/windows" "$root/reader" >/dev/null
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/mingw-toolchain.cmake" \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_C_FLAGS="-isystem $build/win-include" -DCMAKE_CXX_FLAGS="-isystem $build/win-include" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DQt6_DIR="$READER_QT_WINDOWS/lib/cmake/Qt6" \
    -DCMAKE_PREFIX_PATH="$READER_QT_WINDOWS" \
    -DQT_HOST_PATH="$READER_QT_LINUX" -DQT_HOST_PATH_CMAKE_DIR="$READER_QT_LINUX/lib/cmake" \
    -DCMAKE_FIND_ROOT_PATH="$READER_MINGW/x86_64-w64-mingw32;$READER_QT_WINDOWS" \
    -DUSE_HOST_SDL=OFF -DUSE_HOST_LIBZIP=OFF -DUSE_BREAKPAD=OFF -DUSE_DISCORD=OFF \
    -DUSE_OPENMP=OFF -DUSE_LUA=OFF -DREADER_NO_VIDEO_ROUTING=ON \
    -DLIBRETRO=ON -DREADER_CORE=ON -DUSE_VULKAN=OFF -DUSE_DX9=OFF -DUSE_DX11=OFF \
    -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc -static" \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
export TMPDIR="${TMPDIR_CORE:-$build}"
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
