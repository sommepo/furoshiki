# SPDX-FileCopyrightText: PCSX2 Reader fork contributors
# SPDX-License-Identifier: GPL-3.0+

# The reader fork's own code. The emulator's build includes this folder once, after its own.
#
#   reader_lua      Lua 5.4 (reader/lua), built as C++: its errors are then exceptions, which
#                   the bindings rely on to unwind.
#   reader_core     The text engine: profiles, text events. Knows nothing of the emulator.
#   reader_desktop  The reading logic of the desktop app (dictionaries, lookup, cards,
#                   statistics), free of the emulator too.
#   reader_qt       The reader's windows and the PS2-style menu, and reader/qt/host.cpp, which
#                   ties them to the emulator's Qt frontend.
#   reader/core/emulator.cpp is the one file that ties the engine to the emulator, and is
#   built as part of the emulator's core.
#
# The emulator is built without C++ exceptions; the reader's own libraries are built with
# them, and nothing of theirs throws into the emulator's code.

option(READER_DESKTOP_TESTS "Build the tests of the desktop reading logic" OFF)

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
if(MSVC)
	target_compile_options(reader_lua PRIVATE /EHsc /w)
else()
	target_compile_options(reader_lua PRIVATE -fexceptions -w)
endif()
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
	${READER_SHARED}/reader/core/lua_api.cpp
	${READER_SHARED}/reader/core/lua_api.h
	${READER_SHARED}/reader/core/next_choice.cpp
	${READER_SHARED}/reader/core/next_choice.h
	${READER_SHARED}/reader/core/text_codec.cpp
	${READER_SHARED}/reader/core/text_codec.h
	core/types.h
	core/console.h
)
target_include_directories(reader_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(reader_core PUBLIC reader_lua)
# The CRT picture is on in an install that was never told otherwise (reader/core/crt.cpp):
# this console was played on a tube.
target_compile_definitions(reader_core PRIVATE READER_CRT_DEFAULT_ON)
if(MSVC)
	target_compile_options(reader_core PRIVATE /EHsc)
else()
	target_compile_options(reader_core PRIVATE -fexceptions)
endif()
set_target_properties(reader_core PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)

# Save sync: the Eden Reader fork's engine (archive, remote, engine) and what fits it to the
# PlayStation 2's memory cards (host). Knows nothing of the emulator either. cpp-httplib,
# nlohmann's JSON and a SHA-256 are kept in reader/ext (plain HTTP: the service is in the home
# network). A core has none: the program is the client.
if(NOT READER_CORE)
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
if(WIN32)
	target_link_libraries(reader_save_sync PRIVATE ws2_32)
endif()
if(MSVC)
	target_compile_options(reader_save_sync PRIVATE /EHsc)
else()
	target_compile_options(reader_save_sync PRIVATE -fexceptions)
endif()
set_target_properties(reader_save_sync PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)
endif()

# PCSX2 Reader as a core of Reader Home, the one program (core_library/CMakeLists.txt): the
# emulator without its Qt frontend, the engine and save sync being the program's. Built by
# tools/reader/build-core.sh, with ENABLE_QT_UI off.
option(READER_CORE "Build the emulator as a core of Reader Home (reader_core_pcsx2) instead of the app" OFF)

# The emulator's side.
if(READER_CORE)
	# core_library/pcsx2_core.cpp is emulator.cpp's twin there; the CRT picture's table is the
	# core's own (the program hands its sliders over).
	target_sources(PCSX2 PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/core/emulator.h
		${READER_SHARED}/reader/core/crt.cpp
		${READER_SHARED}/reader/core/crt.h
		${CMAKE_CURRENT_SOURCE_DIR}/core/memory_card.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/memory_card.h
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.h)
	set_source_files_properties(${READER_SHARED}/reader/core/crt.cpp PROPERTIES
		COMPILE_DEFINITIONS READER_CRT_DEFAULT_ON)
	target_include_directories(PCSX2 PRIVATE ${CMAKE_SOURCE_DIR})
	# The page fault handler passes a fault outside the emulator's memory on to the handler
	# before it (common/Linux/LnxHostSys.cpp): in the program another core may have one.
	target_compile_definitions(common PRIVATE READER_CORE)
	# ...and no input source the core switches off is started (pcsx2/Input/InputManager.cpp).
	set_source_files_properties(${CMAKE_SOURCE_DIR}/pcsx2/Input/InputManager.cpp TARGET_DIRECTORY PCSX2
		PROPERTIES COMPILE_DEFINITIONS READER_CORE)
	add_subdirectory(core_library)
	return()
endif()
target_sources(PCSX2 PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/core/emulator.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/core/emulator.h
	${CMAKE_CURRENT_SOURCE_DIR}/core/memory_card.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/core/memory_card.h
	# The CRT picture as one of the emulator's present shaders.
	${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/render/crt_present.h)
target_include_directories(PCSX2 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(PCSX2 PUBLIC reader_core reader_save_sync)
# A published build (READER_SAVE_SYNC=OFF) has no save sync: no page for it, and each game
# uses the memory cards the settings name, as in PCSX2.
option(READER_SAVE_SYNC "Save sync (a service of one's own keeps each game's saves)" ON)
if(NOT READER_SAVE_SYNC)
	target_compile_definitions(PCSX2 PUBLIC READER_NO_SAVE_SYNC)
endif()

if(ENABLE_QT_UI)
	add_subdirectory(desktop)
	if(READER_DESKTOP_TESTS)
		add_subdirectory(save_sync/tests)
	endif()

	set(CMAKE_AUTOMOC ON)
	find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
	add_library(reader_qt STATIC
		${READER_SHARED}/reader/qt/audio_prompt.cpp ${READER_SHARED}/reader/qt/audio_prompt.h
		qt/host.cpp qt/host.h qt/host_commands.cpp qt/install.h
		${READER_SHARED}/reader/qt/lookup_flow.cpp ${READER_SHARED}/reader/qt/lookup_flow.h
		${READER_SHARED}/reader/qt/lookup_popup.cpp ${READER_SHARED}/reader/qt/lookup_popup.h
		${READER_SHARED}/reader/qt/popup_web.cpp ${READER_SHARED}/reader/qt/popup_web.h
		qt/ps2_home.cpp qt/ps2_home.h
		${READER_SHARED}/reader/qt/reader_control.cpp ${READER_SHARED}/reader/qt/reader_control.h
		${READER_SHARED}/reader/qt/reader_controller.cpp ${READER_SHARED}/reader/qt/reader_controller.h
		${READER_SHARED}/reader/qt/reader_crt_page.cpp ${READER_SHARED}/reader/qt/reader_crt_page.h
		${READER_SHARED}/reader/qt/reader_cursor.cpp ${READER_SHARED}/reader/qt/reader_cursor.h
		${READER_SHARED}/reader/qt/reader_dialogs.cpp ${READER_SHARED}/reader/qt/reader_dialogs.h
		${READER_SHARED}/reader/qt/reader_games.cpp ${READER_SHARED}/reader/qt/reader_games.h
		${READER_SHARED}/reader/qt/reader_media.cpp ${READER_SHARED}/reader/qt/reader_media.h
		${READER_SHARED}/reader/qt/reader_overlay.cpp ${READER_SHARED}/reader/qt/reader_overlay.h qt/reader_look.cpp qt/reader_look.h ${READER_SHARED}/reader/qt/reader_crt_frame.cpp ${READER_SHARED}/reader/qt/reader_crt_frame.h
		${READER_SHARED}/reader/qt/reader_pad.cpp ${READER_SHARED}/reader/qt/reader_pad.h
		${READER_SHARED}/reader/qt/reader_link.cpp ${READER_SHARED}/reader/qt/reader_link.h
		${READER_SHARED}/reader/qt/reader_steam.cpp ${READER_SHARED}/reader/qt/reader_steam.h
		${READER_SHARED}/reader/qt/save_sync_dialog.cpp ${READER_SHARED}/reader/qt/save_sync_dialog.h
		${READER_SHARED}/reader/qt/reader_session.cpp ${READER_SHARED}/reader/qt/reader_session.h
	)
	# The bundled profiles, one <SERIAL>.lua per game, as resources of the program.
	# A profile's data file that is made from its game (a character table, *.charset) is not in
	# the repository: tools/reader/private-data.sh puts it here for a build of one's own.
	# A published build (READER_BUNDLE_PROFILES=OFF) comes with none: its scripts are installed
	# from the published list (Reader, Game scripts).
	option(READER_BUNDLE_PROFILES "Build the game scripts in reader/profiles into the app" ON)
	if(READER_BUNDLE_PROFILES)
		file(GLOB READER_PROFILE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/profiles/*.lua"
			"${CMAKE_CURRENT_SOURCE_DIR}/profiles/*.bin" "${CMAKE_CURRENT_SOURCE_DIR}/profiles/*.charset")
		qt_add_resources(reader_qt "reader_profiles"
			PREFIX "/reader/profiles"
			BASE "${CMAKE_CURRENT_SOURCE_DIR}/profiles"
			FILES ${READER_PROFILE_FILES})
	endif()
	target_include_directories(reader_qt PUBLIC ${CMAKE_SOURCE_DIR})
	target_include_directories(reader_qt PRIVATE ${CMAKE_SOURCE_DIR}/pcsx2 ${CMAKE_SOURCE_DIR}/pcsx2-qt)
	target_link_libraries(reader_qt PUBLIC reader_desktop reader_core reader_save_sync Qt6::Widgets Qt6::Gui Qt6::Core
		Qt6::Network)
	target_link_libraries(reader_qt PRIVATE PCSX2_FLAGS PCSX2 SDL3::SDL3)
	# The emulator's input library is SDL 3: the Steam Controller's gamepad is made through it.
	target_compile_definitions(reader_qt PRIVATE READER_SDL_MAJOR=3)
	if(WIN32)
		# The Steam Controller's back buttons are read from the device itself.
		target_link_libraries(reader_qt PRIVATE hid setupapi)
	endif()
	if(MSVC)
		target_compile_options(reader_qt PRIVATE /EHsc)
	else()
		target_compile_options(reader_qt PRIVATE -fexceptions -Wno-shadow)
	endif()
	set_target_properties(reader_qt PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
	target_link_libraries(pcsx2-qt PRIVATE reader_qt)
endif()
