#!/usr/bin/env bash
# Configure and build melonDS Reader as a core of Furoshiki (Linux): build-core/bin/reader_core_melonds.so,
# the module Furoshiki loads (reader/core_library, cores/reader_core.h at the top of the repository).
# With --harness, the headless harness instead: build-harness/bin/melonds-reader-harness.
#   tools/reader/build-core.sh [--harness] [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
harness=OFF core=ON build="${READER_CORE_BUILD_DIR:-$root/build-core}"
if [ "${1:-}" = "--harness" ]; then
  harness=ON core=OFF build="$root/build-harness"; shift
fi
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-8G}")
mkdir -p "$build"
export TMPDIR="$build"
link="-static-libstdc++ -static-libgcc -Wl,-rpath,$READER_MESA_PREFIX/lib -Wl,-rpath-link,$READER_MESA_PREFIX/lib"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root/reader" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$P/x86_64-conda-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="$P/x86_64-conda-linux-gnu-g++" \
    -DCMAKE_AR="$P/x86_64-conda-linux-gnu-gcc-ar" \
    -DCMAKE_RANLIB="$P/x86_64-conda-linux-gnu-gcc-ranlib" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_PREFIX_PATH="$READER_MESA_PREFIX" \
    -DCMAKE_SHARED_LINKER_FLAGS="$link" -DCMAKE_MODULE_LINKER_FLAGS="$link" -DCMAKE_EXE_LINKER_FLAGS="$link" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=$core -DREADER_HARNESS=$harness \
    ${READER_CORE_CMAKE_ARGS:-}
fi
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
