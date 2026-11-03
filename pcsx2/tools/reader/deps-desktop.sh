#!/usr/bin/env bash
# Build into the toolchain folder (READER_HOST_PREFIX) the libraries the desktop build needs
# that the unpacked packages do not bring in the shape the emulator's build looks for:
# libbacktrace, KDDockWidgets, PlutoVG, PlutoSVG, RapidYAML and the emulator's patched shaderc.
# Versions, checksums and build options are those of the emulator's own script for Linux
# (.github/workflows/scripts/linux/build-dependencies-qt.sh), which is where they are read
# from. Once per machine; nothing needs root.
#   tools/reader/deps-desktop.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
upstream="$root/.github/workflows/scripts/linux/build-dependencies-qt.sh"
P="$READER_HOST_PREFIX"
export PATH="$P/bin:$PATH"
work="$READER_HOME/src"
mkdir -p "$work"
cd "$work"
limit=("$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}")
version() { sed -n "s/^$1=//p" "$upstream" | head -1; }
LIBBACKTRACE="$(version LIBBACKTRACE)"; KDDOCKWIDGETS="$(version KDDOCKWIDGETS)"
PLUTOVG="$(version PLUTOVG)"; PLUTOSVG="$(version PLUTOSVG)"; RAPIDYAML="$(version RAPIDYAML)"
SHADERC="$(version SHADERC)"; SHADERC_GLSLANG="$(version SHADERC_GLSLANG)"
SHADERC_SPIRVHEADERS="$(version SHADERC_SPIRVHEADERS)"; SHADERC_SPIRVTOOLS="$(version SHADERC_SPIRVTOOLS)"
fetch() { # URL FILE: download once, and hold the file to the checksum the emulator's script names
  [ -f "$2" ] || curl -sfL -o "$2" "$1"
  local want
  want="$(grep -F "  ${2/$3/\$$4}" "$upstream" | awk '{print $1}' | head -1)"
  [ -n "$want" ] || { echo "no checksum for $2 in $upstream" >&2; exit 1; }
  echo "$want  $2" | sha256sum -c --quiet
}
common=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
        -DCMAKE_PREFIX_PATH="$P" -DCMAKE_INSTALL_PREFIX="$P" -G Ninja)
build() { "${limit[@]}" cmake "${common[@]}" "$@" -B build >/dev/null; "${limit[@]}" ninja -C build -j "$READER_JOBS" install >/dev/null; }

if [ ! -f "$P/lib/libbacktrace.a" ]; then
  fetch "https://github.com/ianlancetaylor/libbacktrace/archive/$LIBBACKTRACE/libbacktrace-$LIBBACKTRACE.tar.gz" "libbacktrace-$LIBBACKTRACE.tar.gz" "$LIBBACKTRACE" LIBBACKTRACE
  rm -rf "libbacktrace-$LIBBACKTRACE"; tar xf "libbacktrace-$LIBBACKTRACE.tar.gz"
  (cd "libbacktrace-$LIBBACKTRACE" && CC=clang "${limit[@]}" ./configure --prefix="$P" >/dev/null && "${limit[@]}" make -j "$READER_JOBS" >/dev/null && make install >/dev/null)
fi
if [ ! -d "$P/lib/cmake/KDDockWidgets-qt6" ]; then
  fetch "https://github.com/KDAB/KDDockWidgets/archive/v$KDDOCKWIDGETS/KDDockWidgets-$KDDOCKWIDGETS.tar.gz" "KDDockWidgets-$KDDOCKWIDGETS.tar.gz" "$KDDOCKWIDGETS" KDDOCKWIDGETS
  rm -rf "KDDockWidgets-$KDDOCKWIDGETS"; tar xf "KDDockWidgets-$KDDOCKWIDGETS.tar.gz"
  (cd "KDDockWidgets-$KDDOCKWIDGETS" && build -DKDDockWidgets_QT6=true -DKDDockWidgets_EXAMPLES=false -DKDDockWidgets_FRONTENDS=qtwidgets)
