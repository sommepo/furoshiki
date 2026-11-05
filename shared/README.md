# The shared reader layer

The reader layer every reader app has, written once: the text engine and the Lua profiles'
API, save sync, the desktop app's reading logic and windows, and the Android app's Kotlin
layer with its tests and resources. Every fork builds it from here (since 2026-10-23;
`home/docs/one-program.md`, step 6). Before that it was written in the Flycast Reader fork and
copied into each of the others by `flycast/tools/reader/shared.py`; their history is in
`flycast/` (the files were moved here with `git mv`).

| Path | What |
| --- | --- |
| `reader/core/` | The text engine (`engine.*`), the profiles' API (`lua_api.*`), text codecs, the input filter, the next-choice logic, the audio history, the CRT picture (`crt.*`, `crt_shader.glsl` and `crt_glow.glsl` and the `.inc` files made from them) |
| `reader/save_sync/` | Save sync's engine: archives, the service, deciding which save wins; `console_host.*`, save sync as an app runs it for one console (settings, status, conflicts, the background work), with each console's naming of its games and saves in `naming_<console>.*` (PS1, PS2, PSP, Dreamcast, 3DS; `store.*` for the Android save folders a file picker gave); tests in `tests/` (`naming.cpp` holds the names the service has) |
| `reader/lookup/` | The dictionary lookup without Qt, for an app that has none (the PS Vita's): the desktop's deinflector, query, ranking and compact definitions on the same `reader_dict.db`, checked against the desktop's by `tests/parity.sh` |
| `reader/vita/` | The PS Vita apps' reading layer (PS1 Reader, Adrenaline Reader): the lookup where the word is on the screen by a front tap or the rear pad's pointer, the modal popup that scrolls with the stick (`lookup_layer.*`, tested on a computer by `tests/lookup_layer_test.sh`), its drawing with vita2d in plain C (`scene_draw.c`, which Adrenaline's C draw thread uses too), and the apps' settings file (`settings.*`) |
| `reader/desktop/` | The desktop app's reading logic: dictionaries, lookups, cards, statistics, the backlog, game scripts, the Steam Controller bridge; tests in `tests/` |
| `reader/qt/` | The desktop app's windows over the emulator's, and the development channel |
| `reader/android/reader_jni_common.cpp` | The part of the Android app's native bridge that is the same in every app |
| `android/kotlin/`, `android/test/`, `android/res/` | The Android reading layer, its JVM tests and its resources, in the package `org.readers.app.reader` |
| `popup/` | The dictionary popup as a web page, its script and themes: Furoshiki's web engines and the Android apps' WebView draw it (`popup/README.md`; `tools/popup.py` makes its desktop form) |
| `reader.cmake` | What a fork's CMake includes |
| `android/reader.gradle` | What an Android app's Gradle file applies |

## How a fork builds it

- **C++.** A fork's top `CMakeLists.txt` sets `READER_SHARED` to this folder (`../shared`, or
  `shared/` inside a published tree of the fork) and includes `reader.cmake`, which puts this
  folder on the include path. The fork's own CMake files name the shared files they build as
  `${READER_SHARED}/reader/core/engine.cpp` and so on, beside its own files.
- **What is a fork's own stays in the fork**, under the same names, in its own `reader/`
  folder (`forks.json`'s `"reader"`: `reader/` in most forks, `src/reader/` in Eden's and
  Azahar's): `core/console.h` and `core/registers.h` (what the layer must know of the app, its
  console and its processor), `core/types.h`, `core/emulator.*` (the emulator's side),
  `qt/host.*`, `qt/install.h`, `qt/reader_look.*`, save sync's `host.*` (since 2026-10-24 the
  functions the app calls over one `SaveSync::ConsoleHost` with its console's naming; Eden's is
  its own still), the console's own tests
  (`desktop/tests/console.cpp`). Shared and own files are both found as `"reader/core/..."`;
  **no file may be in both places** (`tools/shared.py check` says when one is).
- **The Android app's native bridge** (`reader_jni_common.cpp`) names its functions by the
  app's Java package: the fork's CMake calls `reader_jni_package(com.flycast.emulator)` in the
  folder that makes the library.
- **Kotlin and resources.** An app's `build.gradle.kts` applies `android/reader.gradle` after its
  `android { }` block. Before anything is compiled, the Kotlin, the tests and the resources are
  written into the app's `build/generated/reader` with `org.readers.app` replaced by the app's
  package (`android.namespace`), and added to its sources: each app is built from exactly the
  text it had when it kept a copy, with the same packages, the same JNI names and the same
  manifest. The app's own Kotlin (`Console.kt`, `ReaderGlue.kt`, `ui/ConsoleLook.kt`,
  `ui/ReaderStyle.kt`) and resources (`values/strings_console.xml`, the themes,
  `xml/reader_prefs_console.xml`) stay in the app, in the same package.
