# Game scripts (text profiles)

One `<SERIAL>.lua` per game is here (`SLPM-66913.lua`: the disc's serial as the console writes
it): the game scripts. A build of one's own builds them into the program; a published build
comes with none and installs them from the list `index.json` beside them in the public
repository (made by `tools/reader/scripts-index.py`; READER.md, "Game scripts"). `READER.md`
at the repository's root says what a profile is and has a paragraph for each game. A
`<SERIAL>.charset` beside a profile is that game's character table: made from the game,
ignored by Git, put here by `tools/reader/private-data.sh` for a build of one's own.
