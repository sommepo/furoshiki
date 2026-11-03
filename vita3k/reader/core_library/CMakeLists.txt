# SPDX-FileCopyrightText: Vita3K Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Vita3K as a core of Furoshiki, the one program (cores/reader_core.h at the top of the
# repository, home/docs/one-program.md): reader_core_vita3k.so, a module Furoshiki loads. Vita3K's
# emulator (its libraries, interface.cpp, which the app builds itself) with vita3k_core.cpp in the
# frontend's place. No Qt, no SDL window, no reader engine: those are the program's. Built by
# tools/reader/build-core.sh with READER_CORE (the top CMakeLists.txt makes every library
# position-independent and hidden then).

set(READER_VITA3K "${CMAKE_SOURCE_DIR}/vita3k")
add_library(reader_core_vita3k MODULE vita3k_core.cpp
	${READER_VITA3K}/interface.cpp ${READER_VITA3K}/performance.cpp)
set_target_properties(reader_core_vita3k PROPERTIES PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(reader_core_vita3k PRIVATE ${READER_VITA3K} ${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/../cores)
target_link_libraries(reader_core_vita3k PRIVATE app audio compat config ctrl dialog display gdbstub
	gxm ime io kernel lang mem miniz modules motion packages patch renderer shader touch util
	psvpfsparser pugixml::pugixml stb SDL3::SDL3 ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})

# What Vita3K reads as it runs, beside the module: cores/vita3k in a package.
if(NOT ANDROID)
add_custom_command(TARGET reader_core_vita3k POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:reader_core_vita3k>/vita3k/data"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${READER_VITA3K}/shaders-builtin" "$<TARGET_FILE_DIR:reader_core_vita3k>/vita3k/shaders-builtin")
endif()

# Only reader_core_get leaves the module: two cores' copies of zlib, fmt or SDL never meet in
# one process, and nothing of the module is taken for the program's own.
if(ANDROID)
	# The Android app's: a "lib" name, as an APK carries only such. Its files go in as the app's
	# assets (android/app/build.gradle.kts), so no copy is made here.
	set_target_properties(reader_core_vita3k PROPERTIES PREFIX "lib")
	target_link_libraries(reader_core_vita3k PRIVATE android log)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(reader_core_vita3k PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
		-Wl,--no-undefined -Wl,-Bsymbolic "-Wl,-z,max-page-size=16384")
elseif(NOT WIN32)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(reader_core_vita3k PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map" -Wl,--no-undefined -Wl,-Bsymbolic)
else()
	# clang-cl, as PCSX2 Reader's: only what is marked for export leaves a DLL.
	set_target_properties(reader_core_vita3k PROPERTIES SUFFIX ".dll")
	# A prebuilt library names the debug runtime, which the toolchain has not and a release build
	# does not want.
	target_link_options(reader_core_vita3k PRIVATE /NODEFAULTLIB:MSVCRTD)
	# The OpenSSL Vita3K's configure fetches is DLLs: they go beside the core, as beside its app.
	add_custom_command(TARGET reader_core_vita3k POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENSSL_ROOT_DIR}/bin/libssl-4-x64.dll"
			"${OPENSSL_ROOT_DIR}/bin/libcrypto-4-x64.dll" "$<TARGET_FILE_DIR:reader_core_vita3k>")
endif()
