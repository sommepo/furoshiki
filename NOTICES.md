# Furoshiki: licences and notices

Furoshiki (the program, `Furoshiki.exe`) and its cores are distributed together under the
**GNU General Public License, version 3** (`COPYING`). The parts and their terms:

| Part | Where | Licence | Its text |
| --- | --- | --- | --- |
| Furoshiki, the reader layer, the cores' interface | `home/`, `shared/`, `cores/` | GPL-2.0-or-later files, distributed as GPL-3.0 | `COPYING` |
| PlayStation core: DuckStation (a 2021 version, below) with the reader | `station/` | GPL-3.0 | `licenses/DuckStation-LICENSE.txt` |
| PlayStation 2 core: PCSX2 with the reader | `pcsx2/` | GPL-3.0-or-later | `licenses/PCSX2-COPYING.GPLv3.txt`, `licenses/PCSX2-ThirdPartyLicenses.html` |
| PSP core: PPSSPP with the reader | `ppsspp/` | GPL-2.0-or-later | `licenses/PPSSPP-LICENSE.txt` |
| Dreamcast core: Flycast with the reader | `flycast/` | GPLv2 text, distributed as GPL-3.0 (below) | `licenses/Flycast-LICENSE.txt` |
| 3DS core: Azahar with the reader | `azahar/` | GPL-2.0-or-later | `licenses/Azahar-license.txt` |
| PS Vita core: Vita3K with the reader, with OpenSSL's two DLLs | `vita3k/` | GPL-2.0-or-later (OpenSSL: Apache-2.0) | `licenses/Vita3K-COPYING.txt`, `licenses/OpenSSL-LICENSE.txt` |
| FFmpeg, built into the PS Vita core (Vita3K's prebuilt libraries, `vita3k/external/ffmpeg`, from `https://github.com/Vita3K/ffmpeg-core`) | in the core | LGPL-2.1-or-later | `licenses/FFmpeg-LGPL-2.1.txt` |
| Qt 6 (Core, Gui, Widgets, Multimedia, Network), as its own DLLs | the package's `Qt6*.dll` and plugins | LGPL-3.0 | `licenses/LGPL-3.0.txt` with `COPYING` |
| SDL 2, as its own DLL | `SDL2.dll` | zlib | `licenses/SDL2-LICENSE.txt` |
| SDL_GameControllerDB (controller mappings) | `gamecontrollerdb.txt` | zlib | below |
| The compiler's runtime (GCC) | `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libssp-0.dll` | GPL-3.0 with the GCC Runtime Library Exception 3.1 | `COPYING`, `licenses/GCC-Runtime-Library-Exception-3.1.txt` |
| MinGW-w64's winpthreads | `libwinpthread-1.dll` | MIT and BSD-3-Clause | `licenses/winpthreads-COPYING.txt` |
| WebView2's loader (Microsoft's WebView2 SDK), which draws the dictionary popup | `WebView2Loader.dll` | BSD-3-Clause (Microsoft's) | `licenses/WebView2-LICENSE.txt` |
| Direct3D's shader compiler (Microsoft), which the PlayStation and PS2 cores load | `d3dcompiler_47.dll` | Microsoft's redistributable code (below), not free software | |
| Frida's `frida-inject`, which runs Agent's hook scripts, as Frida releases it | `hooks/frida-inject.exe` | wxWindows Library Licence 3.1 (below) | `hooks/frida-COPYING.txt` |
| Lua 5.4 | built in | MIT | below |
| SQLite | built in | public domain | |
| QuaZip | built in | LGPL-2.1 with a static linking exception | `licenses/QuaZip-COPYING.txt` |
| Opus | built in | BSD-3-Clause | `licenses/Opus-COPYING.txt` |
| zlib | built in | zlib | |
| DotGothic16, Zen Kaku Gothic New, IBM Plex Mono, Michroma (the program's typefaces) | built in | SIL Open Font License 1.1 | `licenses/OFL-DotGothic16.txt`, `OFL-ZenKakuGothicNew.txt`, `OFL-IBMPlexMono.txt`, `OFL-Michroma.txt` |
| PPSSPP's typefaces beside its core: Roboto Condensed, Inconsolata | `cores/ppsspp/*.ttf` | SIL Open Font License 1.1 | `licenses/PPSSPP-font-*.txt` |
| The titles' names data, made from VNDB's (`https://vndb.org`) | `titles/`, built in | Open Database License 1.0, contents under the Database Contents License 1.0 | `titles/LICENSE`, `licenses/Names-data-ODbL-DbCL.txt` |

Each emulator brings third-party libraries of its own, built into its core. Their licence files
are in the emulator's folder of the source (`station/dep`, `pcsx2/3rdparty`, `ppsspp/ext`,
`flycast/core/deps`, `azahar/externals`, `vita3k/external`, and the submodules `.gitmodules`
names), and the packages carry them: `licenses/third-party/<emulator>.txt` has every library its
folder bundles, with its licence files, or the notice in its source where it has no file
(`home/tools/licences.py` writes them when a package is made). PCSX2's own list is
`licenses/PCSX2-ThirdPartyLicenses.html`. PCSX2's core uses libraries of PCSX2's own Windows
dependencies (SDL 3, zstd, libpng, libjpeg, libwebp, FreeType, HarfBuzz, shaderc and others)
beside it in `cores/`; that list covers them.

The PlayStation core is DuckStation at its commit `9ee96b3647` of 2021-06-07 ("Android: Add quick
load/save touchscreen buttons"), when DuckStation was under the GPL version 3 (its `LICENSE`, in
`station/`). DuckStation's later code, under other terms since 2024, is not in it.

`d3dcompiler_47.dll` is Microsoft's, from the Windows SDK's redistributable files, which may be
given with a program that uses them; it is not free software and its source is not offered.
Windows 8.1 and later have their own copy. `WebView2Loader.dll` is Microsoft's too, from the
WebView2 SDK, under the BSD-style terms in `licenses/WebView2-LICENSE.txt`. `frida-inject.exe` is
Frida's release build, unchanged (`https://github.com/frida/frida`, the version
`home/tools/fetch-hooks.sh` names): the wxWindows Library Licence lets its object code be given
under these terms; it contains libraries of Frida's own build (GLib and others under the LGPL 2.1
or later, and others under permissive licences), whose source is Frida's at that version.

