# Furoshiki: licences and notices

Furoshiki (the program, `reader-home.exe`) and its cores are distributed together under the
**GNU General Public License, version 3** (`COPYING`). The parts and their terms:

| Part | Where | Licence | Its text |
| --- | --- | --- | --- |
| Furoshiki, the reader layer, the cores' interface | `home/`, `shared/`, `cores/` | GPL-2.0-or-later files, distributed as GPL-3.0 | `COPYING` |
| PlayStation core: DuckStation (a 2021 version) with the reader | `station/` | GPL-3.0 | `licenses/DuckStation-LICENSE.txt` |
| PlayStation 2 core: PCSX2 with the reader | `pcsx2/` | GPL-3.0-or-later | `licenses/PCSX2-COPYING.GPLv3.txt`, `licenses/PCSX2-ThirdPartyLicenses.html` |
| PSP core: PPSSPP with the reader | `ppsspp/` | GPL-2.0-or-later | `licenses/PPSSPP-LICENSE.txt` |
| Dreamcast core: Flycast with the reader | `flycast/` | GPLv2 text, distributed as GPL-3.0 (below) | `licenses/Flycast-LICENSE.txt` |
| 3DS core: Azahar with the reader | `azahar/` | GPL-2.0-or-later | `licenses/Azahar-license.txt` |
| Qt 6 (Core, Gui, Widgets, Multimedia, Network), as its own DLLs | the package's `Qt6*.dll` and plugins | LGPL-3.0 | `licenses/LGPL-3.0.txt` with `COPYING` |
| SDL 2, as its own DLL | `SDL2.dll` | zlib | `licenses/SDL2-LICENSE.txt` |
| The compiler's runtime (MinGW-w64, GCC) | `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` | GPL-3.0 with the GCC Runtime Library Exception; MinGW-w64's own terms | |
| Lua 5.4 | built in | MIT | below |
| SQLite | built in | public domain | |
| QuaZip | built in | LGPL-2.1 with a static linking exception | `licenses/QuaZip-COPYING.txt` |
| Opus | built in | BSD-3-Clause | `licenses/Opus-COPYING.txt` |
| zlib | built in | zlib | |
| DotGothic16 (the program's typeface) | built in | SIL Open Font License 1.1 | `licenses/OFL-DotGothic16.txt` |

Each emulator brings third-party libraries of its own, built into its core: their licence files
are in the emulator's folder of the source (`station/dep`, `pcsx2/3rdparty`, `ppsspp/ext`,
`flycast/core/deps`, `azahar/externals`, and the submodules `.gitmodules` names), and PCSX2's
list of them is in `licenses/PCSX2-ThirdPartyLicenses.html`. PCSX2's core uses libraries of
PCSX2's own Windows dependencies (SDL 3, zstd, libpng, libjpeg, libwebp, FreeType, HarfBuzz,
shaderc and others) beside it in `cores/`; that list covers them.

## The Android app

Furoshiki for Android carries the same reader layer and these cores, built for Android, under the
same terms: PPSSPP, Flycast, Azahar and DuckStation as above; **ARMSX2** (`armsx2/`, PCSX2 for
Android, GPL-3.0-or-later, `armsx2/COPYING.GPLv3`) for the PS2; **Vita3K** (`vita3k/`, GPL-2.0-or-later,
`vita3k/COPYING.txt`) for the PS Vita. Their third-party libraries' licences are in their folders
and submodules as above. The app's own libraries from AndroidX and Google's Material Components
are Apache-2.0; Lua, SQLite and zlib are built in as in the program.

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
is distributed here, with the rest, under it.

## Source code

The complete corresponding source code of everything in the Windows package is this
repository at the release's tag (`https://github.com/sommepo/furoshiki`, the tag `vX.Y.Z` of
the release), with the submodules its `.gitmodules` names at the commits that tag records, and
the libraries the build fetches (Qt 6.9.3 from the Qt Company's archives, SDL 2.32.10,
SQLite 3.50.4, QuaZip, Opus, zlib 1.3.1: their versions and addresses are in the CMake files).
`SOURCE.txt` in the package names the exact commit. For three years from a release, anyone
who received the package may also ask for the source by opening an issue in that repository;
it will be provided there at no charge beyond the cost of hosting.

Qt and SDL are separate DLLs: you may replace them with versions of your own (same major
version and the same compiler) and the program will use them.

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
