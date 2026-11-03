#!/usr/bin/env bash
# Build the Android app (arm64 unless READER_ABI names others, comma-separated).
#   tools/reader/build-android.sh [gradle task] [gradle arguments...]
# Output: android/app/build/outputs/apk/. No vcpkg: Boost is in external/boost (headers only),
# OpenSSL is the static Android build Eden's CMake keeps (READER_ANDROID_OPENSSL), curl's source
# the one the core's configure fetched (READER_ANDROID_CURL).
# Environment: JAVA_HOME (a JDK 17 or newer) and ANDROID_HOME, see env.sh.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
task="${1:-:app:assembleRelease}"; shift || true
abi="${READER_ABI:-arm64-v8a}"
: "${READER_ANDROID_OPENSSL:=$root/../eden/.cache/cpm/openssl/android-aarch64-3.6.0-1cb0d36b39}"
: "${READER_ANDROID_CURL:=$root/build-core/_deps/curl-src}"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/29.0.14206865"
cmake_args="-DREADER_ANDROID=ON -DOPENSSL_ROOT_DIR=$READER_ANDROID_OPENSSL -DOPENSSL_USE_STATIC_LIBS=ON"
cmake_args+=" -DFETCHCONTENT_SOURCE_DIR_CURL=$READER_ANDROID_CURL -DBUILD_TESTING=OFF -DBUILD_CURL_EXE=OFF"
sysroot="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr"
cmake_args+=" -DZLIB_INCLUDE_DIR=$sysroot/include -DZLIB_LIBRARY=$sysroot/lib/aarch64-linux-android/libz.a"
cmake_args+=" -DCMAKE_JOB_POOLS=compile=${READER_JOBS};link=2 -DCMAKE_JOB_POOL_COMPILE=compile -DCMAKE_JOB_POOL_LINK=link"
cd "$root/android"
exec "$here/limit.sh" -m "${READER_BUILD_MEMORY_MAX:-12G}" ./gradlew --no-daemon --max-workers="${READER_GRADLE_WORKERS:-4}" \
  -PREADER_CMAKE_ARGS="$cmake_args" "-Pandroid.injected.build.abi=$abi" "$task" "$@"
