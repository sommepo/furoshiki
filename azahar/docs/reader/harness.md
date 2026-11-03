# The headless harness

`azahar-reader-harness` runs a game on a machine with no display, GPU or audio device
and takes text commands. It is how text profiles are developed and how the text
engine is tested against a real game. It is a Linux development tool; it is not part
of the Android app.

```sh
tools/reader/build-host.sh        # build-host/bin/Release/azahar-reader-harness
tools/reader/limit.sh build-host/bin/Release/azahar-reader-harness \
    --user-dir private/harness/user --rom "private/…/game.3ds" \
    --socket private/harness/sock &
python3 tools/reader/hc.py "run 600" "shot private/harness/shots/a.png"
```

Always start it through `tools/reader/limit.sh` (a hard memory limit, 4 GB unless told
otherwise). A profile under development is exactly the kind of code that runs away.

The game runs unthrottled with the software renderer and a fixed clock, so the same
commands give the same frames. `--user-dir` is the harness's own emulator user folder:
it never touches a real Azahar setup. Without `--socket`, commands are read from
`--script FILE` or standard input.

`hc.py` sends one or more commands and prints the reply. Every reply ends with `OK`
or `ERR <message>`. The emulator only advances while a command runs frames, so a
session can sit idle between commands for as long as needed.

## Commands

| Command | |
| --- | --- |
| `run N` | Run N frames. |
| `press BUTTON [hold=4] [after=4]` | Hold a button for some frames, release, run some more. Buttons: `a b x y up down left right l r start select zl zr home`. |
| `hold BUTTON`, `release BUTTON` | Keep a button down across commands. |
| `circle X Y` | Circle pad position, each from -1 to 1. `circle 0 1` walks forward. |
| `touch X Y [hold=4] [after=4]` | Touch the bottom screen at pixel X, Y (320×240). |
| `shot PATH` | Write a 400×480 PNG: top screen above the bottom screen. |
| `events` | Print the text events emitted since the last `events`, one JSON object per line. |
| `layouts` | Print the on-screen layouts reported since the last `layouts`, one JSON object per line. |
| `profile PATH` | Load a Lua profile (replacing the current one). |
| `unload` | Remove the profile. |
| `lua CODE` | Run Lua in the profile's state and print the result: `lua mem.u32(0x100000)`. |
| `mem ADDR LEN` | Hex dump of game memory (ADDR in hex). |
| `memfile ADDR LEN PATH` | Write game memory to a file. |
| `find HEX [START] [END]` | Search game memory for a byte string; prints up to 64 addresses. |
| `savestate PATH`, `loadstate PATH` | Save or restore the whole machine. States from earlier builds of the harness load too. |
| `audio on [SECONDS]`, `audio off`, `audio pos` | Keep recent audio output; print the history position. |
| `wav START END PATH` | Write audio between two history positions (END 0 = now). |
| `status` | Frame, title ID, whether a profile is loaded, hook count. |
| `quit` | Stop. |

`tools/reader/play.py STEPS` presses a button repeatedly and prints the decoded
events compactly, which is the quickest way to read through a scene.
`tools/reader/montage.py` tiles screenshots into one contact sheet.
`tools/reader/layout_check.py SHOT.png OUT.png < layouts` draws the reported text box
and character cells over a screenshot (take the screenshot right after the `layouts`).

## Finding where a game shows text

What worked for Shin Megami Tensei IV, as a recipe:

1. **Unpack.** `tools/reader/ctr.py info|exefs|romfs-list|romfs-extract` gives the
   program (`code.bin`, loaded at 0x00100000) and the data files. Work in `private/`.
2. **Find the script format.** Look for message files, decode a few by hand, and
   tally the control codes over all files until every entry parses.
3. **Reach a line of dialogue** in the harness and save a state there. Compare the
   screenshot with the script to know which entry is showing.
4. **Find the text in memory**: `find <bytes of that line>`.
5. **Find the code.** Constants of the format are good anchors: for control codes
   `F8xx`, instructions using the immediate `0xF800` led to a dozen candidate
   functions. Hook all candidates with a tracing profile that logs their arguments
   (and what pointer arguments point at), run to the line, and see which one is called
   once per message with the message as an argument.
6. **Prefer the expanded text.** If a line has a variable (a name the player typed),
   look for the buffer where it has been filled in and hook after that.
7. **Find the page state.** Dump the fields of the text window object every frame
   (`reader.on_frame`) while a multi-page message is advanced; the field that jumps to
   the next page's offset tells which page is showing.
8. **Find where characters are drawn.** A tracing profile that hooks every function
   entry of the program and notes which ones receive the text window as an argument
   finds the window's update and draw routines within a few frames (7,304 hooks ran at
   8 frames per second here). In the draw routine, the call that draws one character
   has the pen position in its arguments; float arguments are in `cpu.s0`, `cpu.s1`, ….
9. **Write the profile**, load it from a state before the line and compare `events`
   with screenshots over a few minutes of play. Check names, furigana, choices and
   that nothing is emitted before it is shown. Check `layouts` with
   `layout_check.py`.

Dynamic tracing is usually faster than reading disassembly. `tools/reader/env.sh`
documents where a Python environment with the Capstone disassembler and a Ghidra
install live on this machine, for when it is not.

## Known limits

- The software renderer manages 15 to 60 frames per second in 3D scenes and movies.
- States embed the emulator's serialisation layout: after merging a new upstream
  release, old states may not load.
- Audio is whatever the game mixes; the harness has no way to isolate a voice.
