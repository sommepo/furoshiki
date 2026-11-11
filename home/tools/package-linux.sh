#!/usr/bin/env bash
# Make a folder of the Linux build that runs on another Linux computer (the Batocera box among
# them): the program, the libraries it needs that such a computer may not have (Qt, SDL 2, the
# compiler's C++ runtime, and what those need), Qt's plugins, the cores (cores/: each fork's
# tools/reader/build-core.sh; PPSSPP's assets in cores/ppsspp and PCSX2's resources in
# cores/pcsx2/resources, where those cores look), a start script; and a tarball of it.
#   tools/package-linux.sh
# Output: build-desktop/package/Furoshiki/, build-desktop/package/Furoshiki-linux.tar.gz and its
# checksum Furoshiki-linux.tar.gz.sha256 (sha256sum's form; the Batocera box's furoshiki-update
# checks a download against it). Until 2026-10-28 the folder was ReaderHome/ and the tarball
# ReaderHome-linux.tar.gz; the program inside keeps its name (reader-home), as installs and
# launchers call it so.
#   Furoshiki/reader-home      the start script (arguments as the program's: --play FILE, --window)
#   Furoshiki/reader-home.bin  the program
#   Furoshiki/lib, plugins     its libraries and Qt's plugins
#   Furoshiki/cores            the cores (their libraries are in lib too)
#   Furoshiki/VERSION          the commit and the day it was made
# A tarball, not an AppImage: an AppImage needs FUSE where it runs (or unpacks itself into /tmp
# at every start), and it could not be added to: the folder takes cores, as the Windows zip's
# does, and the box keeps it in /userdata unpacked.
# What it leaves to the computer: the C library, the graphics drivers and their libraries (GL,
# EGL, Vulkan, DRM, VA), X11 and xcb (but xcb's utility libraries), the sound servers' libraries, D-Bus, fontconfig and
# FreeType: those must be the computer's own. Every library it takes is checked to ask no more
# of the C library than glibc 2.40 (READER_HOME_GLIBC_MAX; Batocera 42 has 2.40).
# READER_HOME_LINUX_BUILD names another build folder; READER_HOME_FORKS another checkout whose
# forks' core builds are taken (default: this one); READER_HOME_PACKAGE_CORES the cores to take
# (default "station flycast ppsspp azahar pcsx2 eden", and vita3k once its core is built; a package
# for others leaves eden out).
# PCSX2's core is taken from pcsx2/build-core-portable (READER_PCSX2_CORE names another): its
# usual build is made for this machine's processor (-march=native) and stops on another one
# with "illegal instruction". Build it so with
#   READER_CORE_BUILD_DIR=$PWD/build-core-portable READER_CORE_CMAKE_ARGS=-DDISABLE_ADVANCE_SIMD=ON tools/reader/build-core.sh
# in the PCSX2 fork (upstream's own setting for builds that go to other computers).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# READER_HOME_WEB=1: the build with Qt WebEngine (tools/build-desktop.sh's build-desktop-web, with
# the official Qt that has it), so the dictionary popup is the web page, its themes and the
# user's CSS, as on Windows; the package then carries WebEngine (its process, its resources).
web="${READER_HOME_WEB:-0}"
if [ "$web" = 1 ]; then
  export READER_QT_LINUX="${READER_HOME_WEB_QT:-$HOME/.local/share/furoshiki-qt/6.9.3/gcc_64}"
