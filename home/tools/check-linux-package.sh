#!/usr/bin/env bash
# Unpack the Linux package (tools/package-linux.sh) into a folder of its own and run it there on
# the stand-in screen (as tools/desktop.sh does: Weston without a display, with Xwayland), with
# nothing of this machine's build folders in its environment: only what a computer it goes to
# has of its own, the graphics driver (Mesa in software here, as for desktop.sh) and the sound
# library. After a while it lists the libraries the program has loaded from outside the
# package (foreign-libs.txt), then waits for it to end.
#   tools/check-linux-package.sh [--tarball FILE] [arguments for reader-home, e.g. --play FILE] &
# Its folder is private/package-check (READER_PACKAGE_CHECK_HOME): the package in unpacked/,
# Furoshiki's folder in data/reader-home (put a console's BIOS in its
# program/consoles/<console>/system). The stand-in screen's sockets and the development channel
# (sock: a fork's ps.py talks to it) are in $XDG_RUNTIME_DIR/reader-package-check
# (READER_PACKAGE_CHECK_RUN): a socket's path must be shorter than 108 bytes.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/.." && pwd)"
check="${READER_PACKAGE_CHECK_HOME:-$root/private/package-check}"
run="${READER_PACKAGE_CHECK_RUN:-${XDG_RUNTIME_DIR:-/tmp}/reader-package-check}"
tarball="$root/build-desktop/package/Furoshiki-linux.tar.gz"
if [ "${1:-}" = "--tarball" ]; then tarball="$2"; shift 2; fi
weston="${READER_WESTON_PREFIX:-$HOME/.local/share/ppsspp-reader/weston}"
# What the package leaves to the computer that this machine has only in prefixes of its own (the
# GL dispatch libraries, Mesa, PulseAudio's client, VA): links to those in drivers/lib, then the
# prefixes themselves for what Mesa and VA need in turn (on a computer, beside them in /usr/lib).
# They come after the package's lib, so they only fill gaps: the report shows each one taken.
drivers="$READER_MESA_PREFIX"
va="${READER_HOME_VA_LIB:-$HOME/.local/share/eden-reader/vulkan/lib}"
pulse="${READER_HOME_PULSE_LIB:-$drivers/lib}"
rm -rf "$check/drivers"
mkdir -p "$check/drivers/lib"
for dir in "$drivers/lib" "$READER_QT_SYSLIBS" "$pulse" "$va"; do
  for file in "$dir"/lib{GL,GLX,EGL,OpenGL,GLESv2,GLdispatch,EGL_mesa,GLX_mesa,gallium,gbm,drm,vulkan,pulse,pulsecommon,va}[-.]*so*; do
    [ -e "$file" ] && [ ! -e "$check/drivers/lib/$(basename "$file")" ] && ln -s "$file" "$check/drivers/lib/$(basename "$file")"
  done
done
# PulseAudio's client finds its common part in pulseaudio/ beside it.
[ -d "$pulse/pulseaudio" ] && ln -s "$pulse/pulseaudio" "$check/drivers/lib/pulseaudio"
[ -f "$tarball" ] || { echo "check-linux-package: no $tarball: tools/package-linux.sh" >&2; exit 1; }
rm -rf "$check/unpacked"
mkdir -p "$check/unpacked" "$check/home" "$check/config" "$check/data/reader-home" "$run"
chmod 700 "$run"
tar -xzf "$tarball" -C "$check/unpacked"
package="$check/unpacked/Furoshiki"
if ! [ -S "$run/reader-pkg" ] || ! pgrep -u "$(id -u)" -f "weston.*--socket=reader-pkg.*$check" >/dev/null; then
  rm -f "$run/reader-pkg" "$run/reader-pkg.lock"
  setsid nohup "$here/limit.sh" -m 1G env XDG_RUNTIME_DIR="$run" LD_LIBRARY_PATH="$weston/lib" \
    PATH="$weston/bin:$PATH" "$weston/bin/weston" --backend=headless --socket=reader-pkg \
    --width="${READER_DESKTOP_WIDTH:-1280}" --height="${READER_DESKTOP_HEIGHT:-720}" \
    --renderer=pixman --idle-time=0 --xwayland --log="$check/weston.log" > /dev/null 2>&1 &
  sleep 3
fi
display="$(grep -o 'listening on display :[0-9]*' "$check/weston.log" | tail -1 | grep -o ':[0-9]*')"
[ -n "$display" ] || { echo "check-linux-package: the stand-in screen did not start: $check/weston.log" >&2; exit 1; }
# This machine has no Japanese font of its own (the box has Noto Sans JP).
font=()
fc-list :lang=ja | grep -q . || font=(READER_HOME_FONT="$READER_PREVIEW_FONT_FILE")
"$here/limit.sh" -m "${READER_DESKTOP_MEMORY_MAX:-4G}" env -i PATH=/usr/bin:/bin LANG=C.UTF-8 \
  HOME="$check/home" DISPLAY="$display" XDG_CONFIG_HOME="$check/config" XDG_DATA_HOME="$check/data" \
  XDG_RUNTIME_DIR="$run" READER_HOME_DIR="$check/data/reader-home" \
  READER_DESKTOP_CONTROL="$run/sock" LD_LIBRARY_PATH="$check/drivers/lib:$drivers/lib:$va" \
  VK_DRIVER_FILES="$drivers/share/vulkan/icd.d/lvp_icd.x86_64.json" \
  VK_ICD_FILENAMES="$drivers/share/vulkan/icd.d/lvp_icd.x86_64.json" \
  __EGL_VENDOR_LIBRARY_DIRS="$drivers/share/glvnd/egl_vendor.d" LIBGL_ALWAYS_SOFTWARE=1 \
  LP_NUM_THREADS="${LP_NUM_THREADS:-$READER_JOBS}" SDL_AUDIODRIVER=dummy "${font[@]}" \
  "$package/reader-home" --window "$@" &
started=$!
sleep "${READER_PACKAGE_CHECK_WAIT:-20}"
pid="$(pgrep -n -f "^$package/reader-home.bin" || true)"  # its main thread is renamed: not by -x
if [ -n "$pid" ]; then
  awk '$6 ~ /\.so/ {print $6}' "/proc/$pid/maps" | sort -u | grep -v "^$package/" > "$check/foreign-libs.txt" || true
  echo "check-linux-package: loaded from outside the package ($check/foreign-libs.txt):"
  sed 's/^/  /' "$check/foreign-libs.txt"
else
  echo "check-linux-package: the program is not running" >&2
fi
wait "$started"
