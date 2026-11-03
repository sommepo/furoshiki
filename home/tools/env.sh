# Sourced by the scripts here. Furoshiki is built with the tools the reader forks are built
# with (a GCC, cmake and ninja, Qt 6.9 for Linux and for Windows, MinGW): this takes their
# places from a reader fork's own env.sh, the Flycast Reader fork's beside this checkout
# unless READER_FORK_ENV names another.
_home_tools="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
READER_FORK_ENV="${READER_FORK_ENV:-$_home_tools/../../flycast/tools/reader/env.sh}"
if [ ! -f "$READER_FORK_ENV" ]; then
  echo "env.sh: no reader fork's env.sh at $READER_FORK_ENV (set READER_FORK_ENV)" >&2
  return 1 2>/dev/null || exit 1
fi
# shellcheck disable=SC1090
. "$READER_FORK_ENV"
unset _home_tools
