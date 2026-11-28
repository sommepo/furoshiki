# Adding a game

[Back to README](../README.md)

Game scripts live in [sommepo/furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts) and are designed to be written with a coding agent (Claude Code, Codex, Gemini CLI, etc.) on Windows, Linux, or macOS. The agent launches the game in Furoshiki, checks existing Agent and LunaHook tables or locates the game's text in memory, writes the Lua script, and verifies it against the running game.

**Requirements:** Furoshiki 0.3.2+, Python 3.9+, a coding agent, and your own dump of the Japanese release.

1. Fork [furoshiki-scripts](https://github.com/sommepo/furoshiki-scripts), clone it locally, and start your agent inside the repo.
2. Tell the agent which game to add along with the paths to your dump and Furoshiki, for example:
   *"Add support for Steins;Gate on PSP. The game is at `D:\Games\PSP\steins-gate.iso` and Furoshiki is in `C:\Furoshiki`."*
3. The agent follows [AGENTS.md](https://github.com/sommepo/furoshiki-scripts/blob/main/AGENTS.md) and summarizes what it verified in-game (plus anything it couldn't test).
4. Play through a few scenes to test it. The agent saves the script directly into your local scripts folder, which takes precedence over downloaded scripts:
   - Windows: `%LOCALAPPDATA%\Furoshiki\program\reader\profiles`
   - Linux: `~/.local/share/Furoshiki/program/reader/profiles`
   - Android: `Android/data/io.github.sommepo.furoshiki/files/reader/profiles`
5. Have the agent open a pull request. Once merged, anyone can install the script from **Settings › Game scripts** without updating the app.

To share an unmerged script with someone else, send them the `.lua` file (and its `.charset` character table, if it has one) to drop into their scripts folder above.
