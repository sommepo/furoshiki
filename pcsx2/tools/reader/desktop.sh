#!/usr/bin/env bash
# Run the desktop app on this machine, which has no display: Qt's "offscreen" platform. This
# is how the reader's windows and the PS2-style menu are tried here; tools/reader/dc.py talks
# to the running app (keys, clicks, pictures of its windows).
#   tools/reader/desktop.sh [app arguments...] &
# Home is private/desktop (READER_DESKTOP_HOME): the app keeps its files in config/PCSX2Reader
# there, where a first settings file says that the setup wizard is done and names
# private/desktop/games as the games folder. Talks on private/desktop/sock.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
home="${READER_DESKTOP_HOME:-$root/private/desktop}"
data="$home/config/PCSX2Reader"
mkdir -p "$data/inis" "$home/games" "$home/fontconfig" "$home/shots"
if [ ! -f "$data/inis/PCSX2.ini" ]; then
  printf '[UI]\nSetupWizardIncomplete = false\nSettingsVersion = 1\n\n[GameList]\nRecursivePaths = %s\n\n[AutoUpdater]\nCheckAtStartup = false\n' "$home/games" > "$data/inis/PCSX2.ini"
fi
# A font with Japanese in it, and nothing else: what the windows are drawn with here.
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
export XDG_CONFIG_HOME="$home/config"
export QT_QPA_PLATFORM=offscreen
export QT_PLUGIN_PATH="$READER_HOST_PREFIX/lib/qt6/plugins"
export LD_LIBRARY_PATH="$READER_HOST_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export READER_DESKTOP_CONTROL="$home/sock"
# Run a copy: the build replaces the binary in place, which takes a running app down. The
# copy stands beside the build's resources folder, which the app looks for next to itself.
binary="${READER_DESKTOP_BUILD:-$root/build-desktop}/bin/pcsx2-reader.$$"
cp "${READER_DESKTOP_BUILD:-$root/build-desktop}/bin/pcsx2-qt" "$binary"
trap 'rm -f "$binary"' EXIT
cd "$home"
"$here/limit.sh" -m "${READER_DESKTOP_MEMORY_MAX:-8G}" "$binary" "$@"
