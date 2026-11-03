#!/usr/bin/env bash
# Puts the data files of bundled profiles that are not kept in Git where a build takes them
# in: a game's character table (private/<game>/<SERIAL>.charset) beside its profile in
# reader/profiles. The copies are ignored by Git. The build scripts call this; a build made
# without such a file has the profile, which then says that its table is missing.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
target="$root/reader/profiles"
shopt -s nullglob
for file in "$root"/private/*/[A-Z][A-Z][A-Z][A-Z]-[0-9][0-9][0-9][0-9][0-9].charset; do
  name="$(basename "$file")"
  if ! cmp -s "$file" "$target/$name"; then
    install -m 644 "$file" "$target/$name"
    echo "private-data: $name"
  fi
done
