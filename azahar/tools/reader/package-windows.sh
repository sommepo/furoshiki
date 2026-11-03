#!/usr/bin/env bash
# Make a folder (and a zip of it) of the Windows build that runs on a Windows computer: the
# program, the Qt libraries and plugins it loads, and the compiler's runtime libraries. This
# is what Qt's own deployment tool does on Windows; that tool is a Windows program and cannot
# be run here.
#   tools/reader/package-windows.sh
# Output: build-windows/package/AzaharReader/ and build-windows/package/AzaharReader-windows.zip
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
exe="$root/build-windows/bin/Release/azahar.exe"
out="$root/build-windows/package/AzaharReader"
objdump="$READER_MINGW/bin/x86_64-w64-mingw32-objdump"
strip="$READER_MINGW/bin/x86_64-w64-mingw32-strip"
[ -f "$exe" ] || { echo "no $exe: run tools/reader/build-windows.sh first" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out"
install -m 755 "$exe" "$out/azahar-reader.exe"
"$strip" --strip-unneeded "$out/azahar-reader.exe"

# Where a library the program asks for by name may be found, in this order. Anything that is
# in none of these is Windows' own (kernel32.dll and the like).
# The compiler's folders come first: Qt's folder has the compiler's runtime libraries too
# (libstdc++, libgcc, libwinpthread), from the older compiler Qt was built with, and the
# program needs functions those do not have (Eden Reader's first package took Qt's and did
# not start on Windows).
gcc_dir="$(find "$READER_MINGW/lib/gcc/x86_64-w64-mingw32" -maxdepth 1 -name '*-posix' | sort -V | tail -1)"
[ -n "$gcc_dir" ] || { echo "no MinGW compiler under $READER_MINGW" >&2; exit 1; }
runtime=(
  "$gcc_dir"
  "$READER_MINGW/x86_64-w64-mingw32/lib"
)
search=(
  "${runtime[@]}"
  "$READER_QT_WINDOWS/bin"
)
find_dll() {
  local name="$1" dir found
  for dir in "${search[@]}"; do
    # Windows does not tell upper case from lower in file names.
    found="$(find "$dir" -maxdepth 1 -iname "$name" -print -quit 2>/dev/null)"
    if [ -n "$found" ]; then echo "$found"; return 0; fi
  done
  return 1
}
copy_with_deps() {
  local file="$1" dll path dir
  while read -r dll; do
    [ -n "$dll" ] || continue
    [ -e "$out/$dll" ] && continue
    if path="$(find_dll "$dll")"; then
      install -m 644 "$path" "$out/$(basename "$path")"
      for dir in "${runtime[@]}"; do
        [ "$(dirname "$path")" = "$dir" ] && "$strip" --strip-unneeded "$out/$(basename "$path")"
      done
      copy_with_deps "$path"
    fi
  done < <("$objdump" -p "$file" | awk '/DLL Name:/ {print $3}')
}
copy_with_deps "$exe"

# Plugins are loaded by name at run time, so nothing refers to them: the ones a desktop
# program needs, beside the program as Qt looks for them.
for plugin in platforms/qwindows.dll styles/qmodernwindowsstyle.dll \
              imageformats/qjpeg.dll imageformats/qgif.dll imageformats/qico.dll \
              imageformats/qsvg.dll iconengines/qsvgicon.dll tls/qschannelbackend.dll \
              multimedia/windowsmediaplugin.dll; do
  mkdir -p "$out/$(dirname "$plugin")"
  install -m 644 "$READER_QT_WINDOWS/plugins/$plugin" "$out/$plugin"
  copy_with_deps "$READER_QT_WINDOWS/plugins/$plugin"
done

# What Windows checks before it starts the program, checked here: every function a file of
# the package imports from another file of the package is one that file has.
"$READER_HOST_PREFIX/bin/python3" "$here/pe_check.py" "$out"

cat > "$out/README.txt" <<'TXT'
Azahar Reader for Windows

Start azahar-reader.exe. It keeps its saves and settings in a folder of its own
(%APPDATA%\AzaharReader), apart from an Azahar installed on the same computer.

The game list is the 3DS's HOME menu on two screens: the round buttons along the lower
screen's top have the settings, the reader's menu, the statistics, the games whose text can
be read, the game folders and the view. Add the folder your games are in first.

In a game whose text can be read, click a word of the game's own text to look it up. That
needs a dictionary: Reader > Dictionaries (dictionaries made for Yomitan, as zip files).

Cards: Reader > Anki setup, with Anki open on this computer and its AnkiConnect add-on
installed (in Anki: Tools > Add-ons > Get Add-ons, code 2055492159).

This is a personal build: it is not Azahar, and nothing about it should be reported to the
Azahar project.
TXT

( cd "$out/.." && rm -f AzaharReader-windows.zip && "$READER_HOST_PREFIX/bin/python3" - <<'PY'
import os, zipfile
with zipfile.ZipFile("AzaharReader-windows.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for folder, _, files in os.walk("AzaharReader"):
        for name in sorted(files):
            z.write(os.path.join(folder, name))
PY
)
find "$out" -type f | sed "s|$out/||" | sort | sed 's/^/  /'
du -sh "$out" "$out/../AzaharReader-windows.zip"
