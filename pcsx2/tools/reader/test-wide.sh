#!/usr/bin/env bash
# Build and run the tests of the wide picture's recipes (reader/core_library/wide_patches.cpp),
# alone, without PCSX2. Given a folder of the community's pnach files (PCSX2's patches.zip
# unpacked: ARMSX2's copy is armsx2/ARMSX2/app/src/main/assets/resources/patches.zip), each
# recipe's 16:9 words are also compared with its patch's.
#   tools/reader/test-wide.sh [PATCHES_FOLDER]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${READER_WIDE_TEST_BUILD:-$root/build-wide-test}"
mkdir -p "$out"
"$here/limit.sh" -m 2G "${CXX:-g++}" -std=c++20 -O1 -Wall -Wextra -o "$out/wide_patches_test" \
  "$root/reader/core_library/wide_patches.cpp" "$root/reader/core_library/tests/wide_patches_test.cpp"
exec "$here/limit.sh" -m 1G "$out/wide_patches_test" "$@"
