# SPDX-FileCopyrightText: Station Reader fork contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The reader fork's own code. The emulator's build includes this folder once, at its end
# (CMakeLists.txt at the top), after READER_APP names the program the reader goes into: the
# desktop app (duckstation-nogui) or the Android app's library.
#
#   reader_lua      Lua 5.4 (reader/lua), built as C++: its errors are then exceptions, which
#                   the bindings rely on to unwind.
#   reader_core     The text engine: profiles, text events. Knows nothing of the emulator.
#   reader_emu      reader/core/emulator.cpp, the one file that ties the engine to the
#                   emulator. A library of its own because the engine's headers want C++20
#                   and the emulator is built as C++17. The emulator's core calls it (at its
#                   "Reader fork" comments) and it calls the core: the two are linked both ways.

option(READER_TESTS "Build the reader's C++ tests" OFF)
option(READER_QT "Build the desktop reading layer (its windows are Qt's)" OFF)
option(READER_SAVE_SYNC "Build save sync in (a published build has none)" ON)

# The emulator's core reaches the reader's engine from another folder.
cmake_policy(SET CMP0079 NEW)

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
target_compile_options(reader_core PRIVATE -fexceptions)
set_target_properties(reader_core PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)

# Save sync: the Eden Reader fork's engine (archive, remote, engine) and what fits it to the
# PlayStation's memory cards (host). Knows nothing of the emulator either. cpp-httplib,
# nlohmann's JSON and a SHA-256 are kept in reader/ext (plain HTTP: the service is in the home
# network).
if(READER_SAVE_SYNC)
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
# The emulator's own zlib (dep/zlib).
target_link_libraries(reader_save_sync PRIVATE zlib)
if(WIN32)
	target_link_libraries(reader_save_sync PRIVATE ws2_32)
endif()
target_compile_options(reader_save_sync PRIVATE -fexceptions)
set_target_properties(reader_save_sync PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)
endif()

# Save sync is linked only where it is wanted: a build without it (READER_SAVE_SYNC=OFF, the
# published ones) has none of its code, not even what the linker would take along unasked.
if(READER_SAVE_SYNC)
	set(READER_SAVE_SYNC_LIB reader_save_sync)
else()
	set(READER_SAVE_SYNC_LIB)
endif()

