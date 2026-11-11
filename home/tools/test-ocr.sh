#!/usr/bin/env bash
# A window read by OCR, from end to end, without a display (docs/furoshiki-3.md, "A window read
# by OCR"): a throwaway copy of Furochibi's service (the OCR helper) on a loopback port, with a
# pushed picture source and a token made for this run; a stand-in window that shows Furochibi's
# fake game (tools/fake_window.py); the program, worked by a page script, goes through the
# wizard (door B: the window, its text's area drawn with the pointer, the test read, its name),
# reads the title, looks a word up in the strip. The reader's database must hold the fake
# game's lines, and the pictures show each step.
#   tools/test-ocr.sh [PORT]              (default 18433; never a deployed service's port)
#   tools/test-ocr.sh --local-helper [PORT]   (default 18435, and the port after it)
# With --local-helper nothing is started here and no helper is set: a helper is "installed" in a
# folder (Furochibi's helper role with this machine's Python, a helper.json as
# ../furochibi/windows/helper/install.ps1 writes one) that READER_HOME_OCR_HELPER names, and the
# program must find it at door B with nothing typed, start it, read with it, and stop it as it
# quits; then the program is started again with the title (--lines ID) and must start it again.
# FUROCHIBI_BUILT names the Furochibi checkout whose .venv, dist/reader-bridge and build/state/hf
# (the OCR model) are used: by default the one beside this folder; in a worktree, the main
# checkout's. READER_OCR_DICT names a reader_dict.db for the lookup (a copy is used).
# READER_OCR_THEME names the shelf's theme for the pictures (mono, paper, kon, solarized).
# The stand-in window is given a place on the stand-in screen (READER_HOME_FAKE_WINDOW_PLACE),
# so that the layer over the window is made too and a word is looked up through it.
# Pictures: private/ocr-test/*.png (private/ocr-test-<theme> with a theme named).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
furochibi="$(cd "$root/../furochibi" && pwd)"
built="${FUROCHIBI_BUILT:-$furochibi}"
local_helper=0
if [ "${1:-}" = "--local-helper" ]; then local_helper=1; shift; fi
port="${1:-$([ "$local_helper" = 1 ] && echo 18435 || echo 18433)}"
case "$port" in 6677|8642|8788|8794|8789|8100) echo "test-ocr: $port is a deployed service's port" >&2; exit 1 ;; esac
if [ -x /opt/homelab/scripts/verify/check-port.sh ]; then
  /opt/homelab/scripts/verify/check-port.sh --port "$port" --service furoshiki-ocr-test --protocol tcp >/dev/null
fi
py="$built/.venv/bin/python"
for needed in "$py" "$built/dist/reader-bridge/reader-bridge" "$built/build/state/hf"; do
  [ -e "$needed" ] || { echo "test-ocr: $needed is missing (FUROCHIBI_BUILT: a Furochibi checkout that was built)" >&2; exit 1; }
done
theme="${READER_OCR_THEME:-}"
home="$root/private/ocr-test$([ "$local_helper" = 1 ] && echo -helper || true)${theme:+-$theme}"
rm -rf "$home"
mkdir -p "$home/data/program" "$home/config" "$home/state"
if [ -n "${READER_OCR_DICT:-}" ]; then
  cp "$READER_OCR_DICT" "$home/data/reader_dict.db"
fi
# A Unix socket's path is short: the engine's is in a folder of its own.
sockets="$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/furoshiki-ocr.XXXXXX")"
token="$("$py" -I -c 'import secrets; print(secrets.token_urlsafe(24))')"
helper_folder="$home/FurochibiHelper"
if [ "$local_helper" = 1 ]; then
  if [ -x /opt/homelab/scripts/verify/check-port.sh ]; then
    /opt/homelab/scripts/verify/check-port.sh --port "$((port + 1))" --service furoshiki-ocr-test --protocol tcp >/dev/null
  fi
  mkdir -p "$helper_folder"
  "$py" -I - "$helper_folder" "$port" "$token" "$py" "$furochibi/server" "$built/build/state/hf" <<'PY'
