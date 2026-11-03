# SPDX-FileCopyrightText: Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# The reader layer every reader fork shares (shared/reader: the engine, save sync, the desktop
# logic, the Qt windows, the Android app's native bridge), built by each fork from here.
#
# A fork's top CMakeLists.txt sets READER_SHARED to this folder and includes this file before
# its first target. Its own lists then name the shared files as ${READER_SHARED}/reader/...
# beside its own ones (console.h, registers.h, emulator.cpp, host.cpp...), which stay in the
# fork under the same names (reader/core/console.h). Both are found as "reader/core/..." and
# so on: this folder is on the include path, and so is the fork's own. No file may be in both
# (tools/shared.py check).

include_directories(AFTER "${READER_SHARED}")

# The text server (reader/desktop/text_hook_server.*: every line to a WebSocket) is the dev
# line's: a public tree has no source of it, and everything is then built
# without it (READER_NO_TEXT_SERVER), with no switch to set; nor has Furoshiki's published
# build (HOME_PUBLIC) made in this repository. The fork lists name these.
if(EXISTS "${READER_SHARED}/reader/desktop/text_hook_server.cpp" AND NOT HOME_PUBLIC)
	set(READER_TEXT_SERVER_SOURCES "${READER_SHARED}/reader/desktop/text_hook_server.cpp"
		"${READER_SHARED}/reader/desktop/text_hook_server.h")
	set(READER_TEXT_SERVER_TESTS "${READER_SHARED}/reader/desktop/tests/text_hook_server.cpp")
else()
	set(READER_TEXT_SERVER_SOURCES)
	set(READER_TEXT_SERVER_TESTS)
	add_compile_definitions(READER_NO_TEXT_SERVER)
endif()

# READER_JNI_PACKAGE for reader/android/reader_jni_common.cpp, called in the folder whose
# CMakeLists.txt makes the target that builds it: the app's Java package (com.flycast.emulator)
# as JNI writes it in a function's name.
function(reader_jni_package package)
	string(REPLACE "_" "_1" mangled "${package}")
	string(REPLACE "." "_" mangled "${mangled}")
	set_property(SOURCE "${READER_SHARED}/reader/android/reader_jni_common.cpp" APPEND PROPERTY
		COMPILE_DEFINITIONS READER_JNI_PACKAGE=${mangled})
endfunction()
