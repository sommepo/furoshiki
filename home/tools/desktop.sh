#!/usr/bin/env bash
# Run Furoshiki (the Linux build, with its cores) on a stand-in screen, for tests and
# pictures: a Weston compositor that draws to nothing, with Xwayland, the cores' pictures drawn
# by Mesa in software. The reader's development channel listens at <home>/sock (a reader
# fork's ps.py or dc.py talks to it).
#   tools/desktop.sh [--play FILE] [arguments for reader-home...] &
# Its home is private/desktop (READER_DESKTOP_HOME): Furoshiki's folder is <home>/data/reader-home
# (READER_HOME_DIR), a console's BIOS goes in its program/consoles/<console>/system folder
# there (program/consoles/ps1/system for the PlayStation; PPSSPP's assets are linked beside its
# core). The cores are taken from READER_HOME_CORES, by
# default <home>/cores, where the forks' core builds that exist are linked (Station's, Flycast's,
# PPSSPP's, Azahar's, PCSX2's, Eden's, Vita3K's: each fork's tools/reader/build-core.sh). Needs a Weston with
# Xwayland (READER_WESTON_PREFIX; the PPSSPP Reader fork's on this machine by default). PCSX2's,
# Eden's and Vita3K's cores draw with Vulkan, Mesa's software driver of the same Mesa (READER_HOME_VULKAN_PREFIX names another),
# and the Switch's keys (prod.keys) go in program/consoles/switch/system.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# READER_HOME_WEB=1: the build with Qt WebEngine for the popup page (tools/build-desktop.sh,
# build-desktop-web), with its Qt.
if [ "${READER_HOME_WEB:-0}" = 1 ]; then
  export READER_QT_LINUX="${READER_HOME_WEB_QT:-$HOME/.local/share/furoshiki-qt/6.9.3/gcc_64}"
fi
. "$here/env.sh"
root="$(cd "$here/.." && pwd)"
home="${READER_DESKTOP_HOME:-$root/private/desktop}"
weston="${READER_WESTON_PREFIX:-$HOME/.local/share/ppsspp-reader/weston}"
sdl2="${HOME_SDL2_PREFIX:-$HOME/.local/share/station-reader/deps}"
cores="${READER_HOME_CORES:-$home/cores}"
vulkan="${READER_HOME_VULKAN_PREFIX:-${READER_VULKAN_PREFIX:-$READER_MESA_PREFIX}}"
if [ -z "${READER_HOME_CORES:-}" ]; then
  mkdir -p "$cores"
  for built in "$root/../station/build-core/bin/reader_core_station.so" \
               "$root/../flycast/build-core/reader_core_flycast.so" \
               "$root/../ppsspp/build-core/lib/reader_core_ppsspp.so" \
               "$root/../azahar/build-core/bin/Release/reader_core_azahar.so" \
               "$root/../pcsx2/build-core/bin/reader_core_pcsx2.so" \
               "$root/../eden/build-core/bin/reader_core_eden.so" \
               "$root/../vita3k/build-core/bin/reader_core_vita3k.so"; do
    [ -f "$built" ] && ln -sfn "$(realpath "$built")" "$cores/$(basename "$built")"
  done
  # PPSSPP's assets beside its core, where it looks when the console's system folder has none.
  [ -e "$cores/reader_core_ppsspp.so" ] && ln -sfn "$(realpath "$root/../ppsspp/assets")" "$cores/ppsspp"
  # PCSX2's resources (shaders, fonts, the game index) in pcsx2/resources beside its core.
  [ -e "$cores/reader_core_pcsx2.so" ] && ln -sfn "$(realpath "$root/../pcsx2/bin")" "$cores/pcsx2"
  # Vita3K's data and built-in shaders in vita3k beside its core.
  [ -e "$cores/reader_core_vita3k.so" ] && ln -sfn "$(realpath "$root/../vita3k/build-core/bin/vita3k")" "$cores/vita3k"
fi
build="$root/build-desktop$([ "${READER_HOME_WEB:-0}" = 1 ] && echo -web || true)"
[ -x "$build/reader-home" ] || { echo "desktop.sh: not built yet: tools/build-desktop.sh" >&2; exit 1; }
mkdir -p "$home/home" "$home/config" "$home/data/reader-home/program/consoles/ps1/system" "$home/run" "$home/fontconfig" "$home/shots" "$home/app"
chmod 700 "$home/run"
if ! [ -S "$home/run/reader-wl" ] || ! pgrep -u "$(id -u)" -f "weston.*--socket=reader-wl.*$home" >/dev/null; then
  rm -f "$home/run/reader-wl" "$home/run/reader-wl.lock"
  setsid nohup "$here/limit.sh" -m 1G env XDG_RUNTIME_DIR="$home/run" LD_LIBRARY_PATH="$weston/lib" \
    PATH="$weston/bin:$PATH" "$weston/bin/weston" --backend=headless --socket=reader-wl \
    --width="${READER_DESKTOP_WIDTH:-1280}" --height="${READER_DESKTOP_HEIGHT:-720}" \
    --renderer=pixman --idle-time=0 --xwayland --log="$home/weston.log" > /dev/null 2>&1 &
  sleep 3
fi
display="$(grep -o 'listening on display :[0-9]*' "$home/weston.log" | tail -1 | grep -o ':[0-9]*')"
[ -n "$display" ] || { echo "desktop.sh: the stand-in screen did not start: $home/weston.log" >&2; exit 1; }
cat > "$home/fontconfig/fonts.conf" <<CONF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$(dirname "$READER_PREVIEW_FONT_FILE")</dir>
  <cachedir>$home/fontconfig/cache</cachedir>
</fontconfig>
CONF
# Run a copy (the build replaces the program in place, which takes a running one down).
binary="$home/app/reader-home.$$"
cp "$build/reader-home" "$binary"
trap 'rm -f "$binary"' EXIT
cd "$home"
"$here/limit.sh" -m "${READER_DESKTOP_MEMORY_MAX:-4G}" env HOME="$home/home" DISPLAY="$display" \
  XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_RUNTIME_DIR="$home/run" \
  READER_HOME_DIR="$home/data/reader-home" READER_HOME_CORES="$cores" \
  USE_EGL_X11=1 QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$READER_QT_LINUX/plugins" \
  LD_LIBRARY_PATH="$READER_MESA_PREFIX/lib:$sdl2/lib:$READER_QT_LINUX/lib:$READER_QT_SYSLIBS:$weston/lib" \
  VK_DRIVER_FILES="$vulkan/share/vulkan/icd.d/lvp_icd.x86_64.json" \
  VK_ICD_FILENAMES="$vulkan/share/vulkan/icd.d/lvp_icd.x86_64.json" \
  __EGL_VENDOR_LIBRARY_DIRS="$READER_MESA_PREFIX/share/glvnd/egl_vendor.d" LIBGL_ALWAYS_SOFTWARE=1 \
  LP_NUM_THREADS="${LP_NUM_THREADS:-$READER_JOBS}" READER_HOME_FONT="$READER_PREVIEW_FONT_FILE" \
  FONTCONFIG_FILE="$home/fontconfig/fonts.conf" SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" \
  READER_DESKTOP_CONTROL="$home/sock" ${READER_WRAP:-} "$binary" --window "$@"
