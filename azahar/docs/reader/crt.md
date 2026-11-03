# The CRT picture

*Reader settings, CRT picture* shows the game as a cathode-ray tube would show it. **In this app it is off until switched on**: this console was never played on a tube. (The siblings for consoles that were have it on from the first start.)
It is the reader's own: the emulator's own filters and shaders are
left as they are, and while the picture is on it takes the place of the post-processing shader.

## What it does

One switch, six ready-made looks, and a slider for every part. A change shows in the game at
once.

| Group | Sliders | |
| --- | --- | --- |
| Lines | Scan lines, Beam, Lines, Sharpness | The dark gaps between the tube's lines; how much a bright line swells and closes them; how many lines there are; how small the beam's spot is along a line. |
| Phosphor | Phosphor, Phosphor strength, Phosphor size | The pattern of red, green and blue the tube's face is made of: aperture grille (stripes), slot mask (bricks), shadow mask (dots), or a fine one for small screens. |
| Glass | Warp, Corners, Vignette, Reflection | The bulge of the glass, its rounded corners, the darker corners, a lamp mirrored faintly in it. |
| Light | Glow, Halation, Bloom, Brightness | Light that spreads a little around what is bright, light that spreads far and warm through the glass, bright light running over the phosphor's pattern. |
| Colour | Warmth, Colour, Contrast | The tube's white from bluish to amber, the strength of its colours. |
| Signal | Convergence, Grain, Flicker | Red and blue landing apart, snow, a hum bar wandering up the picture and lines that tremble. |

The looks: **Old tube, strong** (the values a new install has, which the maintainer chose on
their own screen: 240 thick lines, a plain slot mask, a strong bulge, much glow, a little
snow and flicker), **Warm anime, soft** (the gentle one, with the console's own number of
lines), **Trinitron**, **Old television**, **Arcade cabinet**, **Studio monitor** and
**Scan lines only**. Moving a slider makes the picture "My own".

Things worth knowing:

- **Lines: as the console** takes the console's own number of lines (240 for a 3DS) and halves it
  until the screen has three of its pixels for each: fewer cannot show a line and its gap,
  and lines that are still too fine fade out rather than beat against the screen's pixels.
  A number chosen by hand is taken as it is.
- **A picture drawn finer than the console drew it** (a higher rendering resolution) keeps
  its detail: the lines are laid over it. A picture at the console's own resolution is
  shown as its rows, each the middle of a line.
- **Phosphor size: as the screen** is one pixel up to 1080 lines of screen, two at 2160.
- **The words stay where the text is.** What the reader lays over the game's text (the
  places a click or a tap finds a word by, the mark on a word) is bent with the glass.
- **The handheld make is lighter**: the Android apps build the shader with fewer samples for
  the glow and along a line. Everything else is the same.

## The frame

With the CRT picture on, the computer's app can also put the tube's frame around it: what
a photograph of an old monitor shows of it, after michadame's monitor frame. Under *Frame*
in the CRT settings:

- **Frame**: the plastic of the case, *Charcoal* (at first), *Black*, *Beige* (the putty of a
  90s office monitor), or *None*.
- **Room**: *Lit*, a lamp in the room lights the plastic and the recess casts a shadow on the
  glass; *Dark*, only the picture lights the recess, and the case is all but black.
- **Wear**: dust settled on the recess's bottom shelf, grime in the seams, plastic aged by
  the years; none at 0.

The opening follows the tube's edge as the shader draws it (*Warp* and *Corners*), a little
inside it, so the frame hides the picture's very edge as a real case does. The frame is drawn
by the reader's layer over the game (`shared/reader/qt/reader_crt_frame.*`, made in the background once
for each window size and setting, then only drawn), under the word's mark and the rest, so
it needs nothing of the emulator's renderers. It is shown while the reader is reading the
game. The looks leave it as it is.

**On Android** (2026-10-16, the maintainer: "Android builds should also have crt frame updates"):
the same drawing, moved out of Qt into `reader/core/crt_frame.h` (header only, so no build lists
it), which the computer's `reader_crt_frame.cpp` now calls too. `crt_jni.h` hands it to the apps
(`CrtNative.frame`, `CrtNative.revision`) and `ui/CrtFrameView.kt` draws it: the first child of the
reader's layer on the game's display, made in the background whenever the view's size, the game's
picture or the settings change (looked at every half second). The settings page has the frame's
rows. **Azahar Reader leaves it out** (`READER_CRT_NO_FRAME` in its bridge: a phone shows both 3DS
screens on one display, and a frame around one would cover the other), and its page has no frame
rows. Checked by the tests of the drawing (`crt_test.cpp`: the glass clear, the case solid, both
colour forms the same where solid) and the apps' builds. **Seen on the Thor** (PPSSPP Reader, 428's first screen, the picture filling the 16:9 display): the charcoal case at the curved edges, the seal, the lit recess with dust on its bottom shelf. Not seen with a 4:3 game, nor on the Fold.

