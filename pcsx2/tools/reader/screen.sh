#!/usr/bin/env bash
# Run the desktop app on this machine with a picture: on a Weston compositor that draws to
# nothing (with Xwayland), the game drawn by Mesa's software Vulkan driver. This is how a game
# is run here, to write its text profile and to try the reader in it; desktop.sh, without any
# screen, is enough for the menu and the reader's windows.
#   tools/reader/screen.sh [-- app arguments...] &        e.g.  screen.sh -- /path/to/game.iso
# Home is private/desktop, as with desktop.sh: the same settings, BIOS and games folders.
# Needs a Weston with Xwayland (READER_WESTON_PREFIX; the PPSSPP Reader fork's by default) and
# a software Vulkan driver (READER_VULKAN_PREFIX; Eden Reader's by default).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
home="${READER_DESKTOP_HOME:-$root/private/desktop}"
weston="${READER_WESTON_PREFIX:-$HOME/.local/share/ppsspp-reader/weston}"
vulkan="${READER_VULKAN_PREFIX:-$HOME/.local/share/eden-reader/vulkan}"
[ "${1:-}" = "--" ] && shift
data="$home/config/PCSX2Reader"
mkdir -p "$data/inis" "$data/bios" "$home/games" "$home/fontconfig" "$home/shots" "$home/run"
chmod 700 "$home/run"
if [ ! -f "$data/inis/PCSX2.ini" ]; then
  printf '[UI]\nSetupWizardIncomplete = false\nSettingsVersion = 1\n\n[GameList]\nRecursivePaths = %s\n\n[AutoUpdater]\nCheckAtStartup = false\n' "$home/games" > "$data/inis/PCSX2.ini"
fi
# The stand-in screen, once: it stays for the next run.
if ! [ -S "$home/run/reader-wl" ] || ! pgrep -u "$(id -u)" -f "weston.*--socket=reader-wl" >/dev/null; then
  rm -f "$home/run/reader-wl" "$home/run/reader-wl.lock"
  setsid nohup "$here/limit.sh" -m 1G env XDG_RUNTIME_DIR="$home/run" LD_LIBRARY_PATH="$weston/lib" \
    PATH="$weston/bin:$PATH" "$weston/bin/weston" --backend=headless --socket=reader-wl \
    --width="${READER_DESKTOP_WIDTH:-1280}" --height="${READER_DESKTOP_HEIGHT:-720}" \
    --renderer=pixman --idle-time=0 --xwayland > "$home/weston.log" 2>&1 &
  sleep 3
fi
display="$(grep -o 'listening on display :[0-9]*' "$home/weston.log" | tail -1 | grep -o ':[0-9]*')"
[ -n "$display" ] || { echo "screen.sh: the stand-in screen did not start: $home/weston.log" >&2; exit 1; }
cat > "$home/fontconfig/fonts.conf" <<CONF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$(dirname "$READER_PREVIEW_FONT_FILE")</dir>
  <cachedir>$home/fontconfig/cache</cachedir>
</fontconfig>
CONF
binary="${READER_DESKTOP_BUILD:-$root/build-desktop}/bin/pcsx2-reader.$$"
cp "${READER_DESKTOP_BUILD:-$root/build-desktop}/bin/pcsx2-qt" "$binary"
trap 'rm -f "$binary"' EXIT
cd "$home"
"$here/limit.sh" -m "${READER_DESKTOP_MEMORY_MAX:-10G}" env DISPLAY="$display" QT_QPA_PLATFORM=xcb \
  XDG_CONFIG_HOME="$home/config" FONTCONFIG_FILE="$home/fontconfig/fonts.conf" \
  QT_PLUGIN_PATH="$READER_HOST_PREFIX/lib/qt6/plugins" \
  LD_LIBRARY_PATH="$READER_HOST_PREFIX/lib:$vulkan/lib" \
  VK_DRIVER_FILES="$vulkan/share/vulkan/icd.d/lvp_icd.x86_64.json" \
  VK_ICD_FILENAMES="$vulkan/share/vulkan/icd.d/lvp_icd.x86_64.json" \
  LP_NUM_THREADS="${LP_NUM_THREADS:-$READER_JOBS}" SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" \
  READER_DESKTOP_CONTROL="$home/sock" "$binary" "$@"
