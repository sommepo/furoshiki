#!/usr/bin/env bash
# Stop the desktop app that tools/reader/desktop.sh started (and only that: the copy of the
# program it runs from private/desktop), and take the copy away.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
home="${READER_DESKTOP_HOME:-$root/private/desktop}"
for proc in /proc/[0-9]*; do
  exe="$(readlink "$proc/exe" 2>/dev/null || true)"
  case "$exe" in
    "$home"/flycast-reader.*) kill -KILL "${proc#/proc/}" 2>/dev/null || true ;;
  esac
done
sleep 0.5
rm -f "$home"/flycast-reader.[0-9]* "$home/sock"
