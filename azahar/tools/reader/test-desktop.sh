#!/usr/bin/env bash
# Build and run the tests of the desktop reader layer's logic (dictionaries, lookups, cards,
# statistics, the backlog): the repository's shared/reader/desktop/tests and src/reader/desktop/tests, with made-up text.
#   tools/reader/test-desktop.sh [Catch2 arguments...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
"$here/build-desktop.sh" reader_desktop_tests
export LD_LIBRARY_PATH="$READER_QT_LINUX/lib:$READER_QT_SYSLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# No display is needed, and none is there.
export QT_QPA_PLATFORM=offscreen
exec "$here/limit.sh" -m "${READER_TEST_MEMORY_MAX:-2G}" "$root/build-desktop/bin/Release/reader_desktop_tests" "$@"
