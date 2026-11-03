#!/usr/bin/env bash
# Stop the desktop app started with tools/reader/desktop.sh or screen.sh for one home
# (READER_DESKTOP_HOME, private/desktop unless set): by the file the process runs, not by a
# pattern (a pattern with the program's path in it matches the shell as well), and only the
# process whose control socket is in that home. Another run, started for another home (an
# agent beside you, a second test), is left alone.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
home="${READER_DESKTOP_HOME:-$root/private/desktop}"
copies=()
for proc in /proc/[0-9]*; do
  exe="$(readlink "$proc/exe" 2>/dev/null || true)"
  case "$exe" in
    "$root"/*/bin/pcsx2-reader.*) ;;
    *) continue ;;
  esac
  if tr '\0' '\n' < "$proc/environ" 2>/dev/null | grep -qx "READER_DESKTOP_CONTROL=$home/sock"; then
    kill -KILL "${proc#/proc/}" 2>/dev/null || true
    copies+=("$exe")
  fi
done
sleep 0.5
rm -f "${copies[@]}" "$home/sock"
