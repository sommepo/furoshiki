#!/usr/bin/env bash
# Configure and build the desktop app for Windows (x86-64), cross-compiled with clang-cl. It
# cannot be run on this machine (no Wine here): it is tried on a Windows computer.
#   tools/reader/build-windows.sh [ninja targets...]
# Output: build-windows/bin/pcsx2-qt.exe; tools/reader/package-windows.sh makes a folder of
# it that runs on Windows.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_WINDOWS_BUILD:-$root/build-windows}"
P="$READER_HOST_PREFIX"
export PATH="$P/bin:$PATH"
export LD_LIBRARY_PATH="$READER_QT_HOST/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}")
"$here/private-data.sh"
# Microsoft's headers under the names the sources call them by (letter case).
python3 "$here/windows-case.py" "$root" "$READER_XWIN" "$READER_WINDOWS_HOME/case-links" >/dev/null
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/windows-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$READER_WINDOWS_DEPS" \
    -DQT_HOST_PATH="$READER_QT_HOST" \
    -DQT_HOST_PATH_CMAKE_DIR="$READER_QT_HOST/lib/cmake" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/bin/ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$P/bin/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_TESTS=OFF -DUSE_VTUNE=OFF -DLTO_PCSX2_CORE=OFF \
    ${READER_WINDOWS_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
exec "${limit[@]}" ninja -C "$build" "$@"
