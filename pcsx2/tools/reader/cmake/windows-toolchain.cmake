# SPDX-FileCopyrightText: PCSX2 Reader fork contributors
# SPDX-License-Identifier: GPL-3.0+

# Reader fork: building the desktop app for Windows on Linux, with clang-cl and lld-link (the
# compiler the emulator supports on Windows, in its Microsoft-compatible mode) against
# Microsoft's C runtime and Windows SDK as the tool xwin lays them out (READER_XWIN), and the
# emulator's own package of prebuilt libraries (READER_WINDOWS_DEPS). Used by
# tools/reader/build-windows.sh.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_SYSTEM_VERSION 10.0)

set(_llvm "$ENV{READER_HOST_PREFIX}/bin")
set(_xwin "$ENV{READER_XWIN}")
set(CMAKE_C_COMPILER "${_llvm}/clang-cl")
set(CMAKE_CXX_COMPILER "${_llvm}/clang-cl")
set(CMAKE_LINKER "${_llvm}/lld-link")
set(CMAKE_AR "${_llvm}/llvm-lib")
set(CMAKE_RC_COMPILER "${_llvm}/llvm-rc")
set(CMAKE_MT "${_llvm}/llvm-mt")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# First a folder of links for the headers the sources name in another letter case than
# Microsoft's files have (tools/reader/windows-case.py makes it).
set(_includes
    "/imsvc$ENV{READER_WINDOWS_HOME}/case-links"
    "/imsvc${_xwin}/crt/include"
    "/imsvc${_xwin}/sdk/include/ucrt"
    "/imsvc${_xwin}/sdk/include/um"
    "/imsvc${_xwin}/sdk/include/shared"
    "/imsvc${_xwin}/sdk/include/winrt"
    "/imsvc${_xwin}/sdk/include/cppwinrt")
string(REPLACE ";" " " _includes "${_includes}")
set(CMAKE_C_FLAGS_INIT "--target=x86_64-pc-windows-msvc -fuse-ld=lld-link ${_includes}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc -fuse-ld=lld-link ${_includes}")
set(CMAKE_RC_FLAGS_INIT "/I $ENV{READER_WINDOWS_HOME}/case-links /I ${_xwin}/crt/include /I ${_xwin}/sdk/include/ucrt /I ${_xwin}/sdk/include/um /I ${_xwin}/sdk/include/shared")
set(_libs "/libpath:$ENV{READER_WINDOWS_HOME}/case-links/lib /libpath:${_xwin}/crt/lib/x86_64 /libpath:${_xwin}/sdk/lib/um/x86_64 /libpath:${_xwin}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libs}")

# Libraries and headers come from the Windows side only; programs (moc, the build tools) from
# this machine.
set(CMAKE_FIND_ROOT_PATH "$ENV{READER_WINDOWS_DEPS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# The debug flavour of Microsoft's runtime is not among what xwin fetches: CMake's own trial
# programs are built as the app is.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# The one assembly file of the emulator is in Microsoft's assembler's language: LLVM's
# stand-in for that assembler takes it.
set(CMAKE_ASM_MASM_COMPILER "${CMAKE_CURRENT_LIST_DIR}/llvm-ml64.sh")
