#!/usr/bin/env bash
# Stops the desktop app that tools/reader/desktop.sh started with the same home
# (READER_DESKTOP_HOME, private/desktop by default), and nothing else: another session's run
# with a home of its own goes on. The stand-in screen stays for the next run.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
home="$(realpath -m "${READER_DESKTOP_HOME:-$root/private/desktop}")"
for exe in /proc/[0-9]*/exe; do
  target="$(readlink "$exe" 2>/dev/null || true)"
  case "$target" in
    "$home"/app/station-reader.*)
      pid="${exe#/proc/}"; pid="${pid%/exe}"
      kill "$pid" 2>/dev/null && echo "desktop-stop: stopped $pid"
      ;;
  esac
done
