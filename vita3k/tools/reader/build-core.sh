#!/usr/bin/env bash
# Configure and build Vita3K as a core of Furoshiki (Linux): build-core/bin/reader_core_vita3k.so,
# the module Furoshiki loads (reader/core_library, cores/reader_core.h at the top of the
# repository): the emulator without a frontend, drawing with Vulkan into the program's window.
# No Qt, no reader engine (the program has both).
#   tools/reader/build-core.sh [--configure-only | ninja targets...]
# Furoshiki finds it beside its program, in its "cores" folder, or in READER_HOME_CORES.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_BUILD_DIR:-$root/build-core}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
# Its own C++ library inside it, hidden like the rest: the program has its own.
link="-static-libstdc++ -static-libgcc"
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DPKG_CONFIG_EXECUTABLE="$P/pkg-config" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$P/x86_64-conda-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="$P/x86_64-conda-linux-gnu-g++" \
    -DCMAKE_AR="$P/x86_64-conda-linux-gnu-gcc-ar" \
    -DCMAKE_RANLIB="$P/x86_64-conda-linux-gnu-gcc-ranlib" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_SHARED_LINKER_FLAGS="$link" -DCMAKE_MODULE_LINKER_FLAGS="$link" \
    -DCMAKE_C_FLAGS="-isystem $READER_EDEN_TOOLCHAINS/x11-include" \
    -DCMAKE_CXX_FLAGS="-isystem $READER_EDEN_TOOLCHAINS/x11-include" \
    -DX11_X11_INCLUDE_PATH="$READER_EDEN_TOOLCHAINS/x11-include" \
    -DX11_X11_LIB="$READER_VULKAN_PREFIX/lib/libX11.so" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DUSE_DISCORD_RICH_PRESENCE=OFF -DUSE_LTO=NEVER -DSDL_UNIX_CONSOLE_BUILD=ON \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
[ $# -gt 0 ] || set -- reader_core_vita3k
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
