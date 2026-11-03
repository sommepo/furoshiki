# Text profiles

A text profile teaches the emulator where one game keeps the text it shows. It is a
Lua script named after the game's title ID, for example `0004000000088A00.lua`. The
core runs it; the Android app and the headless harness only receive the text events
it emits.

Profiles are the same idea as the per-game scripts of Agent or Textractor hook codes:
a few addresses in the game's code, plus the knowledge of how that game encodes its
text. Nothing is hooked without a profile.

## Where profiles live

| Place | Use |
| --- | --- |
| `src/android/app/src/main/assets/reader/profiles/<TITLEID>.lua` | Bundled with the app. Only addresses and decoding logic: no game text. |
| `Android/data/<the app's ID>/files/reader/profiles/<TITLEID>.lua` | Yours, put there by **Reader settings → Game scripts → Install from a file** (or by hand). Wins over the bundled one. Edit, then **Reader → Reload text profile** in the game menu. Until 2026-10-17 this folder was `<Azahar user folder>/reader/profiles/`; what was there is copied over once at the app's first start since. |

A data file a profile asks for with `reader.data("name")` is looked up as
`<TITLEID>.name` in the same two places.

## Lifecycle

The script runs once when the game has been loaded, on the emulation thread, before
the game executes. It registers its hooks and callbacks and returns. It stays loaded
across savestate loads. An error while loading unloads the profile and is shown to
the user; an error in a callback is logged, and a hook that fails five times in a row
is switched off.

There is no file, network or process access. `print` writes to the log, and `load`
accepts source text only. Three limits exist to keep a faulty script from freezing the
game or exhausting memory, and are far beyond what a profile needs: loading may run
500 million Lua instructions, each callback 10 million, and the script may hold 128 MB.
Going over one ends that callback with an error, even inside a `pcall` of the profile's
own. A frame callback that fails five times in a row is switched off like a hook.

## API

### `reader`

