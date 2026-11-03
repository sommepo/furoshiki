#!/usr/bin/env bash
# Run the Linux build without a display, for tests and pictures.
#   tools/run-desktop.sh reader-home|reader-home-tests|reader-home-cores-tests [arguments...]
# Qt's sound module wants PulseAudio's client library; a machine without it names a folder
# that has one in READER_HOME_PULSE_LIB.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# READER_HOME_WEB=1: built with the official Qt, which has Qt WebEngine for the popup page
# (READER_HOME_WEB_QT, by default ~/.local/share/furoshiki-qt/6.9.3/gcc_64, installed with the
# toolchain's aqt), in build-desktop-web. The toolchain's usual Qt has no WebEngine.
if [ "${READER_HOME_WEB:-0}" = 1 ]; then
  export READER_QT_LINUX="${READER_HOME_WEB_QT:-$HOME/.local/share/furoshiki-qt/6.9.3/gcc_64}"
fi
. "$here/env.sh"
root="$(cd "$here/.." && pwd)"
program="$1"; shift
export LD_LIBRARY_PATH="$READER_QT_LINUX/lib:$READER_QT_SYSLIBS:$READER_MESA_PREFIX/lib${READER_HOME_PULSE_LIB:+:$READER_HOME_PULSE_LIB}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
export READER_HOME_FONT="${READER_HOME_FONT:-$READER_PREVIEW_FONT_FILE}"
export QT_QPA_FONTDIR="${QT_QPA_FONTDIR:-$(dirname "$READER_PREVIEW_FONT_FILE")}"
# READER_HOME_PUBLIC=1: the published build (build-desktop-public).
build="$root/build-desktop$([ "${READER_HOME_PUBLIC:-0}" = 1 ] && echo -public || true)$([ "${READER_HOME_WEB:-0}" = 1 ] && echo -web || true)"
exec "$here/limit.sh" -m "${READER_TEST_MEMORY_MAX:-1G}" "$build/$program" "$@"
