#!/usr/bin/env bash
# Configure and build PPSSPP as a core of Reader Home (Linux): build-core/lib/reader_core_ppsspp.so,
# the module Reader Home loads (reader/core_library, cores/reader_core.h at the top of the
# repository): PPSSPP's libretro build, with the core's own files in it.
#   tools/reader/build-core.sh [ninja targets...]
# Reader Home finds it beside its program, in its "cores" folder, or in READER_HOME_CORES.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_BUILD_DIR:-$root/build-core}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
# Qt's own tools (moc, rcc) run during the build and need the libraries Qt was built against.
export LD_LIBRARY_PATH="$READER_QT_LINUX/lib:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
link="-static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined -Wl,-rpath,$READER_MESA_PREFIX/lib -Wl,-rpath,$READER_QT_LINUX/lib -Wl,-rpath,$READER_QT_SYSLIBS -Wl,-rpath-link,$READER_QT_SYSLIBS -Wl,-rpath-link,$READER_MESA_PREFIX/lib"
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$P/x86_64-conda-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="$P/x86_64-conda-linux-gnu-g++" \
    -DCMAKE_AR="$P/x86_64-conda-linux-gnu-gcc-ar" \
    -DCMAKE_RANLIB="$P/x86_64-conda-linux-gnu-gcc-ranlib" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DUSE_CCACHE=OFF -DCMAKE_C_FLAGS=-DGLEW_NO_GLU -DCMAKE_CXX_FLAGS=-DGLEW_NO_GLU \
    -DCMAKE_PREFIX_PATH="$READER_QT_LINUX;$READER_MESA_PREFIX" \
    -DQt6_DIR="$READER_QT_LINUX/lib/cmake/Qt6" \
    -DCMAKE_SHARED_LINKER_FLAGS="$link" -DCMAKE_MODULE_LINKER_FLAGS="$link" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DLIBRETRO=ON -DREADER_CORE=ON -DUSING_QT_UI=OFF -DHEADLESS=OFF -DUNITTEST=OFF -DATLAS_TOOL=OFF \
    -DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
    -DUSE_SYSTEM_LIBPNG=OFF -DUSE_SYSTEM_LIBSDL2=OFF \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
[ $# -gt 0 ] || set -- ppsspp_libretro
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
