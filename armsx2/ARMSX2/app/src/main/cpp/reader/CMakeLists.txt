# SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
# SPDX-License-Identifier: GPL-3.0+

# The reader fork's own native code. The emulator's build includes this folder once.
#
#   reader_lua    Lua 5.4 (reader/lua), built as C++: its errors are then exceptions, which
#                 the bindings rely on to unwind.
#   reader_core   The text engine: profiles, text events. Knows nothing of the emulator.
#   reader/core/emulator.cpp ties the engine to the emulator and is built as part of its
#   core; reader/reader_jni.cpp is the bridge to the app's Kotlin side.
#
# The emulator is built without C++ exceptions; these two libraries are built with them, and
# nothing of theirs throws into the emulator's code.

# A core of Furoshiki, for the one Android app (android/ at the top of the repository): the emulator
# without its Java front, the engine and save sync being the program's. core_library/armsx2_core.cpp
# is emulator.cpp's twin there, made from PCSX2 Reader's core. Built by
# tools/reader/build-core-android.sh.
option(READER_CORE "Build the emulator as a core of Furoshiki (reader_core_armsx2) instead of the app" OFF)
if(READER_CORE)
	set(READER_PCSX2 ${READER_SHARED}/../pcsx2/reader/core)
	target_sources(PCSX2 PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/core/emulator.h
		${READER_SHARED}/reader/core/crt.cpp
		${READER_SHARED}/reader/core/crt.h
		${READER_PCSX2}/memory_card.cpp
		${READER_PCSX2}/memory_card.h
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.h)
	set_source_files_properties(${READER_SHARED}/reader/core/crt.cpp PROPERTIES
		COMPILE_DEFINITIONS READER_CRT_DEFAULT_ON)
	target_include_directories(PCSX2 PRIVATE ${CMAKE_SOURCE_DIR} ${READER_SHARED}/.. ${READER_SHARED}/../pcsx2)
	# No input source the core switches off is started (pcsx2/Input/InputManager.cpp): SDL's
	# would want the Java classes of an app of its own.
	set_source_files_properties(${CMAKE_SOURCE_DIR}/pcsx2/Input/InputManager.cpp TARGET_DIRECTORY PCSX2
		PROPERTIES COMPILE_DEFINITIONS READER_CORE)
	add_subdirectory(core_library)
	return()
endif()

set(READER_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua)
set(READER_LUA_SOURCES)
foreach(name lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo ldump lfunc lgc llex
		lmathlib lmem lobject lopcodes lparser lstate lstring lstrlib ltable ltablib ltm
		lundump lutf8lib lvm lzio)
	list(APPEND READER_LUA_SOURCES ${READER_LUA_DIR}/${name}.c)
endforeach()
set_source_files_properties(${READER_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(reader_lua STATIC ${READER_LUA_SOURCES})
target_include_directories(reader_lua SYSTEM PUBLIC ${READER_LUA_DIR})
target_compile_options(reader_lua PRIVATE -fexceptions -w)
set_target_properties(reader_lua PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)

add_library(reader_core STATIC
	${READER_SHARED}/reader/core/audio_tap.cpp
	${READER_SHARED}/reader/core/audio_tap.h
	${READER_SHARED}/reader/core/crt.cpp
	${READER_SHARED}/reader/core/crt.h
	${READER_SHARED}/reader/core/engine.cpp
	${READER_SHARED}/reader/core/engine.h
	${READER_SHARED}/reader/core/input_filter.cpp
	${READER_SHARED}/reader/core/input_filter.h
	${READER_SHARED}/reader/core/next_choice.cpp
	${READER_SHARED}/reader/core/next_choice.h
	${READER_SHARED}/reader/core/lua_api.cpp
	${READER_SHARED}/reader/core/lua_api.h
	${READER_SHARED}/reader/core/text_codec.cpp
	${READER_SHARED}/reader/core/text_codec.h
	core/types.h
)
target_include_directories(reader_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(reader_core PUBLIC reader_lua)
# The CRT picture is on in an install that was never told otherwise (reader/core/crt.cpp):
# this console was played on a tube.
target_compile_definitions(reader_core PRIVATE READER_CRT_DEFAULT_ON)
target_compile_options(reader_core PRIVATE -fexceptions)
set_target_properties(reader_core PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)

# Save sync: PCSX2 Reader's (reader/save_sync there, the Eden Reader fork's engine and what fits
# it to the PlayStation 2's memory cards), copied here unchanged with its libraries in ext/:
# cpp-httplib, nlohmann's JSON and a SHA-256 (plain HTTP: the service is in the home network).
add_library(reader_save_sync STATIC
	${READER_SHARED}/reader/save_sync/archive.cpp
	${READER_SHARED}/reader/save_sync/archive.h
	${READER_SHARED}/reader/save_sync/console_host.cpp
	${READER_SHARED}/reader/save_sync/console_host.h
	${READER_SHARED}/reader/save_sync/engine.cpp
	${READER_SHARED}/reader/save_sync/engine.h
	save_sync/host.cpp
	save_sync/host.h
	${READER_SHARED}/reader/save_sync/naming_3ds.cpp
	${READER_SHARED}/reader/save_sync/naming_3ds.h
	${READER_SHARED}/reader/save_sync/naming_dreamcast.cpp
	${READER_SHARED}/reader/save_sync/naming_dreamcast.h
	${READER_SHARED}/reader/save_sync/naming_ps1.cpp
	${READER_SHARED}/reader/save_sync/naming_ps1.h
	${READER_SHARED}/reader/save_sync/naming_ps2.cpp
	${READER_SHARED}/reader/save_sync/naming_ps2.h
	${READER_SHARED}/reader/save_sync/naming_psp.cpp
	${READER_SHARED}/reader/save_sync/naming_vita.cpp
	${READER_SHARED}/reader/save_sync/naming_switch.cpp
	${READER_SHARED}/reader/save_sync/naming_psp.h
	${READER_SHARED}/reader/save_sync/naming_vita.h
	${READER_SHARED}/reader/save_sync/naming_switch.h
	${READER_SHARED}/reader/save_sync/remote.cpp
	${READER_SHARED}/reader/save_sync/remote.h
	${READER_SHARED}/reader/save_sync/store.cpp
	${READER_SHARED}/reader/save_sync/store.h
	ext/sha256/sha256.cpp
	ext/sha256/sha256.h
)
target_include_directories(reader_save_sync PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(reader_save_sync SYSTEM PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/ext/reader-httplib ${CMAKE_CURRENT_SOURCE_DIR}/ext/reader-json)
find_package(Threads REQUIRED)
target_link_libraries(reader_save_sync PRIVATE ZLIB::ZLIB PUBLIC Threads::Threads)
target_compile_options(reader_save_sync PRIVATE -fexceptions)
set_target_properties(reader_save_sync PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)

# The emulator's side.
target_sources(PCSX2 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/core/emulator.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/core/emulator.h
	# The CRT picture as one of the emulator's present shaders.
	${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.h)
target_include_directories(PCSX2 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(PCSX2 PUBLIC reader_core reader_save_sync)
# A published build (-PreaderSaveSync=false, READER_SAVE_SYNC=OFF) has no save sync: each game
# uses the memory cards the settings name, as in ARMSX2.
option(READER_SAVE_SYNC "Save sync (a service of one's own keeps each game's saves)" ON)
if(NOT READER_SAVE_SYNC)
	target_compile_definitions(PCSX2 PUBLIC READER_NO_SAVE_SYNC)
endif()
