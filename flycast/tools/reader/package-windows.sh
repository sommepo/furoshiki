#!/usr/bin/env bash
# Make a folder of the Windows build that runs on Windows: the program, the libraries it
# needs (Qt, the compiler's runtime) and Qt's plugins; and a zip of it. The emulator's own
# fonts and pictures are inside the program. On Windows the app keeps its settings beside
# the program and everything else in "data" beside it: the folder is the whole app.
#   tools/reader/package-windows.sh
# Output: build-windows/package/FlycastReader/ and build-windows/package/FlycastReader-windows.zip
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_WINDOWS_BUILD:-$root/build-windows}"
exe="$build/flycast.exe"
out="$build/package/FlycastReader"
objdump="$READER_MINGW/bin/x86_64-w64-mingw32-objdump"
strip="$READER_MINGW/bin/x86_64-w64-mingw32-strip"
export LD_LIBRARY_PATH="$READER_MINGW/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
[ -f "$exe" ] || { echo "no $exe: run tools/reader/build-windows.sh first" >&2; exit 1; }

# The way in must be the emulator's own: Qt's (which a sibling's first package hung in) is
# kept out by reader/CMakeLists.txt, and this refuses a program that has it after all.
if grep -q 'libQt6EntryPoint' "$build/build.ninja"; then
  echo "package-windows.sh: Qt's entry point is linked into the program (reader/CMakeLists.txt)" >&2
  exit 1
fi

rm -rf "$out"
mkdir -p "$out"
install -m 755 "$exe" "$out/flycast-reader.exe"
"$strip" --strip-unneeded "$out/flycast-reader.exe"

# The compiler's runtime libraries must be the ones of the compiler the program was built
# with: Qt ships older copies of the same names, and a program linked against the newer ones
# does not start with them.
gcc_dir="$(find "$READER_MINGW/lib/gcc/x86_64-w64-mingw32" -maxdepth 1 -name '*-posix' | sort -V | tail -1)"
[ -n "$gcc_dir" ] || { echo "no MinGW compiler under $READER_MINGW" >&2; exit 1; }
runtime=("$gcc_dir" "$READER_MINGW/x86_64-w64-mingw32/lib")
search=("${runtime[@]}" "$READER_QT_WINDOWS/bin")
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

for plugin in platforms/qwindows.dll styles/qmodernwindowsstyle.dll \
              imageformats/qjpeg.dll imageformats/qgif.dll imageformats/qico.dll \
              tls/qschannelbackend.dll; do
  mkdir -p "$out/$(dirname "$plugin")"
  install -m 644 "$READER_QT_WINDOWS/plugins/$plugin" "$out/$plugin"
  copy_with_deps "$READER_QT_WINDOWS/plugins/$plugin"
done

# Every function a file of the package takes from another file of the package must be there.
"$READER_HOST_PREFIX/bin/python3" "$here/pe_check.py" "$out"

(cd "$out/.." && rm -f FlycastReader-windows.zip && "$READER_HOST_PREFIX/bin/python3" -m zipfile -c FlycastReader-windows.zip FlycastReader)
du -sh "$out" "$out/../FlycastReader-windows.zip"
