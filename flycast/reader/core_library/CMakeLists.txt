# SPDX-FileCopyrightText: Flycast Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Flycast Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
# the repository, Reader Home's docs/one-program.md): Flycast's libretro build (LIBRETRO), with
# the libretro host every libretro-made core shares (cores/libretro) and this fork's glue
# (flycast_core.cpp, in reader/core/emulator.cpp's place), as reader_core_flycast.so (.dll).
# Only reader_core_get leaves it. Built by tools/reader/build-core.sh (READER_CORE).

set(READER_CORES_DIR "${CMAKE_SOURCE_DIR}/../cores")
target_sources(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/flycast_core.cpp
	${READER_CORES_DIR}/libretro/libretro_core.cpp
	${READER_CORES_DIR}/libretro/libretro_core.h
	${READER_SHARED}/reader/core/crt.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${READER_CORES_DIR}
	${READER_CORES_DIR}/libretro)
# What Flycast's own files do otherwise in a core (shell/libretro/oslib.cpp: a game's own memory
# unit where Flycast Reader keeps it).
target_compile_definitions(${PROJECT_NAME} PRIVATE READER_CORE)
# The CRT picture is on in an install that was never told otherwise, as in the app.
set_source_files_properties(${READER_SHARED}/reader/core/crt.cpp PROPERTIES
	COMPILE_DEFINITIONS READER_CRT_DEFAULT_ON)
# The host and the glue use C++20 (the reader's headers do); Flycast is C++17.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/flycast_core.cpp
	${READER_CORES_DIR}/libretro/libretro_core.cpp ${READER_SHARED}/reader/core/crt.cpp
	PROPERTIES COMPILE_OPTIONS "-std=gnu++20")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "reader_core_flycast" PREFIX "")
if(ANDROID)
	# The Android app's: a "lib" name, as an APK carries only such, and the picture drawn with
	# OpenGL ES on the app's Surface.
	set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "lib")
	target_link_libraries(${PROJECT_NAME} PRIVATE EGL GLESv3 android log)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
		-Wl,-Bsymbolic "-Wl,-z,max-page-size=16384")
elseif(NOT WIN32)
	# The picture is drawn into the program's X11 window with EGL.
	find_package(X11 REQUIRED)
	find_library(READER_EGL_LIBRARY EGL REQUIRED)
	target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(${PROJECT_NAME} PRIVATE ${READER_EGL_LIBRARY} ${X11_LIBRARIES})
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
		-Wl,-Bsymbolic)
	# RetroAchievements' requests (Flycast's http_client, with curl here).
	find_package(CURL REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
else()
	# RetroAchievements' requests: Flycast's http_client with WinINet on Windows.
	target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 gdi32 wininet)
endif()
