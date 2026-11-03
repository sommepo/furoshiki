#!/usr/bin/env bash
# Run the reader's JVM unit tests (no device needed).
#   tools/reader/test-android.sh [--tests 'org.citra.citra_emu.reader.SomeTest'] [gradle args...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
cd "$here/../../src/android"
# The build re-checks GitHub for the Vulkan validation layers every time; once they are here,
# skip that so a network hiccup cannot fail an otherwise local build.
skip=()
[ -f app/build/tmp/Vulkan-ValidationLayers.zip ] && skip=(-x downloadVulkanValidationLayers)
# Gradle, the Kotlin compiler and the native build all run under one memory limit (see limit.sh).
exec "$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}" ./gradlew --no-daemon --max-workers="${READER_GRADLE_WORKERS:-4}" \
  "${skip[@]}" :app:testVanillaDebugUnitTest "$@"
