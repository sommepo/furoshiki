#!/usr/bin/env bash
# Builds the APK: app/build/outputs/apk/debug/app-debug.apk. The cores come first, from each
# fork's tools/reader/build-core-android.sh.
#   tools/build.sh [arm64-v8a|x86_64] [gradle task] [gradle arguments...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
abi="${1:-arm64-v8a}"; shift || true
task="${1:-:app:assembleDebug}"; shift || true
"$here/../../ppsspp/tools/reader/build-core-android.sh" "$abi"
"$here/../../flycast/tools/reader/build-core-android.sh" "$abi"
"$here/../../azahar/tools/reader/build-core-android.sh" "$abi"
"$here/../../station/tools/reader/build-core-android.sh" "$abi"
"$here/../../station/tools/reader/private-data.sh"
"$here/../../armsx2/tools/reader/build-core-android.sh" "$abi"
"$here/../../armsx2/tools/reader/private-data.sh"
"$here/../../vita3k/tools/reader/build-core-android.sh" "$abi"
. "$here/env.sh"
cd "$here/.."
exec "$limit" -m "${READER_BUILD_MEMORY_MAX:-12G}" ./gradlew --no-daemon --max-workers="${READER_GRADLE_WORKERS:-4}" \
  "-PREADER_ABI=$abi" "-PREADER_NDK=$READER_NDK" "$task" "$@"