## How it is made

One shader, one pass, the same text in every reader fork and for every graphics API:
`shared/reader/core/crt_shader.glsl`, written in what GLSL ES 1.00, GLSL 3.30, Vulkan GLSL and HLSL have
in common. It was written here; what it does follows the CRT filters of
[michadame](https://github.com/Morgawr/michadame) (its two tube filters, its glass and its
cathode interference), which the maintainer asked to be the model, and its phosphor patterns
follow Timothy Lottes' public-domain CRT shader, as michadame's do.

| File | |
| --- | --- |
| `shared/reader/core/crt.h`, `crt.cpp` | The table of what can be set (`kParams`), the looks (`kPresets`), the values, and `Pack`: the eight vectors (128 bytes) a renderer hands the shader. Knows nothing of an emulator or a toolkit. |
| `shared/reader/core/crt_shader.glsl`, `crt_shader.inc` | The shader, and the same as a C++ string (made by `tools/crt.py`). |
| `shared/reader/qt/reader_crt_page.*` | The settings page on a computer, built from the table. |
| `shared/reader/qt/reader_crt_frame.*` | The tube's frame around the picture on a computer. |
| `shared/reader/core/crt_jni.h`, `shared/android/kotlin/org/readers/app/reader/ui/CrtSettings.kt` | The Android apps' bridge and their settings page, built from the same table. |
| `shared/reader/desktop/tests/crt_test.cpp` | Tests of the table, the kept text, the looks, what the shader is handed, the glass's bend. |
| `src/video_core/reader_crt_present.h` | What ties it to this emulator's two renderers. |

**These files are in the repository's `shared/`, once for every fork** (the paths above are from
the repository's top). After a change to the shader, `tools/crt.py sync` writes `crt_shader.inc`
and the shader files the PPSSPP and Eden forks build theirs from; `tools/crt.py check` says when
one of them is not as the shader would make it.

**A new slider** is a row in `kParams`, a place in `Pack` (the eight vectors are full: two
pairs already share a number each, as the shader's first lines say), its use in the shader,
`tools/crt.py sync`, and a build of each fork. Neither settings page names a slider. The values are kept as
one line of text (`enabled=1;scanlines=0.550;...`): `picture/crt` in the desktop app's
`reader.ini`, the preference `reader_crt` on Android. A key is never renamed.

## In this emulator

The emulator puts each of the console's screens on the window as a quad. With the CRT
picture on, each quad is drawn with the reader's shader, so **each screen is a tube of its
own**, with its own bulge and corners:

- **Vulkan** (`renderer_vulkan.cpp`): a fourth present pipeline. Its 128 bytes are push
  constants behind the present shaders' own 112 (`PresentUniformData::reader_crt`), so it
  needs a device that takes 240 bytes of them; where not, the picture is shown plain.
- **OpenGL and OpenGL ES** (`renderer_opengl.cpp`): the present shader is built from the
  reader's text while the picture is on, whatever post-processing shader the settings name.
- **The software renderer** and the 3D modes that use two pictures (anaglyph, interlaced)
  show the picture plain.

The console's pictures lie on their side in their textures; the shader is told where the
corners are (`ReaderCrtPresent::Constants`). Whether a card's picture shows the CRT picture
was not looked at. **On Android the word places do not follow the glass's bend when both
screens are shown**: only the single-picture placing was changed.

## How it was checked

- **Tests** with made-up values: `crt_test.cpp`, part of `tools/reader/test-desktop.sh`.
- **In the app on Linux, Vulkan, drawn in software** (2026-10-13; `READER_DESKTOP_QPA=xcb`
  and a stand-in screen, since the usual run here has no window): a game's title screen
  with the picture switched on over the development channel: both screens bent, each with
  its lamp at the top.
- **OpenGL**: The Windows package was built with it (2026-10-13). The Linux build here has no OpenGL renderer, so that path was
  **never run**; its shader text was compiled by itself for OpenGL 4.3 and OpenGL ES 3.2.
  It is what the Windows app uses unless told otherwise: if the picture does not change
  there, *Emulation, Configure, Graphics* has Vulkan.
- **Android**: the APK was built with it and the 194 tests of the reading layer pass; the page itself was drawn nowhere. On no device.
- **Not looked at**: the screen layouts other than the default, upright orientations, the
  sliders moved by hand.
