#!/usr/bin/env bash
# Configure and build Azahar as a core of Reader Home (Linux): build-core/bin/Release/
# reader_core_azahar.so, the module Reader Home loads (src/reader_core_library,
# cores/reader_core.h at the top of the repository): Azahar's libretro build, OpenGL only,
# with the core's own files in it.
#   tools/reader/build-core.sh [ninja targets...]
# Reader Home finds it beside its program, in its "cores" folder, or in READER_HOME_CORES.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_BUILD_DIR:-$root/build-core}"
P="$READER_HOST_PREFIX/bin"
# X11 and EGL for drawing into the program's window: the PPSSPP Reader fork's Mesa prefix.
READER_MESA_PREFIX="${READER_MESA_PREFIX:-$HOME/.local/share/ppsspp-reader/gl}"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}")
# Qt's own build tools (moc, uic, rcc) want the libraries Qt was given.
export LD_LIBRARY_PATH="$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The emulator uses one of Qt's private headers on Linux, and this Qt package's build files
# leave the folder they are in out of the include path.
qtv="$(basename "$(ls -d "$READER_QT_LINUX"/include/QtGui/6.*)")"
export CPLUS_INCLUDE_PATH="$READER_QT_LINUX/include/QtGui/$qtv:$READER_QT_LINUX/include/QtGui/$qtv/QtGui${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
# Qt's libraries were built on a newer system than the compiler's own sysroot: what they refer
# to in the C library is there when the program runs, and is not looked for when it is linked.
link="-static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined -Wl,-rpath,$READER_VULKAN_PREFIX/lib -Wl,-rpath,$READER_QT_LINUX/lib -Wl,-rpath,$READER_QT_SYSLIBS -Wl,-rpath-link,$READER_QT_SYSLIBS"
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
    -DENABLE_LIBRETRO=ON -DREADER_CORE=ON -DREADER_SAVE_SYNC=OFF \
    -DENABLE_QT=OFF -DCMAKE_PREFIX_PATH="$READER_MESA_PREFIX" \
    -DPKG_CONFIG_EXECUTABLE="$P/pkg-config" \
    -DOPENGL_INCLUDE_DIR="$READER_QT_SYSLIBS/../../include" \
    -DOPENGL_opengl_LIBRARY="$READER_QT_SYSLIBS/libOpenGL.so" \
    -DOPENGL_glx_LIBRARY="$READER_QT_SYSLIBS/libGLX.so" \
    -DOPENGL_egl_LIBRARY="$READER_QT_SYSLIBS/libEGL.so" \
    -DOPENGL_gl_LIBRARY="$READER_QT_SYSLIBS/libGL.so" \
    -DOPENGL_GLX_INCLUDE_DIR="$READER_QT_SYSLIBS/../../include" \
    -DOPENGL_EGL_INCLUDE_DIR="$READER_QT_SYSLIBS/../../include" \
    -DENABLE_SDL2=OFF -DENABLE_TESTS=OFF -DENABLE_READER_HARNESS=OFF \
    -DENABLE_READER_DESKTOP_TESTS=OFF \
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