fi
. "$here/env.sh"
root="$(cd "$here/.." && pwd)"
build="${READER_HOME_LINUX_BUILD:-$root/build-desktop$([ "$web" = 1 ] && echo -web || true)}"
forks="${READER_HOME_FORKS:-$root/..}"
cores_wanted="${READER_HOME_PACKAGE_CORES:-station flycast ppsspp azahar pcsx2$([ -d "$forks/eden" ] && echo " eden" || true)$([ -f "$forks/vita3k/build-core/bin/reader_core_vita3k.so" ] && echo " vita3k" || true)}"
glibc_max="${READER_HOME_GLIBC_MAX:-2.40}"
out="$build/package/Furoshiki"
strip="$READER_HOST_PREFIX/bin/x86_64-conda-linux-gnu-strip"
python="$READER_HOST_PREFIX/bin/python3"
sdl2="${HOME_SDL2_PREFIX:-$HOME/.local/share/station-reader/deps}"
weston="${READER_WESTON_PREFIX:-$HOME/.local/share/ppsspp-reader/weston}"
[ -x "$build/reader-home" ] || { echo "no $build/reader-home: run tools/build-desktop.sh first" >&2; exit 1; }
# Where the libraries are looked for while they are gathered: as tools/desktop.sh runs the build.
export LD_LIBRARY_PATH="$sdl2/lib:$READER_QT_LINUX/lib:$READER_QT_SYSLIBS:$weston/lib"
# The computer's own (see above), by the names their files start with.
system_lib='^(ld-linux|libc|libm|libmvec|libpthread|libdl|librt|libutil|libresolv|libanl|libnsl|libBrokenLocale|libGL|libGLX|libEGL|libOpenGL|libGLES[a-zA-Z0-9_]*|libGLdispatch|libvulkan|libgbm|libdrm|libdrm_[a-z]+|libva|libva-[a-z0-9]+|libwayland-[a-z]+|libX11|libX11-xcb|libxcb|libxcb-[a-z0-9-]+|libXext|libXrandr|libXrender|libXi|libXau|libXdmcp|libXfixes|libXcursor|libXinerama|libxshmfence|libXxf86vm|libXRes|libXss|libXtst|libXcomposite|libXdamage|libSM|libICE|libasound|libpulse|libpulse-simple|libpulsecommon-[0-9.]+|libpipewire-[0-9.]+|libjack|libdbus-1|libsystemd|libfontconfig|libfreetype|libexpat)\.so'
# But xcb's utility libraries, which Qt's X11 plugin needs and many computers lack ("xcb-cursor0
# is needed"): they need no more than libxcb itself.
taken_lib='^libxcb-(cursor|icccm|image|keysyms|render-util|util|ewmh)\.so'

core_file() {
  case "$1" in
    station) echo "$forks/station/build-core/bin/reader_core_station.so" ;;
    flycast) echo "$forks/flycast/build-core/reader_core_flycast.so" ;;
    ppsspp) echo "$forks/ppsspp/build-core/lib/reader_core_ppsspp.so" ;;
    azahar) echo "$forks/azahar/build-core/bin/Release/reader_core_azahar.so" ;;
    pcsx2) echo "${READER_PCSX2_CORE:-$forks/pcsx2/build-core-portable/bin/reader_core_pcsx2.so}" ;;
    eden) echo "$forks/eden/build-core/bin/reader_core_eden.so" ;;
    vita3k) echo "$forks/vita3k/build-core/bin/reader_core_vita3k.so" ;;
    *) echo "package-linux: no core called $1" >&2; return 1 ;;
  esac
}

rm -rf "$out"
mkdir -p "$out/lib" "$out/cores"
# Every library a file needs (ldd follows them all), but the computer's own.
copy_deps() {
  local name path missing
  while read -r name path; do
    [ -n "$path" ] && [ -f "$path" ] || continue
    [[ "$name" =~ $system_lib ]] && ! [[ "$name" =~ $taken_lib ]] && continue
    [ -e "$out/lib/$name" ] && continue
    install -m 644 "$(realpath "$path")" "$out/lib/$name"
  done < <(ldd "$1" | awk '$2 == "=>" && $3 ~ /^\// {print $1, $3}')
  # One of the computer's own may be missing here (it is not taken); any other must be found.
  missing="$(ldd "$1" | awk '$2 == "=>" && $3 == "not" {print $1}' | grep -E -v "$system_lib" || true)"
  if [ -n "$missing" ]; then
    echo "package-linux: $1 needs what is not to be found here: $missing" >&2
    exit 1
  fi
}

install -m 755 "$build/reader-home" "$out/reader-home.bin"
# Frida's frida-inject, which runs Agent's scripts (docs/hooks.md; tools/fetch-hooks.sh linux
# fetches it into the build folder). Without it the program fetches it at first use.
if [ -f "$build/hooks/frida-inject" ]; then
  mkdir -p "$out/hooks"
  install -m 755 "$build/hooks/frida-inject" "$out/hooks/frida-inject"
  install -m 644 "$build/hooks/frida-COPYING.txt" "$out/hooks/frida-COPYING.txt"
