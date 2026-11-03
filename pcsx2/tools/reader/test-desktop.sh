#!/usr/bin/env bash
# Build and run the tests of the desktop reading logic (dictionaries, lookups, cards,
# statistics, the backlog): reader/desktop/tests, with made-up text.
#   tools/reader/test-desktop.sh [Catch2 arguments...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_DESKTOP_BUILD:-$root/build-desktop}"
export PATH="$READER_HOST_PREFIX/bin:$PATH"
[ -f "$build/build.ninja" ] || "$here/build-desktop.sh" --configure-only
grep -q "READER_DESKTOP_TESTS:BOOL=ON" "$build/CMakeCache.txt" || cmake -DREADER_DESKTOP_TESTS=ON "$build" >/dev/null
"$here/build-desktop.sh" reader_desktop_tests
export LD_LIBRARY_PATH="$READER_HOST_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM=offscreen
exec "$here/limit.sh" -m "${READER_TEST_MEMORY_MAX:-2G}" "$build/bin/reader_desktop_tests" "$@"
