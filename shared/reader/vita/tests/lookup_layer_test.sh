#!/usr/bin/env bash
# Builds tests/lookup_layer_test.cpp on a computer, with the lookup's SQLite from Station Reader's
# desktop build (its build-desktop must exist, as for shared/reader/lookup/tests/parity.sh), and
# runs it on a reader_dict.db: shared/reader/vita/tests/lookup_layer_test.sh DICT_DB
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../../.." && pwd)"
. "$repo/flycast/tools/reader/env.sh" >/dev/null
fork="$repo/station"
lib="$fork/build-desktop/reader/desktop"
out="${LOOKUP_LAYER_BUILD:-$fork/build-desktop/vita-lookup-layer}"
mkdir -p "$out"
json="$fork/reader/ext/reader-json"
sqlite_inc="$(dirname "$(find "$fork/build-desktop/_deps" -name sqlite3.h | head -1)")"
cxx="$(command -v "$READER_HOST_PREFIX/bin/g++" || command -v g++)"
"$repo/flycast/tools/reader/limit.sh" -m 4G "$cxx" -std=c++20 -O1 -pthread \
  -I"$repo/shared" -I"$fork" -I"$json" -I"$sqlite_inc" -I"$READER_HOST_PREFIX/include" \
  "$here/lookup_layer_test.cpp" "$repo/shared/reader/vita/lookup_layer.cpp" "$repo/shared/reader/vita/settings.cpp" \
  "$repo/shared/reader/lookup/dictionary.cpp" "$repo/shared/reader/lookup/deinflector.cpp" \
  "$lib/libreader_sqlite.a" -L"$READER_HOST_PREFIX/lib" -lz -ldl \
  -Wl,-rpath,"$READER_HOST_PREFIX/lib" -o "$out/lookup_layer_test"
exec "$repo/flycast/tools/reader/limit.sh" -m 2G "$out/lookup_layer_test" "$@"
