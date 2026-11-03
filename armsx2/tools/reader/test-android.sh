#!/usr/bin/env bash
# Run the tests of the Android reading layer off the device (JVM, Robolectric).
#   tools/reader/test-android.sh [--tests 'com.armsx2.reader.X']
# With READER_PREVIEW_DIR set, tests that draw screens write pictures there.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
cd "$root/ARMSX2"
[ -f local.properties ] || echo "sdk.dir=$ANDROID_HOME" > local.properties
exec "$here/limit.sh" -m "${READER_TEST_MEMORY_MAX:-8G}" ./gradlew --no-daemon --max-workers="$READER_JOBS" :app:testDebugUnitTest "$@"
