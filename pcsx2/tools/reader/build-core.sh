#!/usr/bin/env bash
# Configure and build PCSX2 as a core of Reader Home (Linux): build-core/bin/reader_core_pcsx2.so,
# the module Reader Home loads (reader/core_library, cores/reader_core.h at the top of the
# repository). No Qt frontend: the program has the window, the settings and the reader.
#   tools/reader/build-core.sh [ninja targets...]
# Reader Home finds it beside its program, in its "cores" folder, or in READER_HOME_CORES, and
# PCSX2's resources (shaders, fonts, the game index) in a "pcsx2" folder beside it
# (bin/resources here: Reader Home's tools/desktop.sh links it).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_BUILD_DIR:-$root/build-core}"
P="$READER_HOST_PREFIX"
export PATH="$P/bin:$PATH"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
mkdir -p "$build"
# /tmp has a quota on this machine: the compiler's files go in the build folder.
export TMPDIR="${TMPDIR_CORE:-$build}"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$P/bin/clang" -DCMAKE_CXX_COMPILER="$P/bin/clang++" \
    -DCMAKE_EXE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
    -DCMAKE_MODULE_LINKER_FLAGS_INIT="-fuse-ld=lld -Wl,-rpath,$P/lib" \
    -DCMAKE_SHARED_LINKER_FLAGS_INIT="-fuse-ld=lld" \
    -DCMAKE_PREFIX_PATH="$P" \
    -DCMAKE_CXX_STANDARD_LIBRARIES="-lrt" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/bin/ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$P/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF -DUSE_VTUNE=OFF -DLTO_PCSX2_CORE=OFF \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
if [ $# -eq 0 ]; then set -- reader_core_pcsx2; fi
exec "${limit[@]}" ninja -C "$build" "$@"