## The Android app

Furoshiki for Android carries the same reader layer and these cores, built for Android, under the
same terms: PPSSPP, Flycast, Azahar and DuckStation as above; **ARMSX2** (`armsx2/`, PCSX2 for
Android, GPL-3.0-or-later, `armsx2/COPYING.GPLv3`) for the PS2; **Vita3K** (`vita3k/`, GPL-2.0-or-later,
`vita3k/COPYING.txt`) for the PS Vita; **melonDS** (`melonds/`, GPL-3.0-or-later, `melonds/LICENSE`,
with its own free replacement BIOS and firmware, so no file of Nintendo's) for the Nintendo DS. Their third-party libraries' licences are in their folders
and submodules as above, and in the app's `licenses/third-party/`. The app's own libraries from
AndroidX, Google's Material Components, Kotlin and kotlinx.coroutines are Apache-2.0
(`licenses/Apache-2.0.txt`); Lua, SQLite and zlib are built in as in the program. Its typefaces are
the program's (OFL, as above), with PCSX2's Roboto and Roboto Mono (Apache-2.0) and Font Awesome
(`licenses/FontAwesome-LICENSE.txt`). Settings › About › Open-source licences shows these notices
and every licence text the app carries.

Its OCR on the device runs **meikiocr**'s models (rtr46, `https://huggingface.co/rtr46/meiki.text.detect.v0`
and `meiki.txt.recognition.v0`, LGPL-3.0, carried unchanged in `assets/ocr/`: you may replace them
with other versions of the same models) through **ONNX Runtime** (Microsoft, MIT,
`https://github.com/microsoft/onnxruntime`, `libonnxruntime.so`, `licenses/ONNXRuntime-LICENSE.txt`;
the models' licence is `licenses/LGPL-3.0.txt` with `COPYING.txt`). The pipeline that runs them
(`android/app/src/main/java/io/github/sommepo/furoshiki/ocr/MeikiOcr.kt`) follows meikiocr's own
(`https://github.com/rtr46/meikiocr`, Apache-2.0).

## The Linux package

The Linux package (`Furoshiki-linux.tar.gz`) carries the same program and cores, built for
Linux, under the same terms, with these differences:

- Qt 6 comes as its own libraries in `lib/` (LGPL-3.0, `licenses/LGPL-3.0.txt`), with **Qt
  WebEngine** for the dictionary popup: its helper `libexec/QtWebEngineProcess` and its
  `resources/` hold Chromium, under the BSD-3-Clause licence and the licences of Chromium's own
  third-party code, which Qt WebEngine's documentation lists.
- SDL 2 is sdl2-compat over SDL 3, both under the zlib licence (`licenses/SDL2-LICENSE.txt`,
  `licenses/SDL3-LICENSE.txt`).
- `lib/` holds the other libraries the program and the cores load, each as its own file, taken
  from the build machine's conda-forge environments and, for NSS and NSPR (which Qt WebEngine
  loads) and libjpeg-turbo, from Ubuntu's packages. **`licenses/LIBRARIES.txt` lists every one
  with its package, version and licence**, and `licenses/libraries/<package>/` has the texts
  that package ships. They are under permissive licences (MIT, BSD, zlib, Apache-2.0 for
  OpenSSL 3 and shaderc, MPL-2.0 for NSS and NSPR, the Unicode licence for ICU), the LGPL
  (glib, libiconv, libsndfile, LAME, mpg123, keyutils, libudev) or the GPL-3.0 with the GCC
  Runtime Library Exception (libstdc++, libgcc_s, libatomic). rapidyaml (MIT) and shaderc
  (Apache-2.0) are PCSX2's own builds: `licenses/PCSX2-ThirdPartyLicenses.html` has their texts.
