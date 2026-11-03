#!/usr/bin/env bash
# Configure and build the desktop app (Qt) for this machine: what the reader's windows and the
# PS2-style menu are developed and tried with, without a display (tools/reader/desktop.sh).
#   tools/reader/build-desktop.sh [ninja targets...]
# Output: build-desktop/bin/pcsx2-qt (READER_DESKTOP_BUILD names another build folder)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_DESKTOP_BUILD:-$root/build-desktop}"
P="$READER_HOST_PREFIX"
"$here/private-data.sh"
export PATH="$P/bin:$PATH"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}")
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$P/bin/clang" -DCMAKE_CXX_COMPILER="$P/bin/clang++" \
    -DCMAKE_EXE_LINKER_FLAGS_INIT="-fuse-ld=lld -Wl,-rpath,$P/lib -Wl,--allow-shlib-undefined" \
    -DCMAKE_MODULE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
    -DCMAKE_SHARED_LINKER_FLAGS_INIT="-fuse-ld=lld" \
    -DCMAKE_PREFIX_PATH="$P" \
    -DCMAKE_CXX_STANDARD_LIBRARIES="-lrt" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/bin/ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$P/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_TESTS=OFF -DUSE_VTUNE=OFF -DLTO_PCSX2_CORE=OFF \
    ${READER_DESKTOP_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
exec "${limit[@]}" ninja -C "$build" "$@"
