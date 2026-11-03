# What's Shenmue (What's シェンムー 〜湯川(元)専務をさがせ〜), 6107179

One disc; its product number is the seven digits. Build ID CB91066F. Profile:
`reader/profiles/6107179.lua`.

**The game shows no subtitles as it comes**: its settings (設定, 声と字幕) have to be on
ゲームモード (voice and subtitles) or テキストモード (subtitles only). On シェンムーモード,
the first setting, there is nothing on screen to read and nothing is reported.

## What is read

**The subtitles** of the search for Mr Yukawa: what is said in the cutscenes and in the
conversations, each once, as one or two rows, with a place for every character.

## What is not read

- **Who speaks**: the game shows no name.
- **The notebook** (handwriting in a picture), the help screens, the menus, the
  Information part's character viewer and its descriptions.

## How the game keeps its text

As Shenmue II does (`shenmue2.md`), a little differently placed:

- The program's pointer at `0x8C1B277C` leads to the scene task's data, which begins with
  the tag "SCEN". (The pointer is written as `0x0C...`: the same memory.)
- At `+0x80` the subtitle being shown, **EUC-JP**, zero-terminated; a full-width ampersand
  (`A1 F5`) where the second row begins. At `+0x58` a pointer to the text's end, which is
  zero while no subtitle is up: the text's own place holds other things then.
- Measured in pictures: 25 pixels to a full-width character; the rows begin at x = 130, or
  at x = 82 when a row is longer than fourteen characters (seen with fifteen and sixteen);
  one row at y = 413, two rows at 383 and 413.

## How it was checked

On Linux without a display (the game drawn in software), 2026-10-10, with the settings on
ゲームモード: the first scene of the search (Ryo and Tom at the hot dog truck), eight
subtitles, each reported once as it came up and read against pictures; the cells of a
one-row subtitle of sixteen characters and of a two-row one drawn over pictures. **Not
looked at**: anything after that scene, a conversation started by the player, a row longer
than sixteen characters, the Information part.
