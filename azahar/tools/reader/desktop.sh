#!/usr/bin/env bash
# Run the desktop app on this machine, which has no display: Qt's "offscreen" platform, the
# game drawn by the emulator's software renderer (set once in the app's own settings, see
# below). This is how the desktop reader layer is tried against a real game here;
# tools/reader/dc.py talks to the running app (clicks, grabs of its windows).
#   tools/reader/desktop.sh [--rom FILE] [app arguments...] &
# Home is private/desktop (READER_DESKTOP_HOME): the app is started there and keeps its files
# in the folder "user" it finds there. Talks on private/desktop/sock.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
home="${READER_DESKTOP_HOME:-$root/private/desktop}"
rom=""
rest=()
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) rom="$2"; shift 2 ;;
    *) rest+=("$1"); shift ;;
  esac
done
mkdir -p "$home/user/config" "$home/fontconfig"
# Without a display there is no window for a graphics card to draw into: the software
# renderer draws into a picture of the app's own. Written once; the app keeps the file.
if [ ! -f "$home/user/config/qt-config.ini" ]; then
  printf '[Renderer]\ngraphics_api\\default=false\ngraphics_api=0\n' > "$home/user/config/qt-config.ini"
fi
# A font with Japanese in it, and nothing else: what the reader's windows are drawn with here.
cat > "$home/fontconfig/fonts.conf" <<CONF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$(dirname "$READER_PREVIEW_FONT_FILE")</dir>
  <cachedir>$home/fontconfig/cache</cachedir>
</fontconfig>
CONF
export FONTCONFIG_FILE="$home/fontconfig/fonts.conf"
# Reader Home's folder, where the reader apps share their settings and dictionaries: one of
# this run's own, not the one of the person at this computer.
export READER_HOME_DIR="${READER_HOME_DIR:-$home/reader-home}"
# Qt's Multimedia module (the emulator's camera) asks for the system's sound client library,
# which this machine does not have installed: the folders of the copies the toolchain's
# package manager unpacked, and of what those ask for in turn, are searched as well.
sound=""
for name in libpulse.so.0 'libpulsecommon-*.so' libsndfile.so.1 'libFLAC.so.1[0-9]' libvorbis.so.0 \
            libogg.so.0 libopus.so.0 libmpg123.so.0 libmp3lame.so.0; do
  found="$(find "$READER_TOOLCHAINS/mamba/pkgs" -name "$name" 2>/dev/null | sort | tail -1)"
  [ -n "$found" ] && sound="$sound:$(dirname "$found")"
done
# READER_DESKTOP_QPA=xcb with a DISPLAY puts the app on a screen (a stand-in one will do), which
# is what the OpenGL and Vulkan renderers need to draw at all.
export QT_QPA_PLATFORM="${READER_DESKTOP_QPA:-offscreen}"
export QT_PLUGIN_PATH="$READER_QT_LINUX/plugins"
export LD_LIBRARY_PATH="$READER_QT_LINUX/lib:$READER_QT_SYSLIBS:$READER_VULKAN_PREFIX/lib$sound${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export READER_DESKTOP_CONTROL="$home/sock"
# Run a copy: the build replaces the binary in place, which takes a running app down.
binary="$home/azahar-reader.$$"
cp "$root/build-desktop/bin/Release/azahar" "$binary"
trap 'rm -f "$binary"' EXIT
cd "$home"
args=()
[ -n "$rom" ] && args+=("$rom")
"$here/limit.sh" -m "${READER_DESKTOP_MEMORY_MAX:-8G}" "$binary" "${args[@]}" "${rest[@]}"