import json, pathlib, sys
folder, port, token, py, server, hf = pathlib.Path(sys.argv[1]), int(sys.argv[2]), *sys.argv[3:]
(folder / "config.json").write_text(json.dumps({
    "role": "helper", "listen_host": "127.0.0.1", "listen_port": port, "state_dir": str(folder / "state"),
    "sources": [{"id": "furoshiki", "name": "Furoshiki: a window", "push": True, "token": token}],
    "grace_seconds": 30}, indent=1), encoding="utf-8")
(folder / "helper.json").write_text(json.dumps({
    "format": 1, "name": "Furochibi OCR helper", "address": f"http://127.0.0.1:{port}", "source": "furoshiki",
    "token": token,
    "program": [py, "-m", "console_ocr", "helper", "--config", "config.json", "--stop-with-stdin"],
    "env": {"PYTHONPATH": server, "HF_HOME": hf, "HF_HUB_OFFLINE": "1", "LOGURU_COLORIZE": "False",
            "HELPER_FOLDER_IS": "{folder}"}}, indent=1), encoding="utf-8")
PY
fi
cat > "$home/config.json" <<JSON
{"listen_host": "127.0.0.1", "listen_port": $port,
 "feeder_host": "127.0.0.1", "feeder_port": 9, "screen_port": 9, "screen_video_port": 0,
 "desk_source": false, "jellyfin_url": "",
 "sources": [{"id": "furoshiki", "name": "Furoshiki: a window", "push": true, "token": "$token"}],
 "state_dir": "$home/state", "engine_socket": "$sockets/engine.sock",
 "reader_bridge": "$built/dist/reader-bridge/reader-bridge", "grace_seconds": 30}
JSON
pids=()
trap 'kill "${pids[@]}" 2>/dev/null || true; rm -rf "$sockets"' EXIT
export PYTHONPATH="$furochibi/server" HF_HOME="$built/build/state/hf" LOGURU_COLORIZE=False
if [ "$local_helper" != 1 ]; then
"$furochibi/tools/limit.sh" -m 3G -t 600 "$py" -m console_ocr engine --socket "$sockets/engine.sock" > "$home/engine.log" 2>&1 & pids+=($!)
"$furochibi/tools/limit.sh" -m 2G -t 600 "$py" -m console_ocr serve --config "$home/config.json" > "$home/service.log" 2>&1 & pids+=($!)
fi
# The fake game in a "window": three of its lines, again and again.
"$furochibi/tools/limit.sh" -m 1G -t 600 "$py" "$here/fake_window.py" "$home/window.jpg" --lines 3 --gap 0.6 > "$home/window.log" 2>&1 & pids+=($!)
unset PYTHONPATH HF_HOME
ready=0
for _ in $(seq 240); do
  if [ "$local_helper" = 1 ]; then
    # Nothing answers there, and nothing may: the program starts the helper itself.
    if "$py" -I -c 'import socket, sys; s = socket.socket(); sys.exit(0 if s.connect_ex(("127.0.0.1", int(sys.argv[1]))) else 1)' "$port"; then
      [ -s "$home/window.jpg" ] && { ready=1; break; }
    else echo "test-ocr: something answers at port $port already" >&2; exit 1; fi
  elif "$py" -I - "$port" <<'PY' 2>/dev/null
import json, sys, urllib.request
engines = json.load(urllib.request.urlopen(f"http://127.0.0.1:{sys.argv[1]}/api/engines", timeout=2))
sys.exit(0 if any(engine["loaded"] for engine in engines) else 1)
PY
  then ready=1; break; fi
  sleep 0.5
done
[ "$ready" = 1 ] && [ -s "$home/window.jpg" ] || { echo "test-ocr: the helper or the fake window did not start; see $home/*.log" >&2; exit 1; }

