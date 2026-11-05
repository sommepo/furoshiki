#!/usr/bin/env bash
# Make a folder of the Windows build that runs on Windows: the program, the libraries it
# needs (Qt, SDL 2, the compiler's runtime), Qt's plugins and the cores built beside it
# (cores/: each fork's build-core-windows: Station's, Flycast's, PPSSPP's, Azahar's, Eden's, PCSX2's, with PPSSPP's
# assets in cores/ppsspp, where its core looks for them, and PCSX2's libraries and resources);
# and a zip of it.
#   tools/package-windows.sh
# Output: build-windows/package/Furoshiki/ and build-windows/package/Furoshiki-windows.zip
# (READER_HOME_WINDOWS_BUILD names another build folder; READER_HOME_FORKS the folder the
# forks' core builds are in, this checkout's by default: a worktree's has none).
# A published build (READER_HOME_PUBLIC=1, build-windows-public, HOME_PUBLIC): Eden's core is
# left out whatever is built, the licences, notices and the source offer go in, and the
# package is checked (tools/release-check.py) before it is zipped:
# build-windows-public/package/Furoshiki/ and Furoshiki-windows.zip.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/.." && pwd)"
if [ "${READER_HOME_PUBLIC:-${HOME_PUBLIC:-0}}" = 1 ]; then
  build="${READER_HOME_WINDOWS_BUILD:-$root/build-windows-public}"
else
  build="${READER_HOME_WINDOWS_BUILD:-$root/build-windows}"
fi
forks="${READER_HOME_FORKS:-$root/..}"
exe="$build/reader-home.exe"
# Whether the program was built as a published build is what its build says.
public=0
grep -qx "HOME_PUBLIC:BOOL=ON" "$build/CMakeCache.txt" 2>/dev/null && public=1
[ "${READER_HOME_PUBLIC:-${HOME_PUBLIC:-0}}" = 1 ] && [ "$public" != 1 ] &&
  { echo "$build is not a published build (HOME_PUBLIC): tools/build-windows.sh with READER_HOME_PUBLIC=1" >&2; exit 1; }
name=Furoshiki
out="$build/package/$name"
[ -f "$READER_SDL2_WINDOWS/bin/SDL2.dll" ] ||
  READER_SDL2_WINDOWS="$HOME/.local/share/station-reader/sdl2-mingw/SDL2-2.32.10/x86_64-w64-mingw32"
