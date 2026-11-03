#!/usr/bin/env bash
# Build and run the C++ tests of the reader (text engine, JIT hooks, bundled profiles).
#   tools/reader/test-host.sh [Catch2 arguments...]      default: "[reader]"
# The tests run under a memory limit and a time limit: code under test that runs away ends
# itself instead of taking the machine with it.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
"$here/build-host.sh" tests
if [ $# -eq 0 ]; then
  set -- "[reader]"
fi
exec "$here/limit.sh" -m "${READER_TEST_MEMORY_MAX:-2G}" \
  timeout --signal=KILL "${READER_TEST_TIMEOUT:-600}" "$root/build-host/bin/Release/tests" "$@"
