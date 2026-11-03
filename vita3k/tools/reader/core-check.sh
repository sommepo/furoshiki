#!/usr/bin/env bash
# Runs reader/core_library/core_check.cpp: this fork's core, driven by its C interface, with the
# Core check app (reader/test-app, built with VitaSDK), on Furoshiki's stand-in screen.
#   READER_CHECK_VPK=core_check.vpk READER_CHECK_ELF=core_check tools/reader/core-check.sh
# DISPLAY names an X screen; without one, start ../home/tools/desktop.sh first and give its
# display here. Pictures are drawn by Mesa's software Vulkan.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/env.sh"
root="$(cd "$here/../.." && pwd)"
build="${READER_CORE_BUILD_DIR:-$root/build-core}"
data="${READER_CHECK_DATA:-$root/private/core-check}"
elf="${READER_CHECK_ELF:-$root/private/test-app-build/core_check}"
vpk="${READER_CHECK_VPK:-$elf.vpk}"
offset="$("${VITASDK:-$HOME/.local/opt/vitasdk}/bin/arm-vita-eabi-nm" "$elf" | awk '$3=="say"{print "0x" substr($1,3)}')"
mkdir -p "$data"
g++ -std=c++20 -O1 -I"$root/../cores" -I"$READER_EDEN_TOOLCHAINS/x11-include" \
  "$root/reader/core_library/core_check.cpp" -o "$build/core_check" -ldl -lpthread \
  -L"$READER_VULKAN_PREFIX/lib" -lX11 -Wl,-rpath,"$READER_VULKAN_PREFIX/lib"
icd="$READER_VULKAN_PREFIX/share/vulkan/icd.d/lvp_icd.x86_64.json"
exec "$here/limit.sh" -m "${READER_TEST_MEMORY_MAX:-6G}" env VK_DRIVER_FILES="$icd" VK_ICD_FILENAMES="$icd" \
  LD_LIBRARY_PATH="$READER_VULKAN_PREFIX/lib" SDL_AUDIODRIVER=dummy \
  timeout "${READER_CHECK_TIMEOUT:-240}" "$build/core_check" "$build/bin/reader_core_vita3k.so" "$data" "$vpk" "$offset"
