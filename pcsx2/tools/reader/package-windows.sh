#!/usr/bin/env bash
# Make a folder (and a zip of it) of the Windows build that runs on a Windows computer: the
# program, the emulator's resources, the libraries it is linked with and the Qt plugins it
# loads. This is what upstream's install step and Qt's deployment tool do on Windows; that
# tool is a Windows program and cannot be run here.
#   tools/reader/package-windows.sh
# Output: build-windows/package/PCSX2Reader/ and build-windows/package/PCSX2Reader-windows.zip
# Microsoft's C++ runtime (vcruntime140.dll, msvcp140.dll) is not in the package: it is
# Microsoft's to hand out (the "Visual C++ Redistributable"), and the app says so itself
# when it is missing or too old.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_WINDOWS_BUILD:-$root/build-windows}"
built="$build/pcsx2-qt"
exe="$built/pcsx2-qt.exe"
out="$build/package/PCSX2Reader"
deps="$READER_WINDOWS_DEPS"
objdump="$READER_HOST_PREFIX/bin/llvm-objdump"
[ -f "$exe" ] || { echo "no $exe: run tools/reader/build-windows.sh first" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out"
install -m 755 "$exe" "$out/pcsx2-reader.exe"
cp -r "$built/resources" "$out/resources"
# Upstream leaves this font out on Windows, which has an emoji font of its own.
rm -f "$out"/resources/fonts/Twemoji*
if [ -n "$(ls -A "$built/translations" 2>/dev/null)" ]; then cp -r "$built/translations" "$out/translations"; fi

# A library the program asks for by name is taken from the emulator's package of Windows
# libraries; anything that is not there is Windows' own (or Microsoft's C++ runtime).
copy_with_deps() {
  local file="$1" dll path
  while read -r dll; do
    [ -n "$dll" ] || continue
    path="$(find "$deps/bin" -maxdepth 1 -iname "$dll" -print -quit 2>/dev/null)"
    [ -n "$path" ] || continue
    [ -e "$out/$(basename "$path")" ] && continue
    install -m 644 "$path" "$out/$(basename "$path")"
    copy_with_deps "$path"
  done < <("$objdump" -p "$file" | awk '/DLL Name:/ {print $3}')
}
copy_with_deps "$exe"

# Loaded by name at run time, so nothing refers to them: the libraries upstream's install
# step names (the shader compiler for Vulkan, the video capture's codecs, Direct3D 12's own
# newer runtime), where the package of libraries has them.
for name in shaderc_shared.dll libsharpyuv.dll libwebpdemux.dll libwebpmux.dll harfbuzz.dll; do
  if [ -f "$deps/bin/$name" ]; then
    install -m 644 "$deps/bin/$name" "$out/$name"
    copy_with_deps "$deps/bin/$name"
  fi
done
for name in "$deps"/bin/avcodec-*.dll "$deps"/bin/avformat-*.dll "$deps"/bin/avutil-*.dll \
            "$deps"/bin/swresample-*.dll "$deps"/bin/swscale-*.dll; do
  [ -f "$name" ] || continue
  install -m 644 "$name" "$out/$(basename "$name")"
  copy_with_deps "$name"
done
if [ -d "$deps/bin/D3D12" ]; then
  mkdir -p "$out/D3D12"
  for name in D3D12Core.dll d3d12SDKLayers.dll; do
    [ -f "$deps/bin/D3D12/$name" ] && install -m 644 "$deps/bin/D3D12/$name" "$out/D3D12/$name"
  done
fi

# Qt's plugins, where upstream's package has them.
for plugin in platforms/qwindows.dll styles/qmodernwindowsstyle.dll \
              imageformats/qjpeg.dll imageformats/qgif.dll imageformats/qico.dll \
              imageformats/qsvg.dll imageformats/qwebp.dll iconengines/qsvgicon.dll \
              tls/qschannelbackend.dll tls/qcertonlybackend.dll \
              networkinformation/qnetworklistmanager.dll; do
  mkdir -p "$out/QtPlugins/$(dirname "$plugin")"
  install -m 644 "$deps/plugins/$plugin" "$out/QtPlugins/$plugin"
  copy_with_deps "$deps/plugins/$plugin"
done
printf '[Paths]\nPlugins = ./QtPlugins\n' > "$out/qt.conf"

# What Windows checks before it starts the program, checked here: every function a file of
# the package imports from another file of the package is one that file has.
python3 "$here/pe_check.py" "$out"

cat > "$out/README.txt" <<'TXT'
PCSX2 Reader for Windows

Start pcsx2-reader.exe. It keeps its settings, memory cards and savestates in a folder of
its own (Documents\PCSX2Reader), apart from a PCSX2 installed on the same computer. If
Windows says a file like VCRUNTIME140.dll or MSVCP140.dll is missing, install Microsoft's
"Visual C++ Redistributable" (x64) from Microsoft's website and start it again.

It needs the BIOS of your own PlayStation 2 (the first start asks for the folder it is in)
and your games as disc images.

The game list is the PS2's own menu: Browser has the games, System Configuration the
reader's windows and the emulator's settings. Arrow keys, Enter, Esc; or the mouse.

A game's text is read by a game script written for that game. Reader > Game scripts (or
Game Scripts under System Configuration) installs them from the list published with the app,
or from a file of your own.

In a game whose text can be read (a book on its picture in the Browser), click a word of
the game's own text to look it up. That needs a dictionary: Reader > Dictionaries
(dictionaries made for Yomitan, as zip files).

Cards: Reader > Anki setup, with Anki open on this computer and its AnkiConnect add-on
installed (in Anki: Tools > Add-ons > Get Add-ons, code 2055492159).

This is PCSX2 Reader (https://github.com/sommepo/pcsx2-reader): it is not PCSX2, and
nothing about it should be reported to the PCSX2 project.
TXT

( cd "$out/.." && rm -f PCSX2Reader-windows.zip && python3 - <<'PY'
import os, zipfile
with zipfile.ZipFile("PCSX2Reader-windows.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for folder, _, files in os.walk("PCSX2Reader"):
        for name in sorted(files):
            z.write(os.path.join(folder, name))
PY
)
find "$out" -type f -not -path "*/resources/*" | sed "s|$out/||" | sort | sed 's/^/  /'
du -sh "$out" "$out/../PCSX2Reader-windows.zip"
