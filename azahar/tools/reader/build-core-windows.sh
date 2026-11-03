#!/usr/bin/env bash
# Configure and build Azahar as a core of Reader Home for Windows (x86-64), cross-compiled with
# MinGW: build-core-windows/bin/Release/reader_core_azahar.dll (src/reader_core_library). It
# cannot be run on this machine (no Wine here).
#   tools/reader/build-core-windows.sh [ninja targets...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_WINDOWS_BUILD:-$root/build-core-windows}"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}")
# The compiler was unpacked from its packages, not installed: it finds the libraries it was
# linked against here. Qt's own build tools (moc, uic, rcc) are those of the Qt for this
# machine, which is the same version.
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# -lcrypt32: the emulator's HTTP library reads Windows' certificate store, and the emulator's
# build names that library for Microsoft's compiler only.
if [ ! -f "$build/build.ninja" ]; then
  "${limit[@]}" "$READER_CMAKE" -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/mingw-toolchain.cmake" \
    -DCMAKE_MAKE_PROGRAM="$P/ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD_LIBRARIES="-lcrypt32" \
    -DREADER_PACKAGES_WINDOWS=ON \
    -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc -static" \
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_LIBRETRO=ON -DREADER_CORE=ON -DREADER_SAVE_SYNC=OFF -DENABLE_QT=OFF \
    -DENABLE_SDL2=OFF -DENABLE_TESTS=OFF -DENABLE_READER_HARNESS=OFF \
    -DENABLE_ROOM=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF \
    -DENABLE_LIBUSB=OFF -DENABLE_OPENGL=ON -DENABLE_VULKAN=OFF \
    -DENABLE_SOFTWARE_RENDERER=OFF -DENABLE_LTO=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
    ${READER_CORE_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
mkdir -p "$build"
export TMPDIR="${TMPDIR_CORE:-$build}"
[ $# -gt 0 ] || set -- citra_libretro
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
