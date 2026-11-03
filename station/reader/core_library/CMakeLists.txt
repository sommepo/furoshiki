# SPDX-FileCopyrightText: Station Reader fork contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Station Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
# the repository, Reader Home's docs/one-program.md): reader_core_station.so (.dll), a module
# Reader Home loads. DuckStation's emulator (core, common, frontend-common's OpenGL display and
# sound) with station_core.cpp in reader/core/emulator.cpp's place, and the CRT picture's
# table (reader/core/crt.cpp) that its display draws with. No frontend, no Qt, no SDL, no
# reader engine: those are the program's. Built by tools/reader/build-core.sh with READER_CORE
# (the top CMakeLists.txt makes every library position-independent and hidden then).

# The emulator's libraries are told more from this folder (their include folders, on Windows
# the system libraries).
cmake_policy(SET CMP0079 NEW)

add_library(reader_core_station MODULE station_core.cpp ${READER_SHARED}/reader/core/crt.cpp)
# Every disc of a game on several discs shares the first disc's card (DuckStation's system.cpp asks).
target_sources(core PRIVATE ${CMAKE_SOURCE_DIR}/reader/core/disc_sets.cpp)
set_target_properties(reader_core_station PROPERTIES PREFIX "" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(reader_core_station PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/../cores)
# The CRT picture is on in an install that was never told otherwise, as in the app; Reader Home
# hands its sliders over (picture.crt) as soon as a game starts.
target_compile_definitions(reader_core_station PRIVATE READER_CRT_DEFAULT_ON)
target_link_libraries(reader_core_station PRIVATE frontend-common core common glad Threads::Threads)
if(USE_X11)
	target_include_directories(reader_core_station PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(reader_core_station PRIVATE ${X11_LIBRARIES})
endif()
# DuckStation's files reach the reader through reader/core/emulator.h.
target_include_directories(core PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(frontend-common PRIVATE ${CMAKE_SOURCE_DIR})

# Only reader_core_get leaves the module: two cores' copies of zlib, fmt or imgui never meet
# in one process, and nothing of the module is taken for the program's own.
if(ANDROID)
	# The Android app's: a "lib" name, as an APK carries only such; the picture on the app's
	# Surface, the sound handed to the app (station_core.cpp).
	set_target_properties(reader_core_station PROPERTIES PREFIX "lib")
	target_link_libraries(reader_core_station PRIVATE android log)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(reader_core_station PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
		-Wl,--no-undefined -Wl,-Bsymbolic "-Wl,-z,max-page-size=16384")
elseif(NOT WIN32)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(reader_core_station PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map" -Wl,--no-undefined -Wl,-Bsymbolic)
else()
	target_link_options(reader_core_station PRIVATE -Wl,--exclude-all-symbols -Wl,--no-undefined)
endif()

# The Windows core made with MinGW (tools/reader/build-core-windows.sh): the system libraries
# DuckStation names in its sources for MSVC alone, and the Direct3D 11 helpers MinGW's header
# lacks, as the app's build has them (reader/CMakeLists.txt).
if(MINGW)
	set(READER_CORE_WINDOWS_LIBS opengl32 ole32 comctl32 winmm avrt imm32 d3dcompiler dxgi d3d11
		dinput8 dxguid winhttp ws2_32 crypt32 uuid shlwapi)
	target_link_libraries(common PUBLIC ${READER_CORE_WINDOWS_LIBS})
	target_sources(common PRIVATE ${CMAKE_SOURCE_DIR}/reader/windows/d3d11_helpers.cpp)
	set_target_properties(reader_core_station PROPERTIES SUFFIX ".dll")
endif()
