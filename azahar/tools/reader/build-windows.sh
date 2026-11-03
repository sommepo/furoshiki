#!/usr/bin/env bash
# Configure and build the desktop app for Windows (x86-64), cross-compiled with MinGW. It
# cannot be run on this machine (no Wine here): it is tried on a Windows computer.
#   tools/reader/build-windows.sh [ninja targets...]
# Output: build-windows/bin/Release/azahar.exe; tools/reader/package-windows.sh makes a folder
# of it that runs on Windows.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="$root/build-windows"
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
    -DCMAKE_C_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$P/ccache" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_QT=ON -DUSE_SYSTEM_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_QT_UPDATE_CHECKER=OFF \
    -DQt6_DIR="$READER_QT_WINDOWS/lib/cmake/Qt6" -DCMAKE_PREFIX_PATH="$READER_QT_WINDOWS" \
    -DQT_HOST_PATH="$READER_QT_LINUX" \
    -DQT_HOST_PATH_CMAKE_DIR="$READER_QT_LINUX/lib/cmake" \
    -DENABLE_SDL2=ON -DENABLE_TESTS=OFF -DENABLE_READER_HARNESS=OFF \
    -DENABLE_ROOM=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF \
    -DENABLE_LIBUSB=OFF -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON \
    -DENABLE_SOFTWARE_RENDERER=ON -DENABLE_LTO=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
    ${READER_WINDOWS_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
