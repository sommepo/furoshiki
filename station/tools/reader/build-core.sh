#!/usr/bin/env bash
# Configure and build Station as a core of Reader Home (Linux): build-core/bin/reader_core_station.so,
# the module Reader Home loads (reader/core_library, cores/reader_core.h at the top of the
# repository). No frontend, no Qt: the program has those.
#   tools/reader/build-core.sh [ninja targets...]
# Reader Home finds it beside its program, in its "cores" folder, or in READER_HOME_CORES.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_BUILD_DIR:-$root/build-core}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
export PKG_CONFIG_PATH="$READER_DEPS_PREFIX/lib/pkgconfig:$READER_DEPS_PREFIX/share/pkgconfig:$READER_MESA_PREFIX/lib/pkgconfig:$READER_MESA_PREFIX/share/pkgconfig"
export C_INCLUDE_PATH="$READER_DEPS_PREFIX/include" CPLUS_INCLUDE_PATH="$READER_DEPS_PREFIX/include" LIBRARY_PATH="$READER_DEPS_PREFIX/lib"
# Its own C++ library inside it, hidden like the rest: the program has its own.
link="-static-libstdc++ -static-libgcc -Wl,-rpath,\$ORIGIN/lib -Wl,-rpath,$READER_MESA_PREFIX/lib -Wl,-rpath,$READER_DEPS_PREFIX/lib -Wl,-rpath-link,$READER_MESA_PREFIX/lib -Wl,-rpath-link,$READER_DEPS_PREFIX/lib"
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
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
    -DPKG_CONFIG_EXECUTABLE="$P/pkg-config" \
    -DCMAKE_PREFIX_PATH="$READER_DEPS_PREFIX;$READER_MESA_PREFIX" \
    -DCMAKE_SHARED_LINKER_FLAGS="$link" -DCMAKE_MODULE_LINKER_FLAGS="$link" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DREADER_CORE=ON -DBUILD_QT_FRONTEND=OFF -DBUILD_NOGUI_FRONTEND=OFF -DUSE_SDL2=OFF \
    -DENABLE_DISCORD_PRESENCE=OFF -DENABLE_CHEEVOS=OFF -DUSE_WAYLAND=OFF -DUSE_EVDEV=OFF \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
exec "${limit[@]}" "$P/ninja" -C "$build" reader_core_station "$@"