fi
if [ ! -d "$P/lib/cmake/plutovg" ]; then
  fetch "https://github.com/sammycage/plutovg/archive/v$PLUTOVG/plutovg-$PLUTOVG.tar.gz" "plutovg-$PLUTOVG.tar.gz" "$PLUTOVG" PLUTOVG
  rm -rf "plutovg-$PLUTOVG"; tar xf "plutovg-$PLUTOVG.tar.gz"
  (cd "plutovg-$PLUTOVG" && build -DBUILD_SHARED_LIBS=ON -DPLUTOVG_BUILD_EXAMPLES=OFF)
fi
if [ ! -d "$P/lib/cmake/plutosvg" ]; then
  fetch "https://github.com/sammycage/plutosvg/archive/v$PLUTOSVG/plutosvg-$PLUTOSVG.tar.gz" "plutosvg-$PLUTOSVG.tar.gz" "$PLUTOSVG" PLUTOSVG
  rm -rf "plutosvg-$PLUTOSVG"; tar xf "plutosvg-$PLUTOSVG.tar.gz"
  (cd "plutosvg-$PLUTOSVG" && build -DBUILD_SHARED_LIBS=ON -DPLUTOSVG_ENABLE_FREETYPE=OFF -DPLUTOSVG_BUILD_EXAMPLES=OFF)
fi
if [ ! -d "$P/lib/cmake/ryml" ]; then
  fetch "https://github.com/biojppm/rapidyaml/releases/download/v$RAPIDYAML/rapidyaml-$RAPIDYAML-src.tgz" "rapidyaml-$RAPIDYAML-src.tgz" "$RAPIDYAML" RAPIDYAML
  rm -rf "rapidyaml-$RAPIDYAML-src"; tar xf "rapidyaml-$RAPIDYAML-src.tgz"
  (cd "rapidyaml-$RAPIDYAML-src" && build -DBUILD_SHARED_LIBS=ON)
fi
if [ ! -f "$P/lib/libshaderc_shared.so" ]; then
  fetch "https://github.com/google/shaderc/archive/v$SHADERC/shaderc-$SHADERC.tar.gz" "shaderc-$SHADERC.tar.gz" "$SHADERC" SHADERC
  fetch "https://github.com/KhronosGroup/glslang/archive/$SHADERC_GLSLANG/shaderc-glslang-$SHADERC_GLSLANG.tar.gz" "shaderc-glslang-$SHADERC_GLSLANG.tar.gz" "$SHADERC_GLSLANG" SHADERC_GLSLANG
  fetch "https://github.com/KhronosGroup/SPIRV-Headers/archive/$SHADERC_SPIRVHEADERS/shaderc-spirv-headers-$SHADERC_SPIRVHEADERS.tar.gz" "shaderc-spirv-headers-$SHADERC_SPIRVHEADERS.tar.gz" "$SHADERC_SPIRVHEADERS" SHADERC_SPIRVHEADERS
  fetch "https://github.com/KhronosGroup/SPIRV-Tools/archive/$SHADERC_SPIRVTOOLS/shaderc-spirv-tools-$SHADERC_SPIRVTOOLS.tar.gz" "shaderc-spirv-tools-$SHADERC_SPIRVTOOLS.tar.gz" "$SHADERC_SPIRVTOOLS" SHADERC_SPIRVTOOLS
  rm -rf "shaderc-$SHADERC"; tar xf "shaderc-$SHADERC.tar.gz"
  (cd "shaderc-$SHADERC/third_party" &&
    tar xf "../../shaderc-glslang-$SHADERC_GLSLANG.tar.gz" && mv "glslang-$SHADERC_GLSLANG" glslang &&
    tar xf "../../shaderc-spirv-headers-$SHADERC_SPIRVHEADERS.tar.gz" && mv "SPIRV-Headers-$SHADERC_SPIRVHEADERS" spirv-headers &&
    tar xf "../../shaderc-spirv-tools-$SHADERC_SPIRVTOOLS.tar.gz" && mv "SPIRV-Tools-$SHADERC_SPIRVTOOLS" spirv-tools &&
    cd .. && patch -s -p1 < "$root/.github/workflows/scripts/common/shaderc-changes.patch" &&
    build -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON)
fi
echo "desktop dependencies are in $P"