- **Furoshiki** builds the engine and the desktop windows from here too, with its own
  `console.h`, `registers.h`, `host.h` first on the include path (`home/CMakeLists.txt`).

## Changing it

A change made here is in every fork at once. Build and test each fork the change touches
(each fork's `tools/reader/test-desktop.sh`, `test-savesync.sh`, `test-android.sh`; Reader
Home's tests): a fork's own files may need to follow (a new value asked of `console.h`, a new
function of the host). In Kotlin, write `org.readers.app` where the app's package belongs
(`import org.readers.app.R`), never one app's package. A new shared source file is added to
the CMake lists of every fork that builds that part: `tools/shared.py check` names a fork that
does not build it.

`tools/crt.py inc` makes `reader/core/crt_shader.inc` and `crt_glow.inc` after a change to
the shaders; `tools/crt.py sync` also writes the shader's forms that some emulators take
(`forks.json`'s `"crt"`: PPSSPP's post-processing files, Eden's fragment shaders).

**The glass's blurred copies** (2026-11-05): a renderer that draws the steps of
`Crt::GlowPlan` first (five small textures of the picture in linear light, made with
`crt_glow.glsl`) and hands them to the picture's shader (`CRT_GLOW`, `CRT_GLOW_TEX0` to
`CRT_GLOW_TEX4`: a plain flag and five names, as AMD's OpenGL on Windows cannot test a macro
with arguments in `#ifdef`) gets light that spreads as far as a tube's glass spreads it: a haze
off what is bright, into the dark beside it, and none over text on a light box. A renderer that
does not draws the picture as before, with the short glow of one pass. They are made by the
libretro host's OpenGL (`cores/libretro`: Flycast's and PPSSPP's cores), Station's OpenGL,
Azahar's OpenGL, PCSX2's four renderers and Eden's Vulkan; Azahar's Vulkan (its standalone app
only) and ARMSX2 still draw the one pass. Seen in Furoshiki on Linux, in software, for the
Dreamcast, PlayStation, 3DS, PS2 and Switch.

**HDR** (2026-11-05): the CRT page's *Screen* rows (`hdr`, `hdr_white`, `hdr_peak`; the
screen's, so not part of a look nor of Reset, and not on the Android page). A renderer that
draws into an HDR surface defines `CRT_HDR` and `CRT_HDR_V` (`Crt::PackHdr`): the shader then
writes scRGB or HDR10's PQ, the lit phosphor brighter than paper white. Made by the libretro
host on Windows (a DXGI swapchain in scRGB, its OpenGL drawing into a texture shared with
Direct3D 11 through WGL_NV_DX_interop2; chosen as a game starts), PCSX2's Direct3D 11 and 12
(switched while playing) and Eden's Vulkan (scRGB or HDR10, the swapchain made again on a
change). None of it has been seen on an HDR screen yet.

## Publishing

The public repositories are made from this repository (`tools/public-tree.py`, not yet used
for publishing). A published fork is to carry this folder as `shared/` inside its own tree: its
CMake already looks there when there is no `../shared`; its app's `apply(from = ...)` line
names `../shared` by its path from the app, which the export will have to write for the
folder inside (not done yet: publishing has not moved here).
