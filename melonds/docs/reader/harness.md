# The headless harness

`melonds-reader-harness` runs a DS game with no display or sound device and takes text commands.
It is a small libretro frontend of its own around the same melonDS as the core
(`reader/harness/harness.cpp`), with the reader's engine in Furoshiki's place: a profile loaded
here runs as it will in the program. Linux only.

```sh
tools/reader/build-core.sh --harness            # build-harness/bin/melonds-reader-harness
tools/reader/harness.sh --rom private/games/GAME.nds &
python3 tools/reader/hc.py --socket private/harness/sock "run 600" "shot private/harness/shots/a.png"
```

`harness.sh` runs it under a memory limit (3 GB; it uses about 200 MB) with its home in
`private/harness` (its `saves/` and `system/`: put `bios7.bin`, `bios9.bin`, `firmware.bin` there
to leave FreeBIOS) and listens on `private/harness/sock`. A second game at once: another home,
`READER_HARNESS_HOME=$PWD/private/harness-999 tools/reader/harness.sh --rom ...`, and
`hc.py --socket private/harness-999/sock`. `READER_HARNESS_ROTATION=none|left|right|auto` turns
the picture as the core's "nds.rotation" does; `READER_HARNESS_LOG=1` shows melonDS's messages.
The emulator only runs while a command runs frames. It runs about 400 frames a second here.

| Command | |
| --- | --- |
| `run N [TIMEOUT]`, `wait S` | Run N frames (or S seconds of them). |
| `press BUTTON [hold=4] [after=4]` | `a b x y l r start select up down left right`, `lid` (closes it), `mic` (blows). |
| `hold BUTTON`, `release BUTTON` | Keep one down across commands. |
| `touch X Y [hold=4] [after=4]` | A finger on the lower screen, in its own 256×192, whatever the picture's turn. Some games take a tap only when it is held longer (Wish Room: `hold=10`). |
| `drag X1 Y1 X2 Y2 [frames=20] [after=4]` | A finger drawn across the lower screen over that many frames. |
| `shot PATH` | The picture as the core hands it over (turned, if it is), as PNG. |
| `events`, `layouts`, `logs` | What the profile emitted since the last time. |
| `profile PATH`, `unload`, `lua CODE`, `actions`, `action N` | As in the other forks' harnesses. |
| `mem ADDR [LEN]`, `memfile ADDR LEN PATH`, `poke ADDR HEX` | The ARM9's memory (`maps` lists it). |
| `find HEX [START END]`, `findtext TEXT`, `findptr LOW HIGH` | TEXT as UTF-8, UTF-16 and Shift-JIS. |
| `regs` | The ARM9's registers between frames (r15 is two instructions ahead). |
| `save PATH`, `load PATH` | A savestate in a file (the profile is told of a load). |
| `audio on`, `audio off`, `wav START END PATH` | Keep the sound, write a part of it (32768 Hz). |
| `status`, `quit` | |

A hook is given the instruction's own address in `cpu.pc` (and r15), the registers as `cpu.r0`…
`cpu.r15`, `sp`, `lr`. A Thumb instruction's hook is at its (even) address. Any instruction may be
hooked, a conditional one too: the hook runs before it, whether or not its condition holds.

`tools/reader/ndsfs.py ROM` lists a cartridge's files; `--extract DIR --match TEXT` unpacks them,
`--arm9 OUT` the ARM9's program. Everything taken from a game goes under `private/`.