# The picture stands at (40, 200) on the page's sheet, 770 wide (wizard_page.inc); the fake
# game's dialogue box is at 160,760 of 1920x1080, 1600 by 270.
# With a helper to start, door B waits one more round of the fake game's three lines (15.89 s):
# the helper has started by then. The game is not where it is otherwise (nothing was waited for
# before the program started), so the strip's lookup is tried twice: a new line closes a popup,
# and one of two tries 2.4 s apart has a line to itself.
cat > "$home/ocr.script" <<X
wait 400
${theme:+shelf-theme $theme}
$([ "$local_helper" = 1 ] && echo "wait 10" || echo "ocr-helper http://127.0.0.1:$port furoshiki $token")
wizard
wait 300
key Right
wait 200
key Enter
wait $([ "$local_helper" = 1 ] && echo 17690 || echo 1800)
grab $home/1-windows.png
key Enter
wait 2500
grab $home/2-area.png
drag 104 505 746 613
wait 3000
grab $home/3-area-read.png
key Enter
wait 400
grab $home/4-name.png
key Enter
wait 900
grab $home/5-shelf.png
key Enter
wait 300
key Enter
wait 26000
ocr-say
strip-say
screen $home/6-strip.png
strip-lookup 0
wait 1800
strip-say
screen $home/7-lookup.png
$([ "$local_helper" = 1 ] && printf 'wait 600\nstrip-lookup 0\nwait 1800\nstrip-say\nscreen %s/7-lookup-again.png' "$home" || echo "wait 10")
overlay-lookup 3
wait 1300
ocr-say
wait 600
overlay-lookup 3
wait 1300
ocr-say
wait 600
overlay-lookup 3
wait 1300
ocr-say
wait 600
overlay-lookup 3
wait 1300
ocr-say
wait 600
screen $home/7b-overlay.png
lines-stop
wait 900
key Enter
wait 400
grab $home/8-stage.png
key Down
wait 400
grab $home/9-log.png
quit
X
# The development channel's variable makes this a test run: no first-start questions, and the
# program's own text server stays off whatever its setting says.
if [ "$local_helper" = 1 ]; then
  # The helper is the program's child, in its memory scope: its engine needs room.
  export READER_HOME_OCR_HELPER="$helper_folder" READER_TEST_MEMORY_MAX="${READER_TEST_MEMORY_MAX:-4G}"
fi
READER_HOME_FAKE_WINDOWS="森の物語=$home/window.jpg" READER_HOME_FAKE_WINDOW_PLACE=0,0,800,450 READER_HOME_DIR="$home/data" XDG_CONFIG_HOME="$home/config" \
  READER_DESKTOP_CONTROL="$home/sock" \
  "$here/run-desktop.sh" reader-home --window --dry-run --script "$home/ocr.script" > "$home/program.log" 2>&1 || {
    echo "test-ocr: the program failed; see $home/program.log" >&2; exit 1; }
if [ "$local_helper" = 1 ]; then
  cp "$helper_folder/logs/furoshiki.log" "$home/helper-first.log" 2>/dev/null || true
  # The program again, with the title at once: it must start the helper again by itself.
  id="$("$py" -I -c 'import json, sys; print(next(t["id"] for t in json.load(open(sys.argv[1], encoding="utf-8"))["titles"] if t["by"] == "ocr"))' "$home/data/program/sources.json")"
  printf 'wait 14000\nocr-say\nquit\n' > "$home/again.script"
  READER_HOME_FAKE_WINDOWS="森の物語=$home/window.jpg" READER_HOME_DIR="$home/data" XDG_CONFIG_HOME="$home/config" \
    READER_DESKTOP_CONTROL="$home/sock" \
    "$here/run-desktop.sh" reader-home --window --dry-run --lines "$id" --script "$home/again.script" > "$home/program-again.log" 2>&1 || {
      echo "test-ocr: the program failed the second time; see $home/program-again.log" >&2; exit 1; }
fi
"$py" -I - "$home" "$port" "$token" "$local_helper" <<'PY'
import difflib, json, pathlib, socket, sqlite3, sys, urllib.request
home, port, token, local_helper = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4] == "1"
problems = []
sources = json.loads((home / "data/program/sources.json").read_text(encoding="utf-8"))
titles = [t for t in sources["titles"] if t["by"] == "ocr"]
if len(titles) != 1 or titles[0]["window"] != "森の物語" or titles[0]["title"] != "森の物語":
    problems.append(f"the wizard's title: {sources['titles']}")
area = titles[0]["area"] if titles else [0, 0, 0, 0]
want_area = [160 / 1920, 760 / 1080, 1600 / 1920, 270 / 1080]
if any(abs(a - b) > 0.02 for a, b in zip(area, want_area)):
    problems.append(f"the area drawn: {area}, the dialogue box is {want_area}")