fi
"$strip" --strip-unneeded "$out/reader-home.bin"
copy_deps "$build/reader-home"
# Qt's plugins: X11 (with its GL and EGL ways of drawing), the offscreen platform for tests,
# the picture formats of covers, and TLS for the network.
for plugin in platforms/libqxcb.so platforms/libqoffscreen.so \
              xcbglintegrations/libqxcb-glx-integration.so xcbglintegrations/libqxcb-egl-integration.so \
              imageformats/libqjpeg.so imageformats/libqgif.so imageformats/libqico.so imageformats/libqsvg.so \
              tls/libqopensslbackend.so tls/libqcertonlybackend.so; do
  [ -f "$READER_QT_LINUX/plugins/$plugin" ] || continue
  mkdir -p "$out/plugins/$(dirname "$plugin")"
  install -m 644 "$READER_QT_LINUX/plugins/$plugin" "$out/plugins/$plugin"
  copy_deps "$READER_QT_LINUX/plugins/$plugin"
done
# Qt WebEngine: its helper process, its resources and locales, where qt.conf below says.
if [ "$web" = 1 ]; then
  mkdir -p "$out/libexec" "$out/translations"
  install -m 755 "$READER_QT_LINUX/libexec/QtWebEngineProcess" "$out/libexec/QtWebEngineProcess"
  copy_deps "$READER_QT_LINUX/libexec/QtWebEngineProcess"
  cp -r "$READER_QT_LINUX/resources" "$out/resources"
  cp -r "$READER_QT_LINUX/translations/qtwebengine_locales" "$out/translations/qtwebengine_locales"
fi
for key in $cores_wanted; do
  core="$(core_file "$key")"
  if [ ! -f "$core" ]; then
    echo "package-linux: no $key core at $core: build it (its fork's tools/reader/build-core.sh)" >&2
    exit 1
  fi
  if [ "$key" = pcsx2 ] && grep -q -- '-march=native' "$(dirname "$core")/../build.ninja" 2>/dev/null; then
    echo "package-linux: $core is built for this processor only (-march=native): see the head of this script" >&2
    exit 1
  fi
  install -m 644 "$core" "$out/cores/$(basename "$core")"
  "$strip" --strip-unneeded "$out/cores/$(basename "$core")"
  copy_deps "$core"
done
# PCSX2's core opens Vulkan's shader compiler by name (libshaderc_shared.so.1): from PCSX2's
# own prefix, read without the Flycast fork's environment this script has.
if [ -f "$out/cores/reader_core_pcsx2.so" ]; then
  pcsx2_prefix="${READER_PCSX2_PREFIX:-$(env -i HOME="$HOME" bash -c '. "$1" && echo "$READER_HOST_PREFIX"' _ "$root/../pcsx2/tools/reader/env.sh")}"
  shaderc="$pcsx2_prefix/lib/libshaderc_shared.so.1"
  [ -f "$shaderc" ] || { echo "package-linux: no $shaderc" >&2; exit 1; }
  install -m 644 "$(realpath "$shaderc")" "$out/lib/libshaderc_shared.so.1"
  copy_deps "$shaderc"
  mkdir -p "$out/cores/pcsx2"
  cp -r "$forks/pcsx2/bin/resources" "$out/cores/pcsx2/resources"
fi
# PPSSPP's assets beside its core (its fonts, the PSP's flash0, compat.ini; not the profiles,
# which the program carries).
if [ -f "$out/cores/reader_core_ppsspp.so" ]; then
  cp -r "$forks/ppsspp/assets" "$out/cores/ppsspp"
  rm -rf "$out/cores/ppsspp/debugger" "$out/cores/ppsspp/reader"
fi
# Vita3K's data and built-in shaders beside its core.
if [ -f "$out/cores/reader_core_vita3k.so" ]; then
  cp -r "$forks/vita3k/build-core/bin/vita3k" "$out/cores/vita3k"
