#!/usr/bin/env bash
# Build the Android app (arm64).
#   tools/reader/build-android.sh [gradle tasks...]      (default: :app:assembleDebug)
# Output: ARMSX2/app/build/outputs/apk/debug/
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
"$here/private-data.sh"
# The shader compiler's own parts (SPIRV-Tools, glslang and the rest) are not in the
# repository: its script fetches the commits its DEPS file names, once.
shaderc="$root/ARMSX2/app/src/main/cpp/3rdparty/shaderc"
[ -d "$shaderc/third_party/spirv-tools" ] || (cd "$shaderc" && python3 utils/git-sync-deps)
# The emulator's build looks for two of them under the names their projects have.
ln -sfn spirv-tools "$shaderc/third_party/SPIRV-Tools"
ln -sfn spirv-headers "$shaderc/third_party/SPIRV-Headers"
cd "$root/ARMSX2"
[ -f local.properties ] || echo "sdk.dir=$ANDROID_HOME" > local.properties
export CMAKE_BUILD_PARALLEL_LEVEL="$READER_JOBS"
exec "$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-16G}" ./gradlew --no-daemon --max-workers="$READER_JOBS" "${@:-:app:assembleDebug}"