shown = [line[5:].strip() for line in (home / "window.log").read_text(encoding="utf-8").splitlines() if line.startswith("LINE ")]
want = list(dict.fromkeys(shown))
db = sqlite3.connect(home / "data/program/reader/reader.db")
rows = db.execute("SELECT text, chars FROM lines WHERE game = ? ORDER BY id", (titles[0]["id"] if titles else "",)).fetchall()
like = lambda a, b: difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()
for text in want:
    if not any(like(text, got) > 0.9 for got, _ in rows):
        problems.append(f"not read: {text}")
for got, _ in rows:
    if not any(like(text, got) > 0.9 for text in want):
        problems.append(f"read, but the game never showed it: {got}")
if len(want) < 3:
    problems.append(f"the fake game showed only {want}")
if not any(counted for _, counted in rows):
    problems.append("no line was counted")
app = json.loads((home / "data/apps/reader-home-lines.json").read_text(encoding="utf-8"))
chars = sum(day["chars"] for day in app["stats"].get("days", []))
if chars <= 0:
    problems.append("no characters in the lines' file of the page")
if local_helper:
    # The helper the program found, started and stopped: nothing typed, its folder kept as the
    # device's setting; started in its folder with its file's environment; gone with the program
    # (both times), by its standard input's end; its lines say the source and were not counted.
    folder = home / "FurochibiHelper"
    said = json.loads((folder / "helper.json").read_text(encoding="utf-8"))
    ocr = sources.get("ocr", {})
    if (ocr.get("address"), ocr.get("source"), ocr.get("token"), ocr.get("program")) != (said["address"], "furoshiki", token, str(folder)):
        problems.append(f"the device's setting for the helper: { {k: v for k, v in ocr.items() if k != 'token'} }")
    for which, name in (("the first time", "helper-first.log"), ("the second time", "FurochibiHelper/logs/furoshiki.log")):
        text = (home / name).read_text(encoding="utf-8", errors="replace") if (home / name).exists() else ""
        if "the OCR helper answers at" not in text:
            problems.append(f"{which}, the helper was not started by the program ({name})")
        if "the program that started the helper has gone" not in text:
            problems.append(f"{which}, the helper did not end with the program ({name})")
    for at in (int(port), int(port) + 1):
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", at)) == 0:
                problems.append(f"something still answers at port {at} after the program quit")
    state = sqlite3.connect(folder / "state/console-ocr.db")
    helper = [{"source": source, "not_counted": bool(nc)} for source, nc in state.execute("SELECT source, not_counted FROM lines")]
    again = (home / "program-again.log").read_text(encoding="utf-8", errors="replace")
    said_again = [line.strip() for line in again.splitlines() if "ocr:" in line]
    if not any("ocr: reading" in line and "taken 0" not in line for line in said_again):
        problems.append(f"started again with the title, the program did not read: {said_again}")
    print("the helper was found, started and stopped by the program, twice;", *said_again)
else:
    # The helper: every line of this run says its source, none was counted there, the token is told nowhere.
    helper = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/api/lines?since=0&limit=100", timeout=5))["lines"]
    for path in ("/api/source", "/api/status", "/api/regions"):
        if token in urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=5).read().decode():
            problems.append(f"{path} tells the token")
if not helper or any(line["source"] != "furoshiki" or not line["not_counted"] for line in helper):
    problems.append(f"the helper's lines: {[(l['source'], l['not_counted']) for l in helper]}")
log = (home / "program.log").read_text(encoding="utf-8", errors="replace")
print(f"the fake game showed {len(want)} lines; in the reader's database {len(rows)}: {[text for text, _ in rows]}")
print(f"the helper kept {len(helper)} lines; characters in the page's file: {chars}; area {area}")
for line in log.splitlines():
    if "strip:" in line or "ocr:" in line:
        print(line.strip())
if (home / "data/reader_dict.db").exists():
    if not any("strip:" in line and "popup open" in line for line in log.splitlines()):
        problems.append("the strip's lookup did not open its popup")
    # The line stands on the screen some 3.6 s of every 5.3: one of the four tries finds it there.
    if not any("ocr:" in line and "overlay shown, popup open" in line for line in log.splitlines()):
        problems.append("the layer over the window did not look a word up")
for problem in problems:
    print("FAILED", problem)
sys.exit(1 if problems else 0)
PY
echo "test-ocr: passed; pictures in $home"
