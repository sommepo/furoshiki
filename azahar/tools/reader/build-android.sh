#!/usr/bin/env bash
# Build the Android app (default: arm64-v8a, relWithDebInfo, bounded jobs).
#   tools/reader/build-android.sh [gradle task] [extra gradle args...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
task="${1:-:app:assembleVanillaRelWithDebInfo}"; shift || true
abi="${READER_ABI:-arm64-v8a}"
lto="${READER_LTO:-OFF}"
cmake_args="-DCMAKE_JOB_POOLS=compile=${READER_JOBS};link=2 -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link -DENABLE_LTO=${lto}"
cd "$here/../../src/android"
# The build re-checks GitHub for the Vulkan validation layers every time; once they are here,
# skip that so a network hiccup cannot fail an otherwise local build.
skip=()
[ -f app/build/tmp/Vulkan-ValidationLayers.zip ] && skip=(-x downloadVulkanValidationLayers)
# Gradle, the Kotlin compiler and the native build all run under one memory limit (see limit.sh).
exec "$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}" ./gradlew --no-daemon --max-workers="${READER_GRADLE_WORKERS:-4}" \
  "${skip[@]}" -PreaderAbi="$abi" -PreaderCmakeArgs="$cmake_args" "$task" "$@"
