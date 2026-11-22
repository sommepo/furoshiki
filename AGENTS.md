# Furoshiki: instructions for coding agents

Furoshiki reads Japanese games: emulators of several consoles (each in its own folder), one shared
reader layer (`shared/`), a desktop program (`home/`) and an Android app (`android/`). A game is
supported by one Lua script that reads its text from the emulator's memory.

## Adding support for a game

Game scripts are kept in their own repository,
[sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts), and a game is added
there: its `AGENTS.md` is the whole procedure, and it needs only an installed Furoshiki, Python
and the user's dump, on Windows, Linux or macOS. When you are asked to add a game here, say so,
and offer to clone that repository and work in it instead.

`docs/adding-a-game.md` here is the older way, with each emulator's test harness built from
this repository on Linux. Use it only for what the scripts repository's way cannot do (a
character table, an emulator change).

## Rules

- Never commit game data or anything taken from a game: text, dumps, screenshots, fonts, character
  tables (`*.charset`), BIOS files, keys, firmware. They go in the emulator folder's `private/`,
  which Git ignores. Tests use made-up text.
- Run every harness, emulator, test and heavy script under a memory limit with a timeout. Each
  emulator folder has `tools/reader/limit.sh`; for anything else use
  `systemd-run --user --scope -p MemoryMax=4G -- timeout 600 <command>`. If a capped run is
  killed, fix the cause instead of removing the cap.
- Use the emulator folders' own build scripts (`tools/reader/build-*.sh`); they limit parallel
  jobs.
- Say what was seen and how: a test with made-up text, the harness with the real game, or a real
  device. Never report a step as done that was not seen.
- The build scripts and harnesses run on Linux.

## Where things are

- Each console's scripts: `scripts/<console>/` in sommepo/furoshiki-scripts; the copies in
  each emulator's profiles folder here are kept the same by the maintainer.
- The script API: `azahar/docs/reader/profiles.md`. The other scripts in the same console's folder
  are examples.
- Each emulator's harness and its notes on earlier games: its `docs/reader/` folder.
- Title records and character names: `titles/`, written by `tools/titles.py` (`docs/titles.md`).

## Contributing

A game script: a pull request to `sommepo/furoshiki-scripts`. Anything else: work on a branch
and offer it as a pull request to `sommepo/furoshiki`; the maintainer brings it into their line of
work, from which this repository is made.
