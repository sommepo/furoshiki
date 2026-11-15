#!/usr/bin/env bash
# A device in the hand of an agent: the AYN Thor (two displays) or any Android device with
# Furoshiki, through adb. After the 3a project's tools/phone.sh: all of adb in a few words, with
# pictures of both displays. Nothing here roots, flashes or wipes.
#   tools/thor.sh status             the device, its displays, the home app, Furoshiki's version, what is in front
#   tools/thor.sh shot [name]        pictures of every display: <name>-top.png and <name>-lower.png (prints their paths)
#   tools/thor.sh key NAME...        keys in turn: a b x y l1 r1 start select up down left right home back, or KEYCODE_*
#   tools/thor.sh hold NAME          a key held as long as a game needs to see it
#   tools/thor.sh tap X Y [lower]    a touch on the first display, or the second
#   tools/thor.sh swipe X1 Y1 X2 Y2 [ms] [lower]
#   tools/thor.sh start              Furoshiki, the way a launcher starts it
#   tools/thor.sh game               the game in play: its ID and title as its core said them, and its pace
#   tools/thor.sh log [pattern]      the game process's log, without the core's own chatter
#   tools/thor.sh install [apk]      install the build (default: app/build/outputs/apk/debug/app-debug.apk); never over a game in play
#   tools/thor.sh home on|off        Furoshiki as the home screen of both displays, or Android's own launcher back
#   tools/thor.sh connect [host]     reach the device without the cable (wireless debugging must be on; pair over USB first)
# DEVICE=<serial or address:port> chooses the device; default: the only one on adb.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
adb_bin="${ADB:-}"
for candidate in "$(command -v adb || true)" \
  "$HOME/.local/share/azahar-reader/android-sdk/platform-tools/adb" "${ANDROID_HOME:-/nonexistent}/platform-tools/adb"; do
  [ -n "$adb_bin" ] && break
  [ -x "$candidate" ] && adb_bin="$candidate"
done
[ -n "$adb_bin" ] || { echo "adb not found: set ADB=/path/to/adb"; exit 1; }
pkg=io.github.sommepo.furoshiki
shots="${SHOTS:-$here/../build-tmp/shots}"
port_file="$here/../private/thor.port"

serial="${DEVICE:-}"
if [ -z "$serial" ]; then
  listed="$("$adb_bin" devices | awk '$2=="device"{print $1}')"
  if [ "$(grep -c . <<<"$listed")" = 1 ]; then serial="$listed"
  elif [ "${1:-}" != connect ]; then echo "No one device on adb (set DEVICE=...):"; "$adb_bin" devices -l; exit 1; fi
fi
adb() { "$adb_bin" -s "$serial" "$@"; }

keycode() {
  case "$1" in
    a) echo KEYCODE_BUTTON_A ;; b) echo KEYCODE_BUTTON_B ;; x) echo KEYCODE_BUTTON_X ;; y) echo KEYCODE_BUTTON_Y ;;
    l1) echo KEYCODE_BUTTON_L1 ;; r1) echo KEYCODE_BUTTON_R1 ;; l2) echo KEYCODE_BUTTON_L2 ;; r2) echo KEYCODE_BUTTON_R2 ;;
    start) echo KEYCODE_BUTTON_START ;; select) echo KEYCODE_BUTTON_SELECT ;;
    up) echo KEYCODE_DPAD_UP ;; down) echo KEYCODE_DPAD_DOWN ;; left) echo KEYCODE_DPAD_LEFT ;; right) echo KEYCODE_DPAD_RIGHT ;;
    home) echo KEYCODE_HOME ;; back) echo KEYCODE_BACK ;; ok) echo KEYCODE_DPAD_CENTER ;; wake) echo KEYCODE_WAKEUP ;;
    *) echo "$1" ;;
  esac
}