| Function | |
| --- | --- |
| `reader.hook(address, function(cpu) ... end)` | Run the function each time the instruction at `address` is about to execute in the game's process. `cpu` has `r0`…`r12`, `sp`, `lr` and `pc` (the hooked address), and the floating-point registers: `s0`…`s31` as numbers, `d0`…`d15` as doubles (games pass float arguments in `s0`, `s1`, …). `cpu` is only valid while the handler runs: copy what you need. Function entries are the usual choice; any **unconditional** instruction works. A conditional one (`addeq`, `bne`, …, or a Thumb instruction inside an IT block) cannot carry a hook: `reader.hook` raises an error, so pick the instruction before or after it. Give an odd address for Thumb code. |
| `reader.on_frame(function(frame) ... end)` | Run once per emulated frame. Use it to watch state the game updates over time. |
| `reader.on_state_loaded(function() ... end)` | Run after a savestate was loaded. Forget anything remembered about what is on screen. |
| `reader.emit{...}` | Report a piece of presented text (see below). |
| `reader.layout(window, {...})` | Report where the text of a window is on screen, or with `nil` that it is gone (see [Text on screen](#text-on-screen)). |
| `reader.frame()` | Emulated frames since the game started. |
| `reader.title_id`, `reader.game_id` | The running title ID, 16 hex digits (`game_id` is its name in every reader fork). |
| `reader.picture()` | Width and height in pixels of the picture the game hands over for display; nothing before it has shown one. |
| `reader.action(label, function() ... end)` | Offers the player something to do in the game: the apps list the labels, in this order, under *In this game* in the reader's menu, and run the function when one is chosen. What it returns (a string) is shown to the player; an error is shown as it is. |
| `reader.progress{...}` | Progress of the game's own that the player may set by hand (routes read in another copy of the game): `{title, note, items = {{id, label, group}...}, done = function(id), mark = function(id, on)}`. The apps show a box for each item. |
| `reader.cursor(index [, "horizontal"])`, `reader.cursor(nil)` | Which answer of the choice on screen the game's cursor is on (from 0; -1 while it is on none yet), and whether the answers stand side by side; nil once there is no choice. What a tap on an answer is turned into presses of the d-pad with (*Play by touch*). |
| `reader.log(...)`, `reader.warn(...)` | Diagnostics, shown in the log (and by the harness). |
| `reader.data(name)` | Contents of a data file as a string, or nil. |
| `reader.builder()` | Text builder that keeps ruby offsets right (see below). |
| `reader.changed(slot, value)` | True when `value` differs from the last value passed for `slot`. Handy to drop repeats. `reader.forget(slot)` resets one slot, `reader.forget()` all. |

`reader.emit` takes a table:

| Field | |
| --- | --- |
| `text` | Required. UTF-8, lines separated by `\n`. Base text only: no ruby readings. |
| `speaker` | Speaker name, if the game shows one. Not counted as reading. |
| `channel` | `"dialogue"` (default) or `"choice"`. Choices are shown under the line and counted separately. |
| `id` | Stable identity of this text in the game. The same `id` again means the text is being reread. Defaults to a hash of channel, speaker and text; a hash of the game's raw bytes is better. |
| `ruby` | `{ {start, length, reading}, ... }` with offsets in **code points** of `text`. |
| `meta` | Flat table of strings, numbers or booleans. Kept with the event; not shown. |
| `window` | A string or number naming what the text is shown in (the address of the game's window object does well). Needed for `reader.layout`; text emitted later with the same `window` replaces this text on screen. |

Emit a line **when the game presents it**, once. Do not emit text the player has not
been shown (later pages of a message, unvisited script). Events feed the reading
statistics, so a profile that emits early or twice makes the numbers wrong.

### Text on screen

The app lets the player tap a word **in the game's own picture**. For that it has to
know where every character is, and which part of the screen is the text box:

```lua
reader.emit{ text = "道を見張れ", id = ..., window = window }
...
reader.layout(window, {
    screen = "top",                    -- or "bottom"
    box    = { 0, 179, 400, 65 },      -- the text box: x, y, width, height (optional)
    glyphs = { {40, 217, 16, 21}, {56, 217, 16, 21}, ... },
})
...
reader.layout(window, nil)             -- the text is no longer on screen
```

- Coordinates are pixels of that screen: 400×240 for the top, 320×240 for the bottom (a
  `space = {width, height}` says otherwise, as in the other reader forks).
- `glyphs` has **one entry per code point of the emitted `text`**, in order: the cell
  the character occupies, or `false` where nothing is drawn (a line break). Prefer the
  cell from the pen position to the start of the next character over the tight bounds
  of the glyph: a finger aims at cells.
- A layout belongs to the latest text emitted for that `window`; the call returns
  false if there is none. Report it again when it changes (text being typed out, a box
  that moves), not every frame.
- Taps inside `box` never reach the game, so that looking up a word cannot advance the
  text. Without `box` only the characters themselves are protected.
- Closing is the profile's job: when the window closes, when it shows something else,
  and when it simply is not drawn any more. A state load closes everything by itself.

Where the positions come from depends on the game. Layout that is stored when the
text is set can be read from memory. Often it is not stored: the game computes
positions while drawing, and the place to look is the routine that draws one
character. Hook it and read its arguments; that gives exactly what is on screen,
including text that is still being typed out. The SMT IV profile does this.
`tools/reader/layout_check.py` draws reported layouts over a harness screenshot, which
shows at a glance whether the cells sit on the characters.

### `mem`

All reads are of the game process and never fail hard: an unmapped address gives nil
(or a shorter string).

| Function | |
| --- | --- |
| `mem.u8(a)`, `mem.u16(a)`, `mem.u32(a)`, `mem.s32(a)` | Little-endian integers, or nil. |
| `mem.read(a, n)` | Up to `n` bytes as a string. |
| `mem.cstr(a [, limit])` | Bytes up to a zero byte. |
| `mem.wstr(a [, limit])` | Bytes up to a zero 16-bit unit. |
| `mem.u64(a)`, `mem.f32(a)`, `mem.f64(a)` | Also little-endian, or nil. |

Writing, only for what the player asked for: while progress is marked (`reader.progress`'s
`mark`), and once one of the profile's actions was chosen, until the profile is reloaded
(what an action set going may go on writing from its frame callback). Anywhere else it is an
error.

| Function | |
| --- | --- |
| `mem.w8(a, v)`, `mem.w16(a, v)`, `mem.w32(a, v)`, `mem.w64(a, v)` | Write the low bytes of `v`, little-endian. True when written; false, with nothing written, where any of it is not mapped. |

### `text`

| Function | |
| --- | --- |
| `text.sjis(bytes)` | CP932 (Shift-JIS) to UTF-8. |
| `text.eucjp(bytes)` | EUC-JP to UTF-8. |
| `text.utf16le(bytes)`, `text.utf16be(bytes)` | UTF-16 to UTF-8. |
| `text.hash(bytes)` | 16 hex digits; use it for `id`. |
| `text.len(s)` | Length in code points. |

Lua's own `string`, `table`, `math`, `utf8` and `coroutine` libraries are available.

### The builder

```lua
local b = reader.builder()
b:add("…")
b:add_ruby("君", "きみ")   -- base text with a reading
b:add("は")
reader.emit{ text = b:text(), ruby = b:ruby() }
```

## Rules for a profile

1. **Bind it to the build.** Addresses belong to one build of one game. Check a few
   instruction words at load and call `error(...)` when they differ, as the SMT IV
   profile does. A profile that silently hooks the wrong place is worse than none.
2. **Emit what is on screen.** Find the state that says which page or line is
   showing and follow it, rather than emitting a whole script entry at once.
3. **Read after substitution.** Hook where names and numbers have already been
   inserted into the text.
4. **Use the game's own furigana** when it has any, as ruby spans.
5. **Say where the text is** (`reader.layout`) when it can be found out. Without it the
   app can only show the line in its own panel.
6. **No game text in a bundled profile.** Addresses, control-code tables and decoding
   logic are fine. Extracted scripts, glyph tables made from the game's font and
   anything else derived from the ROM stay in `private/` or the user folder.
7. **Say what is not covered** in the header comment.
8. **Say what the profile is** in three lines among the comments it begins with:
   `-- reader: title <the game's name>`, `-- reader: reads <what is read, in a line>`
   and `-- reader: version <the release it was written for>`. The app lists the game
   under *Game scripts* with them. Every bundled profile has them (a test says so);
   a profile of your own without them is listed by its title ID.

## A minimal profile

```lua
-- Example game (title 0004000000XXXX00), build with code SHA-256 ....
local SHOW_MESSAGE = 0x00123456   -- void ShowMessage(Window*, const u16* utf16)

if mem.u32(SHOW_MESSAGE) ~= 0xE92D4070 then
    error("this profile is for another build of the game", 0)
end

reader.hook(SHOW_MESSAGE, function(cpu)
    local raw = mem.wstr(cpu.r1, 2048)
    if #raw == 0 then return end
    reader.emit{ text = text.utf16le(raw), id = text.hash(raw) }
end)
```

See [the SMT IV notes](games/smt4.md) for a full one, and
[the harness guide](harness.md) for how to find the addresses.
