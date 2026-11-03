#!/usr/bin/env bash
# Configure and build PCSX2 as a core of Reader Home for Windows (x86-64), cross-compiled with
# clang-cl as the app's Windows build is (tools/reader/build-windows.sh): the C interface of
# cores/reader_core.h is all that crosses between it and Reader Home's MinGW program. It cannot
# be run on this machine (no Wine here).
#   tools/reader/build-core-windows.sh [ninja targets...]
# Output: build-core-windows/reader/core_library/reader_core_pcsx2.dll. It takes PCSX2's libraries of its
# package of Windows libraries (SDL3.dll, zstd.dll, ...) from beside the program, and its
# resources from the "pcsx2" folder beside it (bin/resources here).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_WINDOWS_BUILD:-$root/build-core-windows}"
P="$READER_HOST_PREFIX"
export PATH="$P/bin:$PATH"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
# Microsoft's headers under the names the sources call them by (letter case).
python3 "$here/windows-case.py" "$root" "$READER_XWIN" "$READER_WINDOWS_HOME/case-links" >/dev/null
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/windows-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$READER_WINDOWS_DEPS" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/bin/ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$P/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF -DUSE_VTUNE=OFF -DLTO_PCSX2_CORE=OFF \
    ${READER_CORE_WINDOWS_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
if [ $# -eq 0 ]; then set -- reader_core_pcsx2; fi
exec "${limit[@]}" ninja -C "$build" "$@"