# The displays as `id uniqueId` lines, the first display first: screencap wants the long number, input the short.
displays() {
  adb shell dumpsys display | tr -d '\r' | awk '
    /mDisplayId=/ { sub(/.*mDisplayId=/, ""); id = $1 }
    /mPrimaryDisplayDevice=|mBaseDisplayInfo=/ && match($0, /uniqueId "local:[0-9]+"/) {
      unique = substr($0, RSTART + 16, RLENGTH - 17); if (!(id in seen)) { seen[id] = 1; print id, unique }
    }' | sort -n
}

case "${1:-status}" in
  status)
    echo "device       $(adb shell getprop ro.product.model | tr -d '\r'), Android $(adb shell getprop ro.build.version.release | tr -d '\r') ($serial)"
    echo "displays     $(displays | tr '\n' ';')"
    echo "battery      $(adb shell dumpsys battery | awk '/level/{print $2}' | tr -d '\r')%"
    echo "home app     $(adb shell cmd package resolve-activity --brief -c android.intent.category.HOME -a android.intent.action.MAIN | tail -1 | tr -d '\r')"
    echo "furoshiki    $(adb shell dumpsys package $pkg | awk -F= '/versionName/{print $2; exit}' | tr -d '\r')"
    echo "in front     $(adb shell dumpsys activity activities | tr -d '\r' | awk '/topResumedActivity=/{print $3}' | tr '\n' ' ')"
    echo "game process $(adb shell pidof $pkg:game | tr -d '\r' || true)"
    ;;
  shot)
    name="${2:-$(date +%H%M%S)}"
    mkdir -p "$shots"
    at=0
    while read -r id unique; do
      [ "$at" = 0 ] && out="$shots/$name-top.png" || out="$shots/$name-lower.png"
      [ "$at" -gt 1 ] && out="$shots/$name-$id.png"
      if [ -n "$unique" ]; then adb exec-out screencap -d "$unique" -p > "$out"; else adb exec-out screencap -p > "$out"; fi
      echo "$(cd "$(dirname "$out")" && pwd)/$(basename "$out")"
      at=$((at + 1))
    done < <(displays)
    ;;
  key)
    shift
    for name in "$@"; do adb shell input keyevent "$(keycode "$name")"; done
    ;;
  hold)
    adb shell input keyevent --longpress "$(keycode "$2")"
    ;;
  tap)
    if [ "${4:-}" = lower ]; then adb shell input -d "$(displays | awk 'NR==2{print $1}')" tap "$2" "$3"; else adb shell input tap "$2" "$3"; fi
    ;;
  swipe)
    shift
    where=()
    [ "${*: -1}" = lower ] && { where=(-d "$(displays | awk 'NR==2{print $1}')"); set -- "${@:1:$#-1}"; }
    adb shell input "${where[@]}" swipe "$@"
    ;;
  start)
    adb shell monkey -p $pkg -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    ;;
  game)
    pid="$(adb shell pidof $pkg:game | tr -d '\r' || true)"
    [ -n "$pid" ] || { echo "no game in play"; exit 0; }
    log="$(adb logcat -d --pid="$pid")"
    grep -E "LOADER\] [A-Z]{4}[0-9]|\[Reader\] profile|Reader\] Game" <<<"$log" | cut -c32- | head -4
    grep -E "pace:" <<<"$log" | cut -c32- | tail -1
    ;;
  log)
    pid="$(adb shell pidof $pkg:game | tr -d '\r' || true)"
    [ -n "$pid" ] || { echo "no game in play"; exit 0; }
    adb logcat -d --pid="$pid" | grep -v -E "core: \[|chatty" | grep -E "${2:-.}" | tail -80
    ;;
  install)
    apk="${2:-$here/../app/build/outputs/apk/debug/app-debug.apk}"
    if [ -n "$(adb shell pidof $pkg:game | tr -d '\r' || true)" ] && [ -n "$(adb shell run-as $pkg cat files/playing.json 2>/dev/null || true)" ]; then
      echo "A game is in play: leave it first (its menu, Back to Furoshiki)."; exit 1
    fi
    was_home="$(adb shell cmd package resolve-activity --brief -c android.intent.category.HOME -a android.intent.action.MAIN | tail -1 | tr -d '\r')"
    adb install -r "$apk"
    # An update makes Android forget that Furoshiki was its home app: chosen again.
    case "$was_home" in $pkg/*)
      # (a moment after the install, or the choice is not kept; then checked)
      for try in 1 2 3; do
        sleep 2
        adb shell cmd package set-home-activity $pkg/.ConsoleHome >/dev/null
        adb shell cmd package resolve-activity --brief -c android.intent.category.HOME -a android.intent.action.MAIN | grep -q "$pkg/" && break
      done
      adb shell input keyevent KEYCODE_HOME ;;
    esac
    adb shell appops set $pkg MANAGE_EXTERNAL_STORAGE allow
    adb shell dumpsys package $pkg | awk -F= '/versionName/{print "installed " $2; exit}'
    ;;
  home)
    case "${2:-}" in
      on)
        # What the app's own Settings row does (Home screen), and Android's choice of home app made here.
        # (adb may not enable an app's components: the app is asked to, as its row does.)
        adb shell am start -n $pkg/.HomeActivity -a $pkg.HOME_SCREEN --ez on true >/dev/null
        sleep 2
        adb shell cmd package set-home-activity $pkg/.ConsoleHome
        # The brightness from the System page (Android's "Modify system settings").
        adb shell appops set $pkg WRITE_SETTINGS allow
        adb shell input keyevent KEYCODE_HOME
        ;;
      off)
        launcher="$(adb shell cmd package query-activities --brief -a android.intent.action.MAIN -c android.intent.category.HOME | tr -d '\r' | awk '/\//{print $1}' | grep -v "^$pkg/" | head -1)"
        [ -n "$launcher" ] && adb shell cmd package set-home-activity "$launcher"
        adb shell am start -n $pkg/.HomeActivity -a $pkg.HOME_SCREEN --ez on false >/dev/null
        sleep 2
        adb shell appops set $pkg WRITE_SETTINGS default
        adb shell input keyevent KEYCODE_HOME
        echo "home app: ${launcher:-as Android chooses}"
        ;;
      *) echo "tools/thor.sh home on|off"; exit 2 ;;
    esac
    ;;
  connect)
    # Without the cable: Android's wireless debugging, which trusts this computer once it was allowed over
    # USB. Its port changes when the device restarts: every one from 30000 to 49999 is asked, the one found kept.
    host="${2:-$(cat "$here/../private/thor.host" 2>/dev/null || true)}"
    [ -n "$host" ] || { echo "tools/thor.sh connect <the device's address> (kept in android/private/thor.host from then on)"; exit 2; }
    mkdir -p "$here/../private"; echo "$host" > "$here/../private/thor.host"
    port="$(cat "$port_file" 2>/dev/null || echo 5555)"
    if ! "$adb_bin" connect "$host:$port" 2>&1 | grep -q "^connected\|already connected"; then
      port="$(python3 -I - "$host" <<'PY'
import socket, sys
from concurrent.futures import ThreadPoolExecutor
host = sys.argv[1]
def open_(p):
    s = socket.socket(); s.settimeout(1.5)
    try:
        s.connect((host, p)); return p
    except OSError:
        return None
    finally:
        s.close()
with ThreadPoolExecutor(200) as pool:
    found = [p for p in pool.map(open_, [5555] + list(range(30000, 50000))) if p]
print(found[0] if found else "")
PY
)"
      [ -n "$port" ] || { echo "No debugging port answers at $host: is wireless debugging on?"; exit 1; }
      echo "$port" > "$port_file"
      "$adb_bin" connect "$host:$port"
    fi
    echo "DEVICE=$host:$port tools/thor.sh status"
    ;;
  *)
    sed -n 2,17p "$0"; exit 2 ;;
esac
