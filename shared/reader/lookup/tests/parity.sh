#!/usr/bin/env bash
# Builds tests/parity.cpp against Station Reader's desktop library (its build-desktop must exist)
# and runs it on a reader_dict.db: shared/reader/lookup/tests/parity.sh DICT_DB [TEXT_FILE]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../../.." && pwd)"
. "$repo/flycast/tools/reader/env.sh" >/dev/null
fork="$repo/station"
lib="$fork/build-desktop/reader/desktop"
out="${PARITY_BUILD:-$fork/build-desktop/lookup-parity}"
mkdir -p "$out"
qt="$READER_QT_LINUX"
json="$fork/reader/ext/reader-json"
sqlite_inc="$(dirname "$(find "$fork/build-desktop/_deps" -name sqlite3.h | head -1)")"
cxx="$(command -v "$READER_HOST_PREFIX/bin/g++" || command -v g++)"
"$repo/flycast/tools/reader/limit.sh" -m 4G "$cxx" -std=c++20 -O1 -fPIC \
  -I"$repo/shared" -I"$fork" -I"$json" -I"$sqlite_inc" \
  -I"$qt/include" -I"$qt/include/QtCore" -I"$READER_HOST_PREFIX/include" \
  "$here/parity.cpp" "$repo/shared/reader/lookup/dictionary.cpp" "$repo/shared/reader/lookup/deinflector.cpp" \
  "$lib/libreader_desktop.a" "$lib/libreader_sqlite.a" \
  -L"$qt/lib" -L"$READER_HOST_PREFIX/lib" -lQt6Core -lz -ldl -lpthread \
  -Wl,-rpath,"$qt/lib" -Wl,-rpath,"$READER_HOST_PREFIX/lib" \
  -Wl,-rpath,"$READER_QT_SYSLIBS" -Wl,-rpath-link,"$READER_QT_SYSLIBS" -o "$out/parity"
export LD_LIBRARY_PATH="$qt/lib:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$repo/flycast/tools/reader/limit.sh" -m 2G "$out/parity" "$@"