- `hooks/frida-inject` is Frida's (the wxWindows Library Licence 3.1,
  `licenses/Frida-COPYING.txt`), as in the Windows package.
- The C library, the graphics drivers, X11, the sound servers' libraries, D-Bus, fontconfig and
  FreeType are the computer's own and are not in the package.

The libraries in `lib/` are separate files: you may replace any of them with a version of your
own (the same major version) and the program will use it. Their source code is that of the
package and version `LIBRARIES.txt` names (conda-forge's feedstocks, Ubuntu's source packages).

## Flycast under version 3

Flycast's `LICENSE` file is the text of the GPL version 2, with no statement of a version
number. Of Flycast's own C and C++ files (`core/` outside `core/deps`, and `shell/libretro`:
549 of them on 2026-10-24), 369 say they may be used under "version 2 of the License, or (at
your option) any later version" (or "GPLv2+"), and none says "version 2 only". 14 carry the
terms of code taken from other projects (BSD-3-Clause from MAME, MIT, zlib), all compatible
with the GPL version 3. The remaining 165 name no licence of their own and are under the
project's `LICENSE`. The GPL version 2 says, in its section 9: *"If the Program does not
specify a version number of this License, you may choose any version ever published by the
Free Software Foundation."* So all of Flycast's code may be used under the GPL version 3, and
is distributed here, with the rest, under it. One file makes it so in any case:
`core/achievements/achievements.cpp` is DuckStation's code under "GPL-3.0 OR CC-BY-NC-ND-4.0",
used here under the GPL version 3, so Flycast with that file can only be distributed under
version 3.

## Source code

The source code of what is free software in the packages is this repository at the release's
tag (`https://github.com/sommepo/furoshiki`, the tag `vX.Y.Z` of the release), with:

- the submodules its `.gitmodules` names, at the commits that tag records, each in its own
  project's repository;
- the libraries the program's build fetches: Qt 6.9.3 (`https://download.qt.io/archive/qt/6.9/6.9.3/single/`),
  SDL 2.32.10, SQLite 3.50.4, QuaZip, Opus and zlib 1.3.1, their versions and addresses in the
  CMake files;
- the prebuilt libraries some cores are built with, each from its project at the version its
  build names: PCSX2's Windows dependencies (`pcsx2/.github/workflows/scripts/windows/build-dependencies.bat`
  lists their sources), Vita3K's FFmpeg (`vita3k/external/ffmpeg`: Vita3K's ffmpeg-core, built
  from FFmpeg's source with its `ffmpeg.patch`), OpenSSL 4.0.1 for Vita3K;
- Frida's source for `frida-inject` (`https://github.com/frida/frida` at its version), and the
  GCC and MinGW-w64 sources of the compiler's runtime (Debian's `gcc-mingw-w64` and `mingw-w64`).

Two files are not free software and have no source: `d3dcompiler_47.dll` and the WebView2 loader
are Microsoft's, given with the program under Microsoft's terms (above). `SOURCE.txt` in the
package names the exact commit. For three years from a release, anyone who received the package
may also ask for the source by opening an issue in that repository; it will be provided there at
no charge beyond the cost of hosting.

Qt and SDL are separate DLLs: you may replace them with versions of your own (same major
version and the same compiler) and the program will use them.

## SDL_GameControllerDB

`gamecontrollerdb.txt` is the community's list of controller mappings for SDL
(`https://github.com/mdqinc/SDL_GameControllerDB`), as PPSSPP carries it (`ppsspp/assets`), under
the zlib licence:

This software is provided 'as-is', without any express or implied warranty. In no event will
the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial
applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote
   the original software. If you use this software in a product, an acknowledgment in the
   product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as
   being the original software.
3. This notice may not be removed or altered from any source distribution.

## Lua

Copyright © 1994–2025 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## zlib

Copyright © 1995–2024 Jean-loup Gailly and Mark Adler. This software is provided 'as-is',
without any express or implied warranty. In no event will the authors be held liable for any
damages arising from the use of this software. Permission is granted to anyone to use this
software for any purpose, including commercial applications, and to alter it and redistribute
it freely, subject to the following restrictions: 1. The origin of this software must not be
misrepresented; you must not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation would be appreciated but
is not required. 2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software. 3. This notice may not be removed or altered
from any source distribution.

## Not included

No BIOS, firmware, keys, games or character tables made from games are in the package or the
repository. Eden (the Switch) is not part of Furoshiki's published builds.
