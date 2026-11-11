#!/usr/bin/env bash
# Frida's frida-inject for a build's package (docs/hooks.md): the program that runs 0xDC00's
# Agent scripts in a game for Furoshiki. Downloaded from Frida's own release into the build
# folder's hooks/ (never into Git), where the package scripts take it from; with Frida's licence
# beside it (the wxWindows Library Licence 3.1, which lets its binaries be passed on).
#   tools/fetch-hooks.sh windows|linux [BUILD FOLDER]
# Sizes (16.7.19): Windows 20 MB to fetch, some 50 MB unpacked; Linux 18.5 MB, 67 MB unpacked.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
version=16.7.19
system="${1:?windows or linux}"
case "$system" in
  windows) build="${2:-${READER_HOME_WINDOWS_BUILD:-$root/build-windows}}"; asset="frida-inject-$version-windows-x86_64.exe.xz"; name=frida-inject.exe ;;
  linux) build="${2:-$root/build-desktop}"; asset="frida-inject-$version-linux-x86_64.xz"; name=frida-inject ;;
  *) echo "fetch-hooks: windows or linux" >&2; exit 1 ;;
esac
out="$build/hooks"
mkdir -p "$out"
if [ -s "$out/$name" ] && [ "$(cat "$out/frida-version.txt" 2>/dev/null)" = "$version" ]; then
  echo "fetch-hooks: $out/$name is Frida $version already"
  exit 0
fi
curl -fsSL --retry 2 -o "$out/$name.xz" "https://github.com/frida/frida/releases/download/$version/$asset"
xz -d -f "$out/$name.xz"
chmod +x "$out/$name"
curl -fsSL --retry 2 -o "$out/frida-COPYING.txt" "https://raw.githubusercontent.com/frida/frida/$version/COPYING"
echo "$version" > "$out/frida-version.txt"
echo "fetch-hooks: $out/$name ($(du -h "$out/$name" | cut -f1)), Frida $version"
