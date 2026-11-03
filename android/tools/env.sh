#!/usr/bin/env bash
# The toolchains are the reader forks': the Flycast fork's env.sh names them. Sourced by build.sh
# and test.sh.
android_tools="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$android_tools/../../flycast/tools/reader/env.sh"
export READER_NDK="${READER_NDK:-28.2.13676358}"
export TMPDIR="${TMPDIR_ANDROID:-$android_tools/../app/build/tmp-tools}"
mkdir -p "$TMPDIR"
limit="$android_tools/../../flycast/tools/reader/limit.sh"
