# SPDX-FileCopyrightText: Flycast Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# The reader fork's own code. The emulator's build includes this folder once, at its end.
#
#   reader_lua      Lua 5.4 (reader/lua), built as C++: its errors are then exceptions, which
#                   the bindings rely on to unwind. The emulator has a Lua of its own for its
#                   own scripting; the two do not meet.
#   reader_core     The text engine: profiles, text events. Knows nothing of the emulator.
#   reader_emu      reader/core/emulator.cpp, the one file that ties the engine to the
#                   emulator. A library of its own because the engine's headers want C++20
#                   and the emulator is built as C++17.

option(READER_HARNESS "Build the reader's headless harness (Linux)" OFF)
option(READER_TESTS "Build the reader's C++ tests" OFF)
option(READER_QT "Build the desktop reading layer (its windows are Qt's)" OFF)

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
# Dreamcast's memory units (host). Knows nothing of the emulator either. cpp-httplib,
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
if(TARGET zlibstatic AND NOT WITH_SYSTEM_ZLIB)
	get_target_property(READER_SYNC_ZLIB_SOURCE zlibstatic SOURCE_DIR)
	get_target_property(READER_SYNC_ZLIB_BINARY zlibstatic BINARY_DIR)
	target_include_directories(reader_save_sync PRIVATE
		"${READER_SYNC_ZLIB_SOURCE}" "${READER_SYNC_ZLIB_BINARY}")
	target_link_libraries(reader_save_sync PRIVATE zlibstatic)
else()
	target_link_libraries(reader_save_sync PRIVATE ZLIB::ZLIB)
endif()
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
target_include_directories(reader_emu PRIVATE
	$<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>)
target_compile_definitions(reader_emu PRIVATE
	$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>)
target_link_libraries(reader_emu PUBLIC reader_core ${READER_SAVE_SYNC_LIB})
set_target_properties(reader_emu PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE reader_emu)

# The CRT picture (core/crt.h) in the emulator's renderers: what puts the finished frame on
# the screen draws it with the reader's shader when that is on.
if(USE_OPENGL OR USE_GLES2 OR USE_GLES)
	target_sources(${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_gl.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_gl.h)
endif()

if(USE_VULKAN)
	target_sources(${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_vk.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_vk.h)
endif()
if(WIN32 AND USE_DX11)
	target_sources(${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_dx11.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/render/crt_dx11.h)
endif()

# The Dreamcast-style menu and the console's colours, drawn with the emulator's own interface
# library: part of the emulator's program on every system.
target_sources(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/ui/dc_home.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ui/dc_home.h)

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
		qt_add_resources(reader_qt "reader_profiles"
			PREFIX "/reader/profiles"
			BASE "${CMAKE_CURRENT_SOURCE_DIR}/profiles"
			FILES ${READER_PROFILE_FILES})
	endif()
	qt_add_resources(reader_qt "reader_icon"
		PREFIX "/reader/icon"
		BASE "${CMAKE_CURRENT_SOURCE_DIR}/icon"
		FILES "${CMAKE_CURRENT_SOURCE_DIR}/icon/app.png")
	target_include_directories(reader_qt PRIVATE
		$<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>)
	target_compile_definitions(reader_qt PRIVATE
		$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>)
	target_include_directories(reader_qt PUBLIC ${CMAKE_SOURCE_DIR})
	# Privately: what Qt asks of the code that uses it (Unicode Windows calls, for one) must
	# not reach the emulator's own files.
	target_link_libraries(reader_qt PRIVATE reader_desktop reader_core Qt6::Widgets Qt6::Gui
		Qt6::Core Qt6::Network)
	# The emulator's input library is SDL 2 (its own copy): the Steam Controller's gamepad is
	# made through it (qt/reader_steam.cpp).
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
	target_compile_definitions(${PROJECT_NAME} PRIVATE READER_QT)
	target_link_libraries(${PROJECT_NAME} PRIVATE reader_qt)
	# The program's way in stays the emulator's own (Qt would add one of its own on Windows),
	# and so does its choice of Windows' non-Unicode calls, which the resources above would
	# otherwise change through what Qt asks of its users.
	set_target_properties(${PROJECT_NAME} PROPERTIES QT_NO_ENTRYPOINT ON)
	set_target_properties(${PROJECT_NAME} PROPERTIES qt_no_entrypoint ON)
	if(WIN32)
		target_compile_options(${PROJECT_NAME} PRIVATE -UUNICODE -U_UNICODE -UQT_NEEDS_QMAIN)
	endif()
endif()

# The Android app's reading layer is Kotlin (shell/android-studio/flycast/src/main/java/com/
# flycast/emulator/reader); this is its bridge to the engine. Its functions are called by
# name from Java, so its object file goes into the app's library whole.
if(ANDROID)
	add_library(reader_android OBJECT android/reader_jni.cpp ${READER_SHARED}/reader/android/reader_jni_common.cpp)
	reader_jni_package(com.flycast.emulator)
	target_include_directories(reader_android PRIVATE ${CMAKE_SOURCE_DIR}
		$<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>)
	target_compile_definitions(reader_android PRIVATE
		$<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>)
	target_link_libraries(reader_android PRIVATE reader_core ${READER_SAVE_SYNC_LIB})
	target_compile_options(reader_android PRIVATE -fexceptions)
	set_target_properties(reader_android PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
		POSITION_INDEPENDENT_CODE ON)
	target_sources(${PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:reader_android>)
endif()
