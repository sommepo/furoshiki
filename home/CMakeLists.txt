# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)
project(reader-home VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
option(HOME_TESTS "Build the tests" OFF)
# The first step of the one program (docs/one-program.md): the cores' interface
# (../cores/reader_core.h), their loader, and the shared reader engine built here with the
# program's console.h and registers.h. Not in the program yet: its tests, and a test core.
option(HOME_CORES "Build the cores' host and its tests" OFF)
# A published build (sommepo/furoshiki, ../tools/publish/): no game scripts and no character
# tables built in (the user installs scripts from the consoles' published lists), none of the
# maintainer's recorded sounds (sounds/ is private: the made ones play), and no save sync, no
# pairing and no statistics sent anywhere: the program of the private line alone has them
# (READER_NO_SAVE_SYNC, as in the forks; their sources are not in a public tree). Eden's core
# is left out of its package (tools/package-windows.sh).
option(HOME_PUBLIC "A published build: nothing private built in" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

add_library(home_core STATIC src/library.cpp src/library.h src/play_settings.cpp src/play_settings.h
	src/popup_style.cpp src/popup_style.h
	src/popup_controls.cpp src/popup_controls.h
	src/old_settings.cpp src/old_settings.h
	src/app_folders.cpp src/app_folders.h
	src/sounds.cpp src/sounds.h
	src/look.cpp src/look.h
	src/steam_bridge.cpp src/steam_bridge.h)
target_include_directories(home_core PUBLIC src)
target_link_libraries(home_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)
if(NOT HOME_PUBLIC)
	target_sources(home_core PRIVATE src/pairing.cpp src/pairing.h)
endif()

# The way in is main() itself: Qt's own entry point for Windows, which a reader app's first
# Windows package hung in, is left out. -mwindows makes it a program without a console.
add_executable(reader-home src/main.cpp src/home_window.cpp src/home_window.h
	src/look_paint.cpp src/look_paint.h
	src/steam_pad.cpp src/steam_pad.h)
target_link_libraries(reader-home PRIVATE home_core Qt6::Widgets)
# The window's themes (themes/*.json, AGENTS.md "The look and its themes") and the fonts they
# name (SIL Open Font License; fonts/OFL-*.txt): every file there, so that a theme is added
# by adding its file and its fonts.
file(GLOB HOME_FONTS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" fonts/*.ttf fonts/*.otf)
file(GLOB HOME_THEMES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" themes/*.json)
qt_add_resources(reader-home "fonts" PREFIX "/fonts" BASE "${CMAKE_CURRENT_SOURCE_DIR}/fonts" FILES ${HOME_FONTS})
# The menu's recorded sounds (src/sounds.cpp says which); kept out of a public tree and of a
# published build (or a public tree, which has no sounds/), where the sounds made from sine
# waves play instead.
if(NOT HOME_PUBLIC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sounds/move.wav")
	qt_add_resources(reader-home "sounds" PREFIX "/sounds" BASE "${CMAKE_CURRENT_SOURCE_DIR}/sounds"
		FILES sounds/move.wav sounds/select.wav sounds/back.wav)
endif()
qt_add_resources(reader-home "themes" PREFIX "/themes" BASE "${CMAKE_CURRENT_SOURCE_DIR}/themes" FILES ${HOME_THEMES})
if(HOME_PUBLIC)
	target_compile_definitions(home_core PUBLIC HOME_PUBLIC READER_NO_SAVE_SYNC)
endif()
if(WIN32)
	target_link_options(reader-home PRIVATE -mwindows)
	# The Steam Controller is read from the device itself.
	target_link_libraries(reader-home PRIVATE hid setupapi)
endif()

if(HOME_TESTS)
	add_executable(reader-home-tests tests/tests.cpp)
	target_link_libraries(reader-home-tests PRIVATE home_core)
	# The themes and fonts are checked where they are kept.
	target_compile_definitions(reader-home-tests PRIVATE HOME_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(HOME_CORES)
	# The shared reader engine, from the repository's shared/ (shared/reader.cmake), which every
	# fork builds too. engine/ comes first on the include path: its reader/core/console.h and
	# registers.h are the program's, said at run time by the core. Lua, reader/core/types.h and
	# the desktop logic's CMake file are the Flycast Reader fork's (../flycast/reader), as the
	# program's reader was first built from that fork.
	get_filename_component(READER_SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../shared" ABSOLUTE)
	include("${READER_SHARED}/reader.cmake")
	set(HOME_READER "${READER_SHARED}/reader")
	set(HOME_FLYCAST_READER "${CMAKE_CURRENT_SOURCE_DIR}/../flycast/reader")
	set(HOME_CORES_API "${CMAKE_CURRENT_SOURCE_DIR}/../cores")
	set(HOME_LUA_SOURCES)
	foreach(name lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo ldump lfunc lgc llex
			lmathlib lmem lobject lopcodes lparser lstate lstring lstrlib ltable ltablib ltm
			lundump lutf8lib lvm lzio)
		list(APPEND HOME_LUA_SOURCES ${HOME_FLYCAST_READER}/lua/${name}.c)
	endforeach()
	# Lua is built as C++, as in the forks: its errors unwind as exceptions.
	set_source_files_properties(${HOME_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
	add_library(home_lua STATIC ${HOME_LUA_SOURCES})
	target_include_directories(home_lua SYSTEM PUBLIC ${HOME_FLYCAST_READER}/lua)
	target_compile_options(home_lua PRIVATE -w)
	add_library(home_engine STATIC
		${HOME_READER}/core/audio_tap.cpp
		${HOME_READER}/core/crt.cpp
		${HOME_READER}/core/engine.cpp
		${HOME_READER}/core/input_filter.cpp
		${HOME_READER}/core/lua_api.cpp
		${HOME_READER}/core/next_choice.cpp
		${HOME_READER}/core/text_codec.cpp
		engine/reader/core/console.h
		engine/reader/core/registers.h)
	# The CRT picture is on in an install that was never told otherwise, as in Station Reader
	# (its only core so far is the PlayStation's, which was played on a tube).
	target_compile_definitions(home_engine PRIVATE READER_CRT_DEFAULT_ON)
	target_include_directories(home_engine PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/engine ${HOME_CORES_API} ${HOME_READER}/.. ${HOME_FLYCAST_READER}/..)
	target_link_libraries(home_engine PUBLIC home_lua)
	set_target_properties(home_engine home_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

	add_library(home_cores STATIC src/core_library.cpp src/core_library.h src/core_session.cpp src/core_session.h)
	target_include_directories(home_cores PUBLIC src)
	target_link_libraries(home_cores PUBLIC home_engine Qt6::Core)

	# A core of a made-up console, as a real one is built: a module of its own, everything in
	# it hidden but reader_core_get.
	add_library(reader_core_test MODULE ../cores/test/test_core.cpp)
	target_include_directories(reader_core_test PRIVATE ${HOME_CORES_API})
	set_target_properties(reader_core_test PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON)
	find_package(Threads REQUIRED)
	target_link_libraries(reader_core_test PRIVATE Threads::Threads)

	# The reader's desktop logic and windows, the shared ones (shared/reader/desktop and qt),
	# with the program's own host (src/reader_host.cpp, src/reader_emu.cpp) and its own
	# engine/reader/qt/host.h, install.h and reader_look.* first on the include path: the
	# reader over the game a core plays in the program's window (src/main_window.cpp).
	# On Windows there is no zlib for the dictionaries' zip files (QuaZip) to be found: one is
	# built here, which reader/desktop takes as an emulator's own (zlibstatic).
	if(WIN32)
		include(FetchContent)
		FetchContent_Declare(home_zlib
			URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
			URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
			EXCLUDE_FROM_ALL)
		FetchContent_MakeAvailable(home_zlib)
	endif()
	add_subdirectory(${HOME_FLYCAST_READER}/desktop ${CMAKE_CURRENT_BINARY_DIR}/reader_desktop)
	target_include_directories(reader_desktop BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine
		${HOME_READER}/.. ${HOME_FLYCAST_READER}/..)
	find_package(Qt6 REQUIRED COMPONENTS Network)
	# SDL 2: the controllers (main_window.cpp), and the Steam Controller's gamepad, which the
	# reader's bridge makes through SDL's virtual joystick (reader_steam.cpp).
	find_package(SDL2 REQUIRED)
	set(HOME_READER_QT)
	foreach(name audio_prompt lookup_flow lookup_popup popup_web reader_control reader_controller
			reader_crt_frame reader_crt_page reader_cursor reader_dialogs reader_games reader_link
			reader_media reader_overlay reader_pad reader_session reader_steam)
		list(APPEND HOME_READER_QT ${HOME_READER}/qt/${name}.cpp ${HOME_READER}/qt/${name}.h)
	endforeach()
	# Save sync (shared/reader/save_sync): a console host for each console whose core tells its
	# save moments, behind the program's SaveSync::Host (engine/reader/save_sync/host.h,
	# src/save_sync_host.cpp); httplib, nlohmann's JSON and the SHA-256 are the Flycast fork's
	# (flycast/reader/ext). A client of the maintainer's service: never in a published build.
	if(NOT HOME_PUBLIC)
	set(HOME_SAVE_SYNC)
	foreach(name archive console_host engine naming_3ds naming_dreamcast naming_ps1 naming_ps2
			naming_psp naming_switch naming_vita remote store)
		list(APPEND HOME_SAVE_SYNC ${HOME_READER}/save_sync/${name}.cpp ${HOME_READER}/save_sync/${name}.h)
	endforeach()
	add_library(home_save_sync STATIC ${HOME_SAVE_SYNC} ${HOME_FLYCAST_READER}/ext/sha256/sha256.cpp
		engine/reader/save_sync/host.h src/save_sync_host.cpp)
	target_include_directories(home_save_sync BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine)
	target_include_directories(home_save_sync PUBLIC ${HOME_READER}/.. ${HOME_FLYCAST_READER}/..)
	target_include_directories(home_save_sync SYSTEM PRIVATE ${HOME_FLYCAST_READER}/ext/reader-httplib
		${HOME_FLYCAST_READER}/ext/reader-json)
	if(TARGET zlibstatic)
		get_target_property(HOME_ZLIB_SOURCE zlibstatic SOURCE_DIR)
		get_target_property(HOME_ZLIB_BINARY zlibstatic BINARY_DIR)
		target_include_directories(home_save_sync PRIVATE "${HOME_ZLIB_SOURCE}" "${HOME_ZLIB_BINARY}")
		target_link_libraries(home_save_sync PRIVATE zlibstatic)
	else()
		find_package(ZLIB REQUIRED)
		target_link_libraries(home_save_sync PRIVATE ZLIB::ZLIB)
	endif()
	if(WIN32)
		target_link_libraries(home_save_sync PRIVATE ws2_32)
	endif()
	target_compile_options(home_save_sync PRIVATE -fexceptions)
	endif()

	add_library(home_reader STATIC ${HOME_READER_QT}
		engine/reader/core/emulator.h engine/reader/qt/host.h engine/reader/qt/install.h
		engine/reader/qt/reader_look.cpp engine/reader/qt/reader_look.h
		src/reader_emu.cpp src/reader_host.cpp)
	target_include_directories(home_reader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine ${HOME_READER}/..
		${HOME_FLYCAST_READER}/..)
	target_link_libraries(home_reader PUBLIC home_cores home_core reader_desktop Qt6::Widgets
		Qt6::Network SDL2::SDL2)
	if(NOT HOME_PUBLIC)
		target_sources(home_reader PRIVATE ${HOME_READER}/qt/save_sync_dialog.cpp ${HOME_READER}/qt/save_sync_dialog.h)
		target_link_libraries(home_reader PUBLIC home_save_sync)
	endif()
	target_compile_definitions(home_reader PRIVATE READER_SDL_MAJOR=2)
	if(WIN32)
		target_link_libraries(home_reader PRIVATE hid setupapi)
	endif()
	# The dictionary popup's page (../shared/popup, tools/popup.py) in a web engine: on Windows
	# the system's WebView2 (its SDK's header, and WebView2Loader.dll beside the program, which
	# finds the installed engine); on Linux Qt WebEngine, where the Qt this is built with has it
	# (the official Qt's; the toolchain's usual Qt has none). Without one the popup draws itself.
	if(WIN32)
		include(FetchContent)
		FetchContent_Declare(home_webview2
			URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
			URL_HASH SHA256=ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74
			DOWNLOAD_NAME webview2.zip
			EXCLUDE_FROM_ALL)
		FetchContent_MakeAvailable(home_webview2)
		target_sources(home_reader PRIVATE ${HOME_READER}/qt/popup_web_view2.cpp)
		# The SDK's header names EventToken.h as Windows' SDK spells it; MinGW's is eventtoken.h,
		# and a cross-build's file names are case-sensitive.
		file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/webview2-shim/EventToken.h "#include <eventtoken.h>\n")
		target_include_directories(home_reader SYSTEM PRIVATE ${home_webview2_SOURCE_DIR}/build/native/include
			${CMAKE_CURRENT_BINARY_DIR}/webview2-shim)
		target_compile_definitions(home_reader PRIVATE READER_WEB_VIEW2)
		target_link_libraries(home_reader PRIVATE ole32)
		add_custom_command(TARGET reader-home POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${home_webview2_SOURCE_DIR}/build/native/x64/WebView2Loader.dll $<TARGET_FILE_DIR:reader-home>)
	else()
		find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
		if(TARGET Qt6::WebEngineWidgets)
			target_sources(home_reader PRIVATE ${HOME_READER}/qt/popup_web_engine.cpp)
			target_compile_definitions(home_reader PRIVATE READER_WEB_ENGINE)
			target_link_libraries(home_reader PRIVATE Qt6::WebEngineWidgets)
			message(STATUS "Furoshiki: the popup page in Qt WebEngine")
		else()
			message(STATUS "Furoshiki: no Qt WebEngine in this Qt; the popup draws itself")
		endif()
	endif()
	# The game scripts of the consoles the program has a core for, under the names the reader
	# looks for them by (ReaderBridge, ":/reader/profiles/<ID>.lua"); a glyph table made from
	# a game's font (<ID>.charset) where the fork's private-data.sh put one beside them. None
	# in a published build: there the user installs them from the consoles' published lists.
	set(HOME_BUNDLED_PROFILES)
	if(NOT HOME_PUBLIC)
		set(HOME_BUNDLED_PROFILES station/reader/profiles flycast/reader/profiles ppsspp/assets/reader/profiles
			azahar/src/android/app/src/main/assets/reader/profiles pcsx2/reader/profiles
			eden/src/android/app/src/main/assets/reader/profiles vita3k/reader/profiles)
	endif()
	foreach(fork_profiles IN LISTS HOME_BUNDLED_PROFILES)
		string(REGEX REPLACE "/.*" "" fork "${fork_profiles}")
		set(HOME_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${fork_profiles}")
		file(GLOB HOME_PROFILE_FILES CONFIGURE_DEPENDS "${HOME_PROFILE_DIR}/*")
		list(FILTER HOME_PROFILE_FILES EXCLUDE REGEX "/(README[^/]*|index\\.json)$")
		if(HOME_PROFILE_FILES)
			qt_add_resources(home_reader "profiles_${fork}" PREFIX "/reader/profiles" BASE "${HOME_PROFILE_DIR}"
				FILES ${HOME_PROFILE_FILES})
		endif()
	endforeach()

	target_sources(reader-home PRIVATE src/main_window.cpp src/main_window.h
		src/settings_window.cpp src/settings_window.h src/settings_pages.cpp)
	target_link_libraries(reader-home PRIVATE home_reader)
	target_compile_definitions(reader-home PRIVATE HOME_CORES)
	# Send feedback, one of the maintainer's own links: never in a published build.
	if(NOT HOME_PUBLIC)
		target_sources(reader-home PRIVATE src/feedback_dialog.cpp src/feedback_dialog.h)
		set_source_files_properties(src/feedback_dialog.cpp PROPERTIES COMPILE_DEFINITIONS HOME_BUILD="${PROJECT_VERSION}")
	endif()

	add_executable(reader-home-cores-tests tests/cores_test.cpp)
	target_link_libraries(reader-home-cores-tests PRIVATE home_cores)
	target_compile_definitions(reader-home-cores-tests PRIVATE HOME_TEST_CORE="$<TARGET_FILE:reader_core_test>")
	add_dependencies(reader-home-cores-tests reader_core_test)
endif()
