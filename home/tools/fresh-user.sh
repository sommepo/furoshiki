#!/usr/bin/env bash
# Run Furoshiki as a new user would see it: on the stand-in screen (desktop.sh), in a home
# folder made empty for the run. No settings, dictionaries, Anki setup, deck or note type of the
# maintainer's, no save sync and no game scripts but what the program installs itself. Use it
# for every check of what a user meets (docs/maintainer.md, "Test as a new user").
#   tools/fresh-user.sh [--bios] [--keep] [arguments for desktop.sh...] &
# --bios copies the PlayStation and PS2 BIOS files into the new home, from the test home named by
# FRESH_USER_BIOS_FROM (private/agent-test by default)
# (a user puts their own there before a PS1 or PS2 game starts; PSP and Dreamcast need none).
# --keep leaves the home after the run (its path is printed); without it the home is removed
# when Furoshiki quits. The control channel is at <home>/sock, as with desktop.sh.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
bios=0
keep=0
while [ $# -gt 0 ]; do
  case "$1" in
    --bios) bios=1; shift ;;
    --keep) keep=1; shift ;;
    *) break ;;
  esac
done
mkdir -p "$root/private/fresh-user"
home="$(mktemp -d "$root/private/fresh-user/run-XXXXXX")"
echo "fresh-user: home $home" >&2
if [ "$bios" = 1 ]; then
  for console in ps1 ps2; do
    from="${FRESH_USER_BIOS_FROM:-$root/private/agent-test}/data/reader-home/program/consoles/$console/system"
    to="$home/data/reader-home/program/consoles/$console/system"
    if [ -d "$from" ]; then
      mkdir -p "$to"
      find "$from" -maxdepth 1 -type f -iname '*.bin' -exec cp {} "$to/" \;
    fi
  done
fi
cleanup() {
  if [ "$keep" = 1 ]; then
    echo "fresh-user: kept $home" >&2
  else
    # desktop.sh leaves its stand-in screen running for the next run in the same home; this
    # home goes, so its screen (Weston, with its Xwayland and helpers) goes too.
    for pid in $(pgrep -u "$(id -u)" -f "weston.*--log=$home/" || true); do
      pkill -P "$pid" || true
      kill "$pid" || true
    done
    rm -rf -- "$home"
  fi
}
trap cleanup EXIT
READER_DESKTOP_HOME="$home" READER_DESKTOP_CONTROL="${READER_DESKTOP_CONTROL:-$home/sock}" "$here/desktop.sh" "$@"
