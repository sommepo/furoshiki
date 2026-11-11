#!/usr/bin/env bash
# A title read from lines, from end to end, without a display (docs/furoshiki-3.md, "Sources"):
# a throwaway WebSocket server on a loopback port (tools/lines_server.py) sends lines as a
# texthooker does; the program, worked by a page script, adds a title read by WebSocket, reads
# it, looks a word up in the text strip, then a title read from the clipboard; the reader's
# database must hold the lines, and the pictures show the shelf, the strip, the stage and the Log.
#   tools/test-lines.sh [PORT]            (default 8431; never a deployed service's port)
# READER_LINES_DICT names a reader_dict.db for the lookup (a copy is used); without one the
# lookup finds nothing and only the lines are checked. Pictures: private/lines-test/*.png.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
port="${1:-8431}"
case "$port" in 6677|8642|8794|8789|8100) echo "test-lines: $port is a deployed service's port" >&2; exit 1 ;; esac
if [ -x /opt/homelab/scripts/verify/check-port.sh ]; then
  /opt/homelab/scripts/verify/check-port.sh --port "$port" --service furoshiki-lines-test --protocol tcp >/dev/null
fi
home="$root/private/lines-test"
rm -rf "$home"
mkdir -p "$home/data/program" "$home/config"
if [ -n "${READER_LINES_DICT:-}" ]; then
  cp "$READER_LINES_DICT" "$home/data/reader_dict.db"
fi
cat > "$home/lines.script" <<X
wait 400
lines-address ws://127.0.0.1:$port
lines-add websocket 雨の山道
wait 300
grab $home/1-shelf.png
key Enter
wait 300
key Enter
wait 4200
screen $home/2-strip.png
strip-say
strip-lookup 0
wait 1800
strip-say
screen $home/3-lookup.png
lines-stop
wait 700
key Enter
wait 400
grab $home/4-stage.png
key Down
wait 400
grab $home/5-log.png
key Escape
key Up
lines-add clipboard 写した文
wait 300
key Right
key Enter
wait 300
key Enter
wait 500
clipboard 写した一行目の文です。
wait 700
clipboard copied English is no line
wait 700
clipboard 二行目も写した。
wait 900
screen $home/6-clipboard.png
lines-stop
wait 700
quit
X
python3 -I "$here/lines_server.py" "$port" > "$home/server.log" 2>&1 &
server=$!
trap 'kill $server 2>/dev/null || true' EXIT
for _ in $(seq 50); do grep -q "lines server" "$home/server.log" && break; sleep 0.1; done
# The development channel's variable makes this a test run: no first-start questions, and the
# program's own text server stays off whatever its setting says.
READER_HOME_DIR="$home/data" XDG_CONFIG_HOME="$home/config" READER_DESKTOP_CONTROL="$home/sock" \
  "$here/run-desktop.sh" reader-home --window --dry-run --script "$home/lines.script" > "$home/program.log" 2>&1 || {
    echo "test-lines: the program failed; see $home/program.log" >&2; exit 1; }
python3 -I - "$home" <<'PY'
import json, pathlib, sqlite3, sys
home = pathlib.Path(sys.argv[1])
db = sqlite3.connect(home / "data/program/reader/reader.db")
rows = db.execute("SELECT game, text, chars FROM lines ORDER BY id").fetchall()
sources = json.loads((home / "data/program/sources.json").read_text(encoding="utf-8"))
ids = {t["by"]: t["id"] for t in sources["titles"]}
got = {by: [(text, counted) for game, text, counted in rows if game == ids[by]] for by in ids}
want_ws = ["山道は細く、曲がりくねっている。", "助手席の奈美は、まだ眠っていた。", "雨が降り始めた。ワイパーを動かす。"]
want_clip = ["写した一行目の文です。", "二行目も写した。"]
problems = []
if [text for text, _ in got.get("websocket", [])] != want_ws:
    problems.append(f"websocket lines: {got.get('websocket')}")
if [text for text, _ in got.get("clipboard", [])] != want_clip:
    problems.append(f"clipboard lines: {got.get('clipboard')}")
if not all(counted for lines in got.values() for _, counted in lines):
    problems.append(f"a line not counted: {got}")
app = json.loads((home / "data/apps/reader-home-lines.json").read_text(encoding="utf-8"))
chars = sum(day["chars"] for day in app["stats"].get("days", []))
if chars <= 0:
    problems.append("no characters in the lines' file of the page")
log = (home / "program.log").read_text(encoding="utf-8", errors="replace")
print(f"lines by WebSocket: {len(got.get('websocket', []))}, from the clipboard: {len(got.get('clipboard', []))}, "
      f"characters in the page's file: {chars}")
for line in log.splitlines():
    if "strip:" in line:
        print(line.strip())
if (home / "data/reader_dict.db").exists() and "popup open" not in log:
    problems.append("the lookup's popup did not open")
for problem in problems:
    print("FAILED", problem)
sys.exit(1 if problems else 0)
PY
echo "test-lines: passed; pictures in $home"
