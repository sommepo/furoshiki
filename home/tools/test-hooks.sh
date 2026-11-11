#!/usr/bin/env bash
# Hooks started by Furoshiki, from end to end, without a display (docs/hooks.md; docs/
# furoshiki-3.md, 9). Through the wizard's door C, worked by a page script:
#   1. An Agent script: the scripts fetched (0xDC00's real ones, with the test's own script
#      added, from a file made here), the test's script chosen by searching, the stand-in game
#      (build-desktop/hook-target, a real process) chosen among the programs, hooked by the real
#      frida-inject with Agent's real runtime; its lines must reach the reader's database.
#   2. Textractor and 3. LunaHook, as stand-ins that write what the two Windows programs are
#      documented to write (tests/hooks/hook_standin.py): the thread chooser, the dialogue's
#      thread remembered, its lines in the database and no other thread's.
# Neither Textractor nor LunaHook is run: they are Windows programs.
#   tools/test-hooks.sh
# Fetched once into private/hooks-cache (not in Git): frida-inject for Linux (18.5 MB) and
# 0xDC00's scripts (1.5 MB). READER_HOOKS_DICT names a reader_dict.db for the strip's lookup;
# (the stand-in game goes on by itself, four lines and then four seconds of none: the word is
# looked up three times two seconds apart, and once at least while the line stands);
# READER_HOOKS_THEME another shelf theme (paper, kon, solarized). Pictures: private/hooks-test.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
home="$root/private/hooks-test"
cache="$root/private/hooks-cache"
target="$root/build-desktop/hook-target"
[ -x "$target" ] || { echo "test-hooks: build first (tools/build-desktop.sh): $target is not there" >&2; exit 1; }
mkdir -p "$cache"
frida="${READER_HOME_FRIDA_INJECT:-$cache/frida-inject-16.7.19}"
if [ ! -x "$frida" ]; then
  echo "test-hooks: fetching frida-inject 16.7.19 into $cache"
  curl -fsSL --retry 2 -o "$frida.xz" https://github.com/frida/frida/releases/download/16.7.19/frida-inject-16.7.19-linux-x86_64.xz
  xz -d -f "$frida.xz"
  chmod +x "$frida"
fi
if [ ! -f "$cache/scripts.zip" ]; then
  echo "test-hooks: fetching 0xDC00's scripts into $cache"
  curl -fsSL --retry 2 -o "$cache/scripts.zip" https://codeload.github.com/0xDC00/scripts/zip/refs/heads/main
