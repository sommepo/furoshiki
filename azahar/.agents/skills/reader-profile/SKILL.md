---
name: reader-profile
description: Add or extend a game's text profile for Azahar Reader. Use when asked to make a 3DS game readable (extract its text live), to investigate how a game shows text, or to fix or widen an existing profile.
---

# Writing a text profile for a game

The result is `src/android/app/src/main/assets/reader/profiles/<TITLEID>.lua` plus
`docs/reader/games/<game>.md`. Read `docs/reader/profiles.md` (API and rules) and
`docs/reader/harness.md` (tools) first. `docs/reader/games/smt4.md` is a finished
example.

Everything taken from the ROM stays under `private/<game>/`. The profile itself holds
addresses and decoding logic only.

## 1. Identify the build

```sh
. tools/reader/env.sh
python3 tools/reader/ctr.py info "path/to/game.3ds"
```

Note the title ID, product code, version and `code_sha256`. An encrypted image cannot
be used: the user has to supply a decrypted dump. If a profile for this title ID
already exists, check which build it was written for before changing it.

## 2. Unpack what is needed

```sh
python3 tools/reader/ctr.py exefs ROM private/<game>/exefs          # code.bin, loaded at 0x00100000
python3 tools/reader/ctr.py romfs-list ROM > private/<game>/romfs-list.txt
python3 tools/reader/ctr.py romfs-extract ROM private/<game>/romfs --match '*.ext'
```

Group the file list by extension and folder. Message data is usually many small files
in one format. Extract only the small text-like files, not media.

## 3. Understand the text format

Decode a few entries by hand (`xxd`, Python). Work out the container, the character
encoding (Shift-JIS, UTF-16, UTF-8 or a custom table tied to the game's font) and the
control codes. Then parse **every** entry of **every** file and count what does not
fit, until nothing is left unexplained. Record the control-code table.

If the game uses a custom glyph table instead of a standard encoding, the mapping
from glyph numbers to characters has to be established from the font and reviewed.
Never fill gaps with guesses or another edition's text.

## 4. Run the game

```sh
tools/reader/build-host.sh
build-host/bin/Release/azahar-reader-harness --user-dir private/harness/user \
    --rom ROM --socket private/harness/sock > private/harness/harness.log 2>&1 &
python3 tools/reader/hc.py "run 600" "shot private/harness/shots/a.png"
```

Look at screenshots (the Read tool shows PNGs; `tools/reader/montage.py` tiles
several). Play to the first dialogue with `press`, `circle` and `touch`. Save a state
just before a line appears and at other useful points: states make every later step
quick.

## 5. Find the function that shows text

- `find <hex of the line's bytes>` shows where the script sits in memory.
- Look in `code.bin` for constants of the format (control-code bases, magic numbers)
  and collect the functions that use them (`private/smt4/work/smt4re.py` shows a way
  with Capstone; `$READER_GHIDRA` is there for decompiling).
- Write a throwaway tracing profile that hooks all candidates and logs arguments and
  what pointer arguments point at. Load a state before the line, run to it, read the
  log. The wanted function is called once per message with the message (or its
  number) as an argument.
- If the text has variables (player name, numbers), find where the expanded text is
  and hook there instead.

## 6. Follow what is on screen

A message can have several pages. Find the window or message object (an argument of
the function above) and log its fields every frame while a multi-page message is
advanced. Identify the page position and, if there is one, the "waiting for the
player" and "closed" states. Emit each page when the game shows it.

Classify what comes through the function: dialogue, narration, choices, system
prompts. Use an argument or the caller's return address to tell them apart, and put
choices on the `choice` channel.

## 7. Write the profile

- Header comment: game, title ID, product code, the build's `code_sha256`, how text is
  shown, what is not covered.
- A build check that calls `error(...)` when instruction words at the hook addresses
  differ.
- `id = text.hash(raw bytes)` (with a page suffix for multi-page messages).
- The game's furigana as ruby spans via `reader.builder()`.
- `reader.on_state_loaded` clearing whatever is remembered about the screen.

## 8. Verify

Load the profile from a state before known lines and compare `events` with
screenshots for several minutes of play (`tools/reader/play.py`). Check at least:

- every line on screen has exactly one event, at the time it appears;
- speaker names, furigana offsets and inserted names are right;
- pages arrive one by one;
- choices are on the `choice` channel;
- nothing is emitted for text that is not shown;
- loading a state emits nothing by itself;
- a deliberately wrong build check is refused with a clear message.

Then build the APK (`tools/reader/build-android.sh`) so the profile is bundled. On a
device, a profile can also be installed with Reader settings → Game scripts → Install from
a file (it goes to the app's own folder, `Android/data/<the app's ID>/files/reader/profiles/`)
and reloaded from the in-game Reader menu without rebuilding.

## 9. Write it up

`docs/reader/games/<game>.md`: build, what is reported, what is not, the format and
control codes, the addresses and object fields with what they mean, how far it was
verified, open items. Add the game to the table in `docs/reader/README.md`. State
exactly what was played; do not claim routes or modes that were not run.
