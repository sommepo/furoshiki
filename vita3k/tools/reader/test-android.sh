#!/usr/bin/env bash
# Run the JVM tests of the Android reading layer (no device, no emulator, no native build).
#   tools/reader/test-android.sh [--tests 'org.vita3k.emulator.reader.X'] [gradle arguments...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
cd "$here/../../android"
exec "$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}" ./gradlew --no-daemon --max-workers="${READER_GRADLE_WORKERS:-4}" \
  :app:testDebugUnitTest "$@"
