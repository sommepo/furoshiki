Furoshiki @VERSION@ for Linux (x86-64)

To start
  1. Unpack the tarball anywhere you like and run Furoshiki/reader-home
     (from a terminal: ./Furoshiki/reader-home; --window keeps it out of full screen).
  2. Say yes when it offers a dictionary (Jitendex, free, 39 MB), or choose your own
     Yomitan dictionaries. Lookups need one.
  3. Install the game scripts of your games in Settings > Consoles (they are read from
     the published lists), then choose the folder your games are in, or "Add game".
  4. For Anki cards: open Anki with the AnkiConnect add-on (code 2055492159), then
     Settings > Cards > Anki setup.

It needs an X11 desktop (or Xwayland), glibc 2.38 or newer, working OpenGL and Vulkan
drivers, PulseAudio or PipeWire for sound, and fontconfig with a Japanese font
(Noto Sans CJK JP, for example). Everything else it needs is in its lib folder.

Your settings, dictionaries, saves and BIOS files are kept in ~/.local/share/Furoshiki
(or $XDG_DATA_HOME/Furoshiki), so a new version can be unpacked over the old one.
A console's BIOS goes in program/consoles/<console>/system there (ps1, ps2).

The guide, the list of supported games and help:
  https://github.com/sommepo/furoshiki

COPYING.txt, NOTICES.txt and the licenses folder say under which licences the program and
its emulators are published. SOURCE.txt says where the source code is.
