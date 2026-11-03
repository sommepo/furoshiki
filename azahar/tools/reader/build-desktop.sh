#!/usr/bin/env bash
# Configure and build the desktop app (Qt) for this machine: what the desktop reader layer is
# developed and tried against, without a display (tools/reader/desktop.sh). The Windows build
# is build-windows.sh.
#   tools/reader/build-desktop.sh [ninja targets...]
# Output: build-desktop/bin/Release/azahar
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="$root/build-desktop"
P="$READER_HOST_PREFIX/bin"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}")
# Qt's own build tools (moc, uic, rcc) want the libraries Qt was given.
export LD_LIBRARY_PATH="$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The emulator uses one of Qt's private headers on Linux, and this Qt package's build files
# leave the folder they are in out of the include path.
qtv="$(basename "$(ls -d "$READER_QT_LINUX"/include/QtGui/6.*)")"
export CPLUS_INCLUDE_PATH="$READER_QT_LINUX/include/QtGui/$qtv:$READER_QT_LINUX/include/QtGui/$qtv/QtGui${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
# Qt's libraries were built on a newer system than the compiler's own sysroot: what they refer
# to in the C library is there when the program runs, and is not looked for when it is linked.
link="-Wl,--allow-shlib-undefined -Wl,-rpath,$READER_VULKAN_PREFIX/lib -Wl,-rpath,$READER_QT_LINUX/lib -Wl,-rpath,$READER_QT_SYSLIBS -Wl,-rpath-link,$READER_QT_SYSLIBS"
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
    -DCMAKE_EXE_LINKER_FLAGS="$link" \
    -DCMAKE_SHARED_LINKER_FLAGS="$link" \
    -DCMAKE_C_FLAGS="-isystem $READER_EDEN_TOOLCHAINS/x11-include" \
    -DCMAKE_CXX_FLAGS="-isystem $READER_EDEN_TOOLCHAINS/x11-include" \
    -DCMAKE_JOB_POOLS="compile=${READER_JOBS};link=2" \
    -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link \
    -DENABLE_QT=ON -DUSE_SYSTEM_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_QT_UPDATE_CHECKER=OFF \
    -DQt6_DIR="$READER_QT_LINUX/lib/cmake/Qt6" -DCMAKE_PREFIX_PATH="$READER_QT_LINUX" \
    -DPKG_CONFIG_EXECUTABLE="$P/pkg-config" \
    -DOPENGL_INCLUDE_DIR="$READER_QT_SYSLIBS/../../include" \
    -DOPENGL_opengl_LIBRARY="$READER_QT_SYSLIBS/libOpenGL.so" \
    -DOPENGL_glx_LIBRARY="$READER_QT_SYSLIBS/libGLX.so" \
    -DOPENGL_egl_LIBRARY="$READER_QT_SYSLIBS/libEGL.so" \
    -DOPENGL_gl_LIBRARY="$READER_QT_SYSLIBS/libGL.so" \
    -DOPENGL_GLX_INCLUDE_DIR="$READER_QT_SYSLIBS/../../include" \
    -DOPENGL_EGL_INCLUDE_DIR="$READER_QT_SYSLIBS/../../include" \
    -DENABLE_SDL2=ON -DENABLE_TESTS=OFF -DENABLE_READER_HARNESS=OFF \
    -DENABLE_READER_DESKTOP_TESTS=ON \
    -DENABLE_ROOM=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF \
    -DENABLE_LIBUSB=OFF -DENABLE_OPENGL=OFF -DENABLE_VULKAN=ON \
    -DENABLE_SOFTWARE_RENDERER=ON -DENABLE_LTO=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
    ${READER_DESKTOP_CMAKE_ARGS:-}
fi
if [ "${1:-}" = "--configure-only" ]; then exit 0; fi
exec "${limit[@]}" "$P/ninja" -C "$build" "$@"