fi
rm -rf "$home"
mkdir -p "$home/data/program" "$home/config" "$home/scripts/scripts-main"
# What the program fetches in the test: the authors' scripts as they are, and the test's own two.
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$cache/scripts.zip" "$home/scripts"
cp "$root/tests/hooks/TEST_Furoshiki_Hook_Target.js" "$root/tests/hooks/libFuroshikiTest.js" "$home/scripts/scripts-main/"
# A zip as GitHub makes it: the tree under scripts-main/, names in UTF-8 (four are not ASCII).
(cd "$home/scripts" && python3 -I -c 'import sys, os, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for top, _, files in os.walk("scripts-main"):
        for f in files:
            z.write(os.path.join(top, f))' "$home/scripts.zip")
rm -rf "$home/scripts"
if [ -n "${READER_HOOKS_DICT:-}" ]; then
  cp "$READER_HOOKS_DICT" "$home/data/reader_dict.db"
fi
theme="${READER_HOOKS_THEME:-mono}"
cat > "$home/hooks.script" <<X
wait 400
shelf-theme $theme
wizard
key Right
wait 200
grab $home/01-doors.png
key Enter
wait 400
wizard-window 雨の山道
wait 200
grab $home/02-games.png
key Enter
wait 3500
grab $home/03-how.png
key Down
wait 100
grab $home/04-how-search.png
key Up
key Enter
wait 4500
grab $home/08-agent-says.png
key Enter
wait 300
grab $home/09-name.png
key Enter
wait 9000
hook-say
screen $home/10-strip.png
strip-say
strip-lookup 0
wait 900
strip-say
screen $home/11-lookup.png
wait 1100
strip-lookup 0
wait 900
strip-say
screen $home/11-lookup-b.png
wait 1100
strip-lookup 0
wait 900
strip-say
screen $home/11-lookup-c.png
lines-stop
wait 700
key Enter
wait 400
grab $home/12-stage.png
key Down
wait 400
grab $home/13-log.png
key Escape
key Up
wizard
key Right
key Enter
wait 400
wizard-window 怪盗の夜
wait 200
key Enter
wait 600
grab $home/14-textractor-how.png
key Down
key Down
key Down
key Enter
wait 3200
grab $home/15-threads.png
key Down
key Down
wait 100
grab $home/16-thread-chosen.png
key Enter
wait 300
type 怪盗の夜
grab $home/17-textractor-name.png
key Enter
wait 6500
hook-say
screen $home/18-textractor-strip.png
lines-stop
wait 600
wizard
key Right
key Enter
wait 400
wizard-window 月の怪盗
wait 200
key Enter
wait 600
key Down
key Down
key Enter
wait 3200
key Down
key Space
key Up
key Up
key Space
wait 100
grab $home/19-luna-threads.png
key Space
key Enter
wait 300
type 月の怪盗
key Enter
wait 6500
hook-say
lines-stop
wait 600
quit
X
"$target" forever > "$home/target.log" 2>&1 &
game=$!
trap 'kill $game 2>/dev/null || true' EXIT
standin="python3|-I|$root/tests/hooks/hook_standin.py"
# The development channel's variable makes this a test run (no first-start questions, no text
# server). The programs "running": the stand-in game for real, and made-up ones for the lists.
READER_HOME_DIR="$home/data" XDG_CONFIG_HOME="$home/config" READER_DESKTOP_CONTROL="$home/sock" \
  READER_HOME_FRIDA_INJECT="$frida" READER_HOME_SCRIPTS_URL="file://$home/scripts.zip" \
  READER_HOME_TEXTRACTOR_CLI="$standin|textractor" READER_HOME_LUNA_HOST="$standin|luna" \
  READER_HOME_FAKE_PROGRAMS="?|hook-target|雨の山道;5151|kaitou.exe|怪盗の夜 ver1.02;5152|tsuki.exe|月の怪盗;6200|yuzu.exe|yuzu 1734 · FLOWERS -Le volume sur ete-;6311|PPSSPPWindows64.exe|PPSSPP v1.18.1 - ULJM05887;7020|notepad.exe|Untitled - Notepad|32" \
  "$here/run-desktop.sh" reader-home --window --dry-run --script "$home/hooks.script" > "$home/program.log" 2>&1 || {
    echo "test-hooks: the program failed; see $home/program.log" >&2; exit 1; }
python3 -I - "$home" "${READER_HOOKS_DICT:-}" <<'PY'
import json, pathlib, sqlite3, sys
home = pathlib.Path(sys.argv[1])
db = sqlite3.connect(home / "data/program/reader/reader.db")
rows = db.execute("SELECT game, text, chars FROM lines ORDER BY id").fetchall()
sources = json.loads((home / "data/program/sources.json").read_text(encoding="utf-8"))
titles = {t["engine"]: t for t in sources["titles"] if t["by"] == "hook"}
problems = []
for engine in ("agent", "textractor", "luna"):
    if engine not in titles:
        problems.append(f"no title read by {engine} in sources.json: {sources['titles']}")
got = {engine: [text for game, text, _ in rows if game == title["id"]] for engine, title in titles.items()}
story = ["山道は細く、曲がりくねっている。", "助手席の奈美は、まだ眠っていた。", "雨が降り始めた。ワイパーを動かす。"]
agent = got.get("agent", [])
if len(agent) < 3 or set(agent) != set(story) or any(a == b for a, b in zip(agent, agent[1:])):
    problems.append(f"the Agent script's lines: {agent}")
if titles.get("agent", {}).get("script") != "TEST_Furoshiki_Hook_Target.js" or titles.get("agent", {}).get("process") != "hook-target":
    problems.append(f"the Agent title: {titles.get('agent')}")
kaitou = ["怪盗は屋根の上で、月を見ていた。", "「今夜の獲物は、あの時計塔だ」", "風が強い。マントが音を立てる。", "警部はまだ、何も知らない。"]
for engine in ("textractor", "luna"):
    lines = got.get(engine, [])
    if len(lines) < 3 or not set(lines) <= set(kaitou):
        problems.append(f"{engine}'s lines (the dialogue's thread alone): {lines}")
    if titles.get(engine, {}).get("threads") != ["KIRIKIRI|HQN-C*0@1B2F40:KAITOU.EXE|1B2A|0"]:
        problems.append(f"{engine}'s remembered thread: {titles.get(engine, {}).get('threads')}")
if not all(counted for _, _, counted in rows):
    problems.append("a line not counted")
app = json.loads((home / "data/apps/reader-home-lines.json").read_text(encoding="utf-8"))
chars = sum(day["chars"] for day in app["stats"].get("days", []))
if chars <= 0:
    problems.append("no characters in the lines' file of the page")
log = (home / "program.log").read_text(encoding="utf-8", errors="replace")
print(f"lines by the Agent script: {len(agent)}, by the Textractor stand-in: {len(got.get('textractor', []))}, "
      f"by the LunaHook stand-in: {len(got.get('luna', []))}; characters in the page's file: {chars}")
for line in log.splitlines():
    if "hook:" in line or "strip:" in line:
        print(line.strip())
if sys.argv[2] and "popup open" not in log:
    problems.append("the lookup's popup did not open")
for problem in problems:
    print("FAILED", problem)
sys.exit(1 if problems else 0)
PY
echo "test-hooks: passed; pictures in $home"
