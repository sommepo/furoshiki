#!/usr/bin/env bash
# The JVM tests of the reader layer, as this app builds it (no device, no emulator).
#   tools/test.sh [gradle arguments...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
cd "$here/.."
exec "$limit" -m "${READER_BUILD_MEMORY_MAX:-12G}" ./gradlew --no-daemon --max-workers="${READER_GRADLE_WORKERS:-4}" \
  "-PREADER_NDK=$READER_NDK" :app:testDebugUnitTest "$@"
