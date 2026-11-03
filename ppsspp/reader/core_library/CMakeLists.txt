# SPDX-FileCopyrightText: PPSSPP Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# PPSSPP Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
# the repository, Reader Home's docs/one-program.md): PPSSPP's libretro build (LIBRETRO), with
# the libretro host every libretro-made core shares (cores/libretro) and this fork's glue
# (ppsspp_core.cpp, in reader/core/emulator.cpp's place), as reader_core_ppsspp.so (.dll).
# Only reader_core_get leaves it. Built by tools/reader/build-core.sh (READER_CORE).

set(READER_CORES_DIR "${CMAKE_SOURCE_DIR}/../cores")
target_sources(ppsspp_libretro PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/ppsspp_core.cpp
	${READER_CORES_DIR}/libretro/libretro_core.cpp
	${READER_CORES_DIR}/libretro/libretro_core.h
	${READER_SHARED}/reader/core/crt.cpp)
target_include_directories(ppsspp_libretro PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libretro
	${READER_CORES_DIR} ${READER_CORES_DIR}/libretro)
# The CRT picture is on in an install that was never told otherwise, as in the app.
set_source_files_properties(${READER_SHARED}/reader/core/crt.cpp PROPERTIES
	COMPILE_DEFINITIONS READER_CRT_DEFAULT_ON)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ppsspp_core.cpp
	${READER_CORES_DIR}/libretro/libretro_core.cpp ${READER_SHARED}/reader/core/crt.cpp
	PROPERTIES COMPILE_OPTIONS "-std=gnu++20")
set_target_properties(ppsspp_libretro PROPERTIES OUTPUT_NAME "reader_core_ppsspp" PREFIX "")
if(ANDROID)
	# The Android app's: a "lib" name, as an APK carries only such, and the picture drawn with
	# OpenGL ES on the app's Surface.
	set_target_properties(ppsspp_libretro PROPERTIES PREFIX "lib")
	target_link_libraries(ppsspp_libretro EGL GLESv3 android log)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(ppsspp_libretro PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
		"-Wl,-z,max-page-size=16384")
elseif(NOT WIN32)
	# The picture is drawn into the program's X11 window with EGL.
	find_package(X11 REQUIRED)
	find_library(READER_EGL_LIBRARY EGL REQUIRED)
	target_include_directories(ppsspp_libretro PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(ppsspp_libretro ${READER_EGL_LIBRARY} ${X11_LIBRARIES})
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(ppsspp_libretro PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map")
else()
	target_link_libraries(ppsspp_libretro opengl32 gdi32)
endif()
