#!/usr/bin/env bash
# The stretch test of a wide picture (reader/core_library/wide_patches.cpp): the same moments of a
# game at its own 4:3 and at a wide shape, in Furoshiki on its stand-in screen, and how well the
# middle of the wide picture matches the 4:3 one (SSIM: near 0.9 when the wide picture is the 4:3
# one with more at the sides; near 0.3 when it is stretched). A recipe is checked when it passes.
#   tools/reader/wide-check.sh GAME.iso SHAPE "SLOTS" [TAG]
#   e.g. tools/reader/wide-check.sh private/ffx/ffx.iso 32:9 "5 1"
# Needs Furoshiki built (../home/tools/build-desktop.sh, or READER_HOME_BUILD naming a folder with
# reader-home in it), this fork's core (tools/reader/build-core.sh), the console's BIOS and the
# game's savestates (Furoshiki's own, "SERIAL (CRC).NN.p2s") in the test home's
# data/reader-home/program/consoles/ps2/{system,states}. The test home is READER_DESKTOP_HOME
# (private/wide-check by default); the pictures and a side-by-side of each slot go in its shots/.
# READER_WIDE_TRY passes words being tried to the core (WidePatches::Pnach); SKIP_NATIVE=1 reuses
# the 4:3 pictures of an earlier run.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
home_root="$root/../home"
iso="$(realpath "$1")"; shape="$2"; slots="$3"; tag="${4:-$(basename "$iso" | tr -c 'A-Za-z0-9\n' '_')}"
test_home="${READER_DESKTOP_HOME:-$root/private/wide-check}"
data="$test_home/data/reader-home/program"
cores="$test_home/cores"
mkdir -p "$cores" "$data/consoles/ps2/system" "$data/consoles/ps2/states" "$test_home/shots"
ln -sfn "$root/build-core/bin/reader_core_pcsx2.so" "$cores/reader_core_pcsx2.so"
ln -sfn "$root/bin" "$cores/pcsx2"
dc=("python3" "$here/dc.py" "--socket" "$test_home/sock")

stop() {
  for proc in /proc/[0-9]*; do
    case "$(readlink "$proc/exe" 2>/dev/null || true)" in
      "$test_home"/app/reader-home.*) kill "${proc#/proc/}" 2>/dev/null || true ;;
    esac
  done
  sleep 3
}

# One run of Furoshiki at a shape: each slot loaded with the game at 1 % of its speed (so that
# every run grabs the same frame) and the window grabbed.
shots() {
  local wide="$1" name="$2"
  printf '{ "format": 1, "consoles": { "ps2": { "ps2.wide": "%s" } } }\n' "$wide" > "$data/settings.json"
  rm -f "$test_home/sock"
  READER_DESKTOP_HOME="$test_home" READER_HOME_CORES="$cores" READER_DESKTOP_MEMORY_MAX="${READER_DESKTOP_MEMORY_MAX:-6G}" \
    nice -n 5 "$home_root/tools/desktop.sh" --play "$iso" > "$test_home/run-$name.log" 2>&1 &
  for _ in $(seq 1 60); do
    [ -S "$test_home/sock" ] && "${dc[@]}" status > /dev/null 2>&1 && break
    sleep 3
  done
  sleep 25
  "${dc[@]}" "speed 1" > /dev/null
  for slot in $slots; do
    "${dc[@]}" "state load $slot" > /dev/null
    sleep 6
    # The picture is written with a later frame, which is slow at 1 %: asked again if it is not there.
    picture="$test_home/shots/$tag-$name-$slot.png"
    rm -f "$picture"
    for _ in 1 2 3; do
      "${dc[@]}" "grab-window $picture" > /dev/null || true
      sleep 5
      [ -s "$picture" ] && break
    done
  done
  stop
}

stop
# SKIP_NATIVE=1: the 4:3 pictures of an earlier run are used again.
[ -n "${SKIP_NATIVE:-}" ] || shots off native
shots "$shape" "${shape/:/x}"
# The window is 1280x720 on the stand-in screen: 4:3 pillarboxed, a wider shape letterboxed.
w="${shape%%:*}"; h="${shape##*:}"
[ "$shape" = "21:9" ] && { w=64; h=27; }
pic_h=$(( 1280 * h / w )); [ "$pic_h" -gt 720 ] && pic_h=720
pic_y=$(( (720 - pic_h) / 2 )); mid_w=$(( pic_h * 4 / 3 ))
for slot in $slots; do
  native="$test_home/shots/$tag-native-$slot.png"; wide="$test_home/shots/$tag-${shape/:/x}-$slot.png"
  ssim=$(ffmpeg -i "$native" -i "$wide" -filter_complex \
    "[0]crop=960:720:160:0,scale=480:360[a];[1]crop=$mid_w:$pic_h:$(( (1280 - mid_w) / 2 )):$pic_y,scale=480:360[b];[a][b]ssim" \
    -f null - 2>&1 | grep -o 'All:[0-9.]*' | cut -d: -f2)
  ffmpeg -v error -y -i "$native" -i "$wide" -filter_complex \
    "[0]crop=960:720:160:0,scale=320:240,pad=1280:250:480:5:color=0x002b36[a];[1]crop=1280:$pic_h:0:$pic_y,scale=-2:240,pad=1280:250:(ow-iw)/2:5:color=0x002b36[b];[a][b]vstack=2" \
    "$test_home/shots/$tag-${shape/:/x}-$slot-compare.png"
  verdict=$(awk -v s="$ssim" 'BEGIN { print (s >= 0.75 ? "widened" : "STRETCHED") }')
  echo "slot $slot at $shape: SSIM $ssim ($verdict)  $test_home/shots/$tag-${shape/:/x}-$slot-compare.png"
done