# The emulator's side.
add_library(reader_emu STATIC core/emulator.cpp core/emulator.h)
target_include_directories(reader_emu PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(reader_emu PUBLIC reader_core ${READER_SAVE_SYNC_LIB} PRIVATE common)
if(NOT READER_SAVE_SYNC)
	target_compile_definitions(reader_emu PRIVATE READER_NO_SAVE_SYNC)
endif()
set_target_properties(reader_emu PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)
target_include_directories(core PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(core PUBLIC reader_emu)
# Every disc of a game on several discs shares the first disc's card (system.cpp asks).
target_sources(core PRIVATE ${CMAKE_SOURCE_DIR}/reader/core/disc_sets.cpp)
target_include_directories(${READER_APP} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${READER_APP} PRIVATE reader_emu)

# The CRT picture (core/crt.h) in the emulator's display: its OpenGL display draws the game
# with the reader's shader while that is on (src/frontend-common/opengl_host_display.cpp).
if(TARGET frontend-common)
	target_include_directories(frontend-common PRIVATE ${CMAKE_SOURCE_DIR})
	target_link_libraries(frontend-common PRIVATE reader_core)
	# The PlayStation-style home in the full-screen interface (ui/ps_home), and the console's
	# colours there (src/frontend-common/fullscreen_ui.cpp and imgui_fullscreen.h call it).
	target_sources(frontend-common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui/ps_home.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/ui/ps_home.h)
	if(NOT READER_SAVE_SYNC)
		set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ui/ps_home.cpp TARGET_DIRECTORY frontend-common
			PROPERTIES COMPILE_DEFINITIONS READER_NO_SAVE_SYNC)
	endif()
endif()

# The Android app's bridge to its Kotlin layer (reader/android: reader_jni.cpp is this fork's
# own, reader_jni_common.cpp the same in every reader fork), in the app's library
# (duckstation-native, android/app/src/cpp). Its objects go into the library itself, so that the
# linker keeps every JNI function though nothing in the library calls them.
if(ANDROID)
	add_library(reader_android OBJECT android/reader_jni.cpp ${READER_SHARED}/reader/android/reader_jni_common.cpp)
	reader_jni_package(com.github.stenzek.duckstation)
	target_include_directories(reader_android PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
	target_link_libraries(reader_android PRIVATE reader_core ${READER_SAVE_SYNC_LIB} core common)
	if(NOT READER_SAVE_SYNC)
		target_compile_definitions(reader_android PRIVATE READER_NO_SAVE_SYNC)
	endif()
	target_compile_options(reader_android PRIVATE -fexceptions)
	set_target_properties(reader_android PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
		POSITION_INDEPENDENT_CODE ON)
	target_sources(${READER_APP} PRIVATE $<TARGET_OBJECTS:reader_android>)
endif()

# The desktop reading layer: the reading logic, free of the emulator (dictionaries, lookup,
# cards, statistics, the backlog database; tests in desktop/tests), and the reader's windows,
# which are Qt's and lie over the emulator's own window. reader/qt/host.cpp ties them to the
# emulator and reader/qt/install.h is all the emulator's files see of them.
if(READER_QT)
	add_subdirectory(desktop)

	set(CMAKE_AUTOMOC ON)
	find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
	add_library(reader_qt STATIC
		${READER_SHARED}/reader/qt/audio_prompt.cpp ${READER_SHARED}/reader/qt/audio_prompt.h
		qt/host.cpp qt/host.h qt/install.h
		${READER_SHARED}/reader/qt/lookup_flow.cpp ${READER_SHARED}/reader/qt/lookup_flow.h
		${READER_SHARED}/reader/qt/lookup_popup.cpp ${READER_SHARED}/reader/qt/lookup_popup.h
		${READER_SHARED}/reader/qt/popup_web.cpp ${READER_SHARED}/reader/qt/popup_web.h
		${READER_SHARED}/reader/qt/reader_control.cpp ${READER_SHARED}/reader/qt/reader_control.h
		${READER_SHARED}/reader/qt/reader_controller.cpp ${READER_SHARED}/reader/qt/reader_controller.h
		${READER_SHARED}/reader/qt/reader_crt_page.cpp ${READER_SHARED}/reader/qt/reader_crt_page.h
		${READER_SHARED}/reader/qt/reader_cursor.cpp ${READER_SHARED}/reader/qt/reader_cursor.h
		${READER_SHARED}/reader/qt/reader_dialogs.cpp ${READER_SHARED}/reader/qt/reader_dialogs.h
		${READER_SHARED}/reader/qt/reader_games.cpp ${READER_SHARED}/reader/qt/reader_games.h
		${READER_SHARED}/reader/qt/reader_link.cpp ${READER_SHARED}/reader/qt/reader_link.h
		${READER_SHARED}/reader/qt/reader_media.cpp ${READER_SHARED}/reader/qt/reader_media.h
		${READER_SHARED}/reader/qt/reader_overlay.cpp ${READER_SHARED}/reader/qt/reader_overlay.h qt/reader_look.cpp qt/reader_look.h ${READER_SHARED}/reader/qt/reader_crt_frame.cpp ${READER_SHARED}/reader/qt/reader_crt_frame.h
		${READER_SHARED}/reader/qt/reader_pad.cpp ${READER_SHARED}/reader/qt/reader_pad.h
		${READER_SHARED}/reader/qt/reader_steam.cpp ${READER_SHARED}/reader/qt/reader_steam.h
		${READER_SHARED}/reader/qt/reader_session.cpp ${READER_SHARED}/reader/qt/reader_session.h
	)
	# A published build (READER_BUNDLE_PROFILES=OFF) comes with no game scripts: they are
	# installed from the published list (Reader, Game scripts).
	option(READER_BUNDLE_PROFILES "Build the game scripts in reader/profiles into the app" ON)
	if(READER_BUNDLE_PROFILES)
		file(GLOB READER_PROFILE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/profiles/*")
		if(READER_PROFILE_FILES)
			qt_add_resources(reader_qt "reader_profiles"
				PREFIX "/reader/profiles"
				BASE "${CMAKE_CURRENT_SOURCE_DIR}/profiles"
				FILES ${READER_PROFILE_FILES})
		endif()
	endif()
	qt_add_resources(reader_qt "reader_icon"
		PREFIX "/reader/icon"
		BASE "${CMAKE_CURRENT_SOURCE_DIR}/icon"
		FILES "${CMAKE_CURRENT_SOURCE_DIR}/icon/app.png")
	target_include_directories(reader_qt PRIVATE ${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/src/duckstation-nogui ${SDL2_INCLUDE_DIRS})
	target_include_directories(reader_qt PUBLIC ${CMAKE_SOURCE_DIR})
	# Privately: what Qt asks of the code that uses it (Unicode Windows calls, for one) must
	# not reach the emulator's own files.
	target_link_libraries(reader_qt PRIVATE reader_desktop reader_core Qt6::Widgets Qt6::Gui
		Qt6::Core Qt6::Network core common frontend-common imgui glad ${SDL2_LIBRARIES})
	# The emulator's input library is SDL 2: the Steam Controller's gamepad is made through it
	# (qt/reader_steam.cpp).
	target_compile_definitions(reader_qt PRIVATE READER_SDL_MAJOR=2)
	# The Save sync window (qt/save_sync_dialog, the same in every reader fork), in a build
	# with save sync only.
	if(READER_SAVE_SYNC)
		target_sources(reader_qt PRIVATE ${READER_SHARED}/reader/qt/save_sync_dialog.cpp ${READER_SHARED}/reader/qt/save_sync_dialog.h)
		target_link_libraries(reader_qt PRIVATE reader_save_sync)
	else()
		target_compile_definitions(reader_qt PRIVATE READER_NO_SAVE_SYNC)
	endif()
	if(WIN32)
		# The Steam Controller's back buttons are read from the device itself.
		target_link_libraries(reader_qt PRIVATE hid setupapi)
	endif()
	target_compile_options(reader_qt PRIVATE -fexceptions)
	set_target_properties(reader_qt PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
	if(READER_TESTS)
		# After reader/desktop, whose tests fetch Catch2.
		add_subdirectory(save_sync/tests)
	endif()
	target_compile_definitions(${READER_APP} PRIVATE READER_QT)
	target_link_libraries(${READER_APP} PRIVATE reader_qt)
	# The program's way in stays the emulator's own (Qt would add one of its own on Windows).
	set_target_properties(${READER_APP} PROPERTIES QT_NO_ENTRYPOINT ON)
	set_target_properties(${READER_APP} PROPERTIES qt_no_entrypoint ON)
	if(WIN32)
		target_compile_options(${READER_APP} PRIVATE -UQT_NEEDS_QMAIN)
	endif()
endif()

# The Windows build made with MinGW (tools/reader/build-windows.sh). DuckStation names some of
# the system libraries it needs in its sources (#pragma comment(lib), which only MSVC heeds):
# they are named here, after the emulator's own libraries that need them. MinGW's Direct3D 11
# header lacks helpers that Microsoft's has (reader/windows). The program's way in is
# DuckStation's wWinMain (-municode), its icon the reader's (reader/icon/app.ico).
if(MINGW AND TARGET duckstation-nogui)
	set(READER_WINDOWS_LIBS opengl32 ole32 comctl32 winmm avrt imm32 d3dcompiler dxgi d3d11
		dinput8 dxguid winhttp ws2_32 crypt32 uuid shlwapi)
	target_link_libraries(common PUBLIC ${READER_WINDOWS_LIBS})
	target_sources(common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/windows/d3d11_helpers.cpp)
	target_link_libraries(duckstation-nogui PRIVATE ${READER_WINDOWS_LIBS})
	target_link_options(duckstation-nogui PRIVATE -municode)
	file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/icon/app.ico" READER_ICON_FILE)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/station-reader.rc" "1 ICON \"${READER_ICON_FILE}\"\n")
	target_sources(duckstation-nogui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/station-reader.rc")
endif()
