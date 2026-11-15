#!/usr/bin/env bash
# Start the headless harness with a game, under a memory limit (docs/reader/harness.md).
#   tools/reader/harness.sh [--rom FILE] [harness arguments...] &
#   tools/reader/hc.py "run 600" "shot private/harness/shots/a.png"
# Home is private/harness (its saves and system folders); it talks on private/harness/sock.
# READER_HARNESS_ROTATION=none|left|right|auto turns the picture (auto: as the game is held).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
home="${READER_HARNESS_HOME:-$root/private/harness}"
rom="${READER_GAME:-$root/private/game.nds}"
rest=()
while [ $# -gt 0 ]; do
  case "$1" in
    --rom) rom="$2"; shift 2 ;;
    *) rest+=("$1"); shift ;;
  esac
done
[ -x "$root/build-harness/bin/melonds-reader-harness" ] || {
  echo "harness.sh: the harness is not built yet: tools/reader/build-core.sh --harness" >&2; exit 1; }
[ -e "$rom" ] || { echo "harness.sh: no game at $rom: name yours with --rom FILE" >&2; exit 1; }
mkdir -p "$home"
socket=(--socket "$home/sock")
for arg in "${rest[@]}"; do
  case "$arg" in --socket) socket=() ;; esac
done
# Run a copy: the build replaces the binary in place, which takes a running harness down.
binary="$home/melonds-reader-harness.$$"
cp "$root/build-harness/bin/melonds-reader-harness" "$binary"
trap 'rm -f "$binary"' EXIT
"$here/limit.sh" -m "${READER_HARNESS_MEMORY_MAX:-3G}" \
  "$binary" --home "$home" --rom "$rom" "${socket[@]}" "${rest[@]}"