fi
# This machine's folders out of the files' search paths: each finds its libraries in lib.
"$python" "$here/elf_rpath.py" '$ORIGIN/lib' "$out/reader-home.bin"
"$python" "$here/elf_rpath.py" '$ORIGIN/../lib' "$out"/cores/*.so
"$python" "$here/elf_rpath.py" '$ORIGIN' "$out"/lib/*
find "$out/plugins" -name '*.so' -exec "$python" "$here/elf_rpath.py" '$ORIGIN/../../lib' {} +
if [ "$web" = 1 ]; then "$python" "$here/elf_rpath.py" '$ORIGIN/../lib' "$out/libexec/QtWebEngineProcess"; fi
# Each file again as it will be found where the package runs, with nothing of this machine's
# folders: every library it needs in lib or the computer's own, and of a version that has
# what it asks for (two builds can bring the same library in versions of different ages; the
# first one taken is kept).
elfs="$(find "$out" -type f \( -name '*.so*' -o -name 'reader-home.bin' -o -name 'QtWebEngineProcess' \))"
bad=0
while read -r file; do
  report="$(env -i PATH=/usr/bin:/bin LD_LIBRARY_PATH="$out/lib" ldd "$file" 2>&1 || true)"
  if grep -q 'version .* not found' <<<"$report" ||
     awk '$2 == "=>" && $3 == "not" {print $1}' <<<"$report" | grep -E -q -v "$system_lib"; then
    echo "package-linux: ${file#"$out"/}:" >&2
    grep -E 'not found' <<<"$report" >&2
    bad=1
  fi
done <<<"$elfs"
[ "$bad" = 0 ] || exit 1
# What the C library must have: no newer than glibc_max.
newest="$(while read -r file; do objdump -T "$file" 2>/dev/null | grep -o 'GLIBC_[0-9.]*'; done <<<"$elfs" | sort -u -V | tail -1)"
newest="${newest#GLIBC_}"
if [ "$(printf '%s\n%s\n' "$newest" "$glibc_max" | sort -V | tail -1)" != "$glibc_max" ]; then
  echo "package-linux: a file wants glibc $newest, newer than $glibc_max:" >&2
  while read -r file; do objdump -T "$file" 2>/dev/null | grep -q "GLIBC_$newest" && echo "  $file" >&2; done <<<"$elfs"
  exit 1
fi
cat > "$out/qt.conf" <<'CONF'
[Paths]
Prefix = .
Plugins = plugins
LibraryExecutables = libexec
Data = .
Translations = translations
CONF
cat > "$out/reader-home" <<'SCRIPT'
#!/bin/sh
# Furoshiki (reader-home) from this folder, with the libraries it carries. Arguments are the
# program's: --play FILE plays a game at once, --window keeps it out of full screen.
here="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$here/plugins"
# X11 where there is a display (Wayland has not been tried); the PlayStation's core draws
# through EGL there.
[ -n "${QT_QPA_PLATFORM:-}" ] || { [ -n "${DISPLAY:-}" ] && export QT_QPA_PLATFORM=xcb; }
export USE_EGL_X11="${USE_EGL_X11:-1}"
# Qt WebEngine's popup (a package made with it): Chromium's sandbox will not run as root (the
# Batocera box runs everything as root).
if [ -x "$here/libexec/QtWebEngineProcess" ]; then
  export QTWEBENGINEPROCESS_PATH="$here/libexec/QtWebEngineProcess"
  export QTWEBENGINE_RESOURCES_PATH="$here/resources" QTWEBENGINE_LOCALES_PATH="$here/translations/qtwebengine_locales"
  [ "$(id -u)" = 0 ] && export QTWEBENGINE_DISABLE_SANDBOX=1
fi
exec "$here/reader-home.bin" "$@"
SCRIPT
chmod 755 "$out/reader-home"
{
  echo "Furoshiki (reader-home) for Linux, x86-64"
  echo "commit $(git -C "$root" rev-parse --short HEAD)$(git -C "$root" diff --quiet HEAD -- . ../cores ../shared || echo ' (with changes)')"
  echo "made $(date -u +%Y-%m-%dT%H:%MZ), needs glibc $newest or newer"
  echo "cores: $cores_wanted"
} > "$out/VERSION"
(cd "$out/.." && rm -f Furoshiki-linux.tar.gz Furoshiki-linux.tar.gz.sha256 &&
  tar --owner=0 --group=0 -czf Furoshiki-linux.tar.gz Furoshiki &&
  sha256sum Furoshiki-linux.tar.gz > Furoshiki-linux.tar.gz.sha256)
cat "$out/VERSION"
du -sh "$out" "$out/../Furoshiki-linux.tar.gz"
