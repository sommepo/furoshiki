#!/usr/bin/env bash
# Configure and build Vita3K as a core of Furoshiki for Windows (x86-64), cross-compiled with
# clang-cl as PCSX2 Reader's is: build-core-windows/bin/reader_core_vita3k.dll and vita3k/ beside
# it. Only the C interface of cores/reader_core.h crosses between it and Furoshiki's MinGW program.
# It cannot be run on this machine (no Wine here).
#   tools/reader/build-core-windows.sh [--configure-only | ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_WINDOWS_BUILD:-$root/build-core-windows}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
# Microsoft's headers under the names the sources call them by (letter case).
export READER_CASE_LINKS="$build/case-links"
mkdir -p "$READER_CASE_LINKS/lib"
ln -sfn "$READER_XWIN/sdk/include/um/ws2tcpip.h" "$READER_CASE_LINKS/Ws2tcpip.h"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/windows-toolchain.cmake" \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DUSE_DISCORD_RICH_PRESENCE=OFF -DUSE_LTO=NEVER -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DFMT_MODULE=OFF \
    ${READER_CORE_WINDOWS_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
[ $# -gt 0 ] || set -- reader_core_vita3k
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