objdump="$READER_MINGW/bin/x86_64-w64-mingw32-objdump"
strip="$READER_MINGW/bin/x86_64-w64-mingw32-strip"
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[ -f "$exe" ] || { echo "no $exe: run tools/build-windows.sh first" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out"
# Its name in the package is the program's own; reader-home.exe is the build's (its CMake target).
install -m 755 "$exe" "$out/Furoshiki.exe"
"$strip" --strip-unneeded "$out/Furoshiki.exe"
# The compiler's runtime libraries must be the ones of the compiler the program was built
# with: Qt ships older copies of the same names.
gcc_dir="$(find "$READER_MINGW/lib/gcc/x86_64-w64-mingw32" -maxdepth 1 -name '*-posix' | sort -V | tail -1)"
[ -n "$gcc_dir" ] || { echo "no MinGW compiler under $READER_MINGW" >&2; exit 1; }
runtime=("$gcc_dir" "$READER_MINGW/x86_64-w64-mingw32/lib")
search=("${runtime[@]}" "$READER_QT_WINDOWS/bin" "$READER_SDL2_WINDOWS/bin")
find_dll() {
  local name="$1" dir found
  for dir in "${search[@]}"; do
    found="$(find "$dir" -maxdepth 1 -iname "$name" -print -quit 2>/dev/null)"
    if [ -n "$found" ]; then echo "$found"; return 0; fi
  done
  return 1
}
copy_with_deps() {
  local file="$1" dll path dir
  while read -r dll; do
    [ -n "$dll" ] || continue
    [ -e "$out/$dll" ] && continue
    if path="$(find_dll "$dll")"; then
      install -m 644 "$path" "$out/$(basename "$path")"
      for dir in "${runtime[@]}"; do
        [ "$(dirname "$path")" = "$dir" ] && "$strip" --strip-unneeded "$out/$(basename "$path")"
      done
      copy_with_deps "$path"
    fi
  done < <("$objdump" -p "$file" | awk '/DLL Name:/ {print $3}')
}
copy_with_deps "$exe"
# WebView2's loader, which the popup page loads when it is first wanted (not an import).
install -m 644 "$build/WebView2Loader.dll" "$out/WebView2Loader.dll"
# tls/: Windows' own TLS for HTTPS (dictionary audio, updates); without it every HTTPS request fails.
for plugin in platforms/qwindows.dll styles/qmodernwindowsstyle.dll \
              imageformats/qjpeg.dll imageformats/qgif.dll imageformats/qico.dll tls/qschannelbackend.dll; do
  mkdir -p "$out/$(dirname "$plugin")"
  install -m 644 "$READER_QT_WINDOWS/plugins/$plugin" "$out/$plugin"
  copy_with_deps "$READER_QT_WINDOWS/plugins/$plugin"
done
# The cores the program loads (each a DLL that keeps its libraries inside).
# Eden's core is the maintainer's alone: never in a published package.
for core in "$forks"/station/build-core-windows/bin/reader_core_*.dll \
            "$forks"/flycast/build-core-windows/reader_core_*.dll \
            "$forks"/ppsspp/build-core-windows/reader_core_*.dll \
            "$forks"/azahar/build-core-windows/bin/Release/reader_core_*.dll \
            "$forks"/eden/build-core-windows/bin/reader_core_*.dll; do
  [ -f "$core" ] || continue
  # A build folder's stripped copy of a core (PPSSPP's) is not a core of its own.
  [[ "$(basename "$core")" == *.stripped.dll ]] && continue
  if [ "$public" = 1 ] && [[ "$(basename "$core")" == reader_core_eden* ]]; then
    echo "a published package: $(basename "$core") left out"
    continue
  fi
  mkdir -p "$out/cores"
  install -m 644 "$core" "$out/cores/$(basename "$core")"
  "$strip" --strip-unneeded "$out/cores/$(basename "$core")"
  copy_with_deps "$core"
done
# PCSX2's core is built with clang-cl against Microsoft's C runtime (the Visual C++
# Redistributable must be installed, as for the PCSX2 Reader app), and takes the libraries of
# PCSX2's own package of Windows libraries (SDL 3, zstd, libpng, ...): they go beside it in
# cores/, where Windows looks for a core's libraries (the program loads a core by its full
# path), under names nothing else in the program uses; with Vulkan's shader compiler, which
# the core loads from there, and PCSX2's resources (shaders, fonts, the game index) in
# cores/pcsx2/resources. Nothing of Qt: the core has no Qt of its own.
# (READER_PCSX2_CORE_WINDOWS names a DLL built in another checkout.)
pcsx2_core="${READER_PCSX2_CORE_WINDOWS:-$forks/pcsx2/build-core-windows/reader/core_library/reader_core_pcsx2.dll}"
if [ -f "$pcsx2_core" ]; then
  # PCSX2's own paths, read without the Flycast fork's environment this script has (both name READER_HOME).
  pcsx2_deps="${READER_PCSX2_WINDOWS_DEPS:-$(env -i HOME="$HOME" bash -c '. "$1" && echo "$READER_WINDOWS_DEPS"' _ "$forks/pcsx2/tools/reader/env.sh")}/bin"
  mkdir -p "$out/cores"
  install -m 644 "$pcsx2_core" "$out/cores/reader_core_pcsx2.dll"
  copy_pcsx2_deps() {
    local dll path
    while read -r dll; do
      [ -n "$dll" ] || continue
      path="$(find "$pcsx2_deps" -maxdepth 1 -iname "$dll" -print -quit)"
      [ -n "$path" ] || continue
      [ -e "$out/cores/$(basename "$path")" ] && continue
      install -m 644 "$path" "$out/cores/$(basename "$path")"
      copy_pcsx2_deps "$path"
    done < <("$objdump" -p "$1" | awk '/DLL Name:/ {print $3}')
  }
  copy_pcsx2_deps "$pcsx2_core"
  install -m 644 "$pcsx2_deps/shaderc_shared.dll" "$out/cores/shaderc_shared.dll"
  mkdir -p "$out/cores/pcsx2"
  cp -r "$forks/pcsx2/bin/resources" "$out/cores/pcsx2/resources"
fi
# Vita3K's core is built with clang-cl too (the same Redistributable), with OpenSSL's two DLLs
# beside it and its data and built-in shaders in cores/vita3k.
vita3k_core="$forks/vita3k/build-core-windows/bin/reader_core_vita3k.dll"
if [ -f "$vita3k_core" ]; then
  mkdir -p "$out/cores"
  install -m 644 "$vita3k_core" "$(dirname "$vita3k_core")"/libssl-4-x64.dll \
    "$(dirname "$vita3k_core")"/libcrypto-4-x64.dll "$out/cores/"
  cp -r "$(dirname "$vita3k_core")/vita3k" "$out/cores/vita3k"
fi
# PPSSPP's assets beside its core (its fonts, the PSP's flash0, compat.ini; not the profiles,
# which the program carries).
if [ -f "$out/cores/reader_core_ppsspp.dll" ]; then
  cp -r "$forks/ppsspp/assets" "$out/cores/ppsspp"
  rm -rf "$out/cores/ppsspp/debugger" "$out/cores/ppsspp/reader"
fi
"$READER_HOST_PREFIX/bin/python3" "$here/pe_check.py" "$out"
# PCSX2's core takes its libraries from cores/, beside it: checked there.
[ -f "$out/cores/reader_core_pcsx2.dll" ] && "$READER_HOST_PREFIX/bin/python3" "$here/pe_check.py" "$out/cores"
# A published package: the licences (publish/NOTICES.md says which is which), the notices and
# where its source is (FUROSHIKI_VERSION and FUROSHIKI_COMMIT, which the release script sets).
if [ "$public" = 1 ]; then
  src="$root/.."
  licenses="$out/licenses"
  mkdir -p "$licenses"
  install -m 644 "$root/COPYING" "$out/COPYING.txt"
  install -m 644 "$root/publish/NOTICES.md" "$out/NOTICES.txt"
  sed -e "s/@VERSION@/${FUROSHIKI_VERSION:-(a build of the repository)}/g" \
      -e "s/@COMMIT@/${FUROSHIKI_COMMIT:-(not a release build)}/g" \
      "$root/publish/SOURCE.txt" > "$out/SOURCE.txt"
  lic() { [ -f "$2" ] || { echo "no licence file $2" >&2; exit 1; }; install -m 644 "$2" "$licenses/$1"; }
  lic LGPL-3.0.txt "$root/publish/licenses/LGPL-3.0.txt"
  lic OFL-DotGothic16.txt "$root/fonts/OFL-DotGothic16.txt"
  lic SDL2-LICENSE.txt "$(dirname "$READER_SDL2_WINDOWS")/LICENSE.txt"
  lic QuaZip-COPYING.txt "$build/_deps/reader_quazip-src/COPYING"
  lic Opus-COPYING.txt "$build/_deps/reader_opus-src/COPYING"
  lic DuckStation-LICENSE.txt "$src/station/LICENSE"
  lic PCSX2-COPYING.GPLv3.txt "$src/pcsx2/COPYING.GPLv3"
  lic PCSX2-ThirdPartyLicenses.html "$src/pcsx2/bin/docs/ThirdPartyLicenses.html"
  lic PPSSPP-LICENSE.txt "$src/ppsspp/LICENSE.TXT"
  lic Flycast-LICENSE.txt "$src/flycast/LICENSE"
  lic Azahar-license.txt "$src/azahar/license.txt"
  if [ -f "$out/cores/reader_core_vita3k.dll" ]; then
    lic Vita3K-COPYING.txt "$src/vita3k/COPYING.txt"
    lic OpenSSL-LICENSE.txt "$forks/vita3k/build-core-windows/external/openssl/LICENSE.txt"
  fi
  "$READER_HOST_PREFIX/bin/python3" "$here/release-check.py" --folder "$out"
fi
(cd "$out/.." && rm -f "$name-windows.zip" && "$READER_HOST_PREFIX/bin/python3" -m zipfile -c "$name-windows.zip" "$name")
[ "$public" = 1 ] && "$READER_HOST_PREFIX/bin/python3" "$here/release-check.py" "$out/../$name-windows.zip"
du -sh "$out" "$out/../$name-windows.zip"
