# Source this file: toolchain locations for the reader fork on this machine.
# Everything the desktop build needs is in one folder outside the repository (READER_HOME,
# ~/.local/share/ps2-reader unless set): a Clang, Qt 6.10, SDL3 and the emulator's other
# libraries, as packages of conda-forge unpacked there without root, plus KDDockWidgets and
# libbacktrace built from source into the same place (docs/reader/desktop.md says how).
export READER_HOME="${READER_HOME:-$HOME/.local/share/ps2-reader}"
export READER_HOST_PREFIX="${READER_HOST_PREFIX:-$READER_HOME/host}"
export CCACHE_DIR="${CCACHE_DIR:-$READER_HOME/ccache}"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-10G}"
# A font with Japanese in it, for the desktop app when it is run without a display.
export READER_PREVIEW_FONT_FILE="${READER_PREVIEW_FONT_FILE:-$HOME/.local/share/azahar-reader/fonts/NotoSansJP-Regular.otf}"
# Parallel compile jobs. This is a shared home server: stay well under 16 cores.
export READER_JOBS="${READER_JOBS:-8}"
# The Windows build, cross-compiled here (tools/reader/build-windows.sh): Microsoft's C
# runtime and Windows SDK as xwin lays them out, the emulator's package of prebuilt Windows
# libraries, and a Qt for this machine of the same version as that package's, for its tools.
export READER_WINDOWS_HOME="${READER_WINDOWS_HOME:-$READER_HOME/windows}"
export READER_XWIN="${READER_XWIN:-$READER_WINDOWS_HOME/xwin-sdk}"
export READER_WINDOWS_DEPS="${READER_WINDOWS_DEPS:-$READER_WINDOWS_HOME/deps-x/deps}"
export READER_QT_HOST="${READER_QT_HOST:-$READER_WINDOWS_HOME/qt-host}"
