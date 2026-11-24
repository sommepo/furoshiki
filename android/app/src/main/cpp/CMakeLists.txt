# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# libfuroshiki.so, the Android app's native side: the reader engine, built as the desktop program
# builds it (home/CMakeLists.txt, HOME_CORES: home/engine first on the include path, so that
# the console is said at run time by the running core), the core session (home/src), and the
# bridge to the Kotlin layer. The emulators are not in it: each is a core this library dlopens
# (cores/reader_core.h), in a process that is ended with the game.

cmake_minimum_required(VERSION 3.22)
project(furoshiki C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(READER_SHARED "${REPOSITORY}/shared")
include("${READER_SHARED}/reader.cmake")
set(READER "${READER_SHARED}/reader")
set(HOME "${REPOSITORY}/home")
set(FLYCAST_READER "${REPOSITORY}/flycast/reader")

set(LUA_SOURCES)
foreach(name lapi lauxlib lbaselib lcode lcorolib lctype ldebug ldo ldump lfunc lgc llex lmathlib
		lmem lobject lopcodes lparser lstate lstring lstrlib ltable ltablib ltm lundump lutf8lib
		lvm lzio)
	list(APPEND LUA_SOURCES ${FLYCAST_READER}/lua/${name}.c)
endforeach()
# Lua is built as C++, as in the forks: its errors unwind as exceptions.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-w")

add_library(furoshiki SHARED
	furoshiki_jni.cpp
	dictionary_jni.cpp
	${READER}/android/reader_jni_common.cpp
	${READER}/core/audio_tap.cpp
	${READER}/core/crt.cpp
	${READER}/core/engine.cpp
	${READER}/core/input_filter.cpp
	${READER}/core/lua_api.cpp
	${READER}/core/next_choice.cpp
	${READER}/core/text_codec.cpp
	${HOME}/src/core_session.cpp
	${HOME}/src/reader_emu.cpp
	${LUA_SOURCES})

# Save sync: the shared engine and the program's host of every console (home/src/save_sync_host.cpp).
# A client of the maintainer's service: never in a published build (-DREADER_SAVE_SYNC=OFF).
option(READER_SAVE_SYNC "Save sync" ON)
if(READER_SAVE_SYNC)
	foreach(name archive console_host engine naming_3ds naming_dreamcast naming_ps1 naming_ps2
			naming_psp naming_switch naming_vita remote store)
		target_sources(furoshiki PRIVATE ${READER}/save_sync/${name}.cpp)
	endforeach()
	target_sources(furoshiki PRIVATE ${HOME}/src/save_sync_host.cpp ${FLYCAST_READER}/ext/sha256/sha256.cpp)
	target_include_directories(furoshiki SYSTEM PRIVATE ${FLYCAST_READER}/ext/reader-httplib ${FLYCAST_READER}/ext/reader-json)
	target_link_libraries(furoshiki PRIVATE z)
else()
	target_compile_definitions(furoshiki PRIVATE READER_NO_SAVE_SYNC)
endif()
reader_jni_package(io.github.sommepo.furoshiki)
target_include_directories(furoshiki PRIVATE
	${HOME}/engine ${HOME}/src ${REPOSITORY}/cores ${READER_SHARED} ${FLYCAST_READER}/.. ${FLYCAST_READER}/lua)
target_compile_options(furoshiki PRIVATE -fvisibility=hidden -fexceptions -frtti)
target_link_libraries(furoshiki PRIVATE aaudio android log dl)

# The dictionary engine (shared/dict, docs/dictionary-engine.md), for the Kotlin layer's
# dict/DictionaryNative.kt (dictionary_jni.cpp).
include("${READER_SHARED}/dict/dict.cmake")
target_link_libraries(furoshiki PRIVATE reader_dict)

# SQLite, for the move of an earlier version's reader_dict.db into the engine read straight from
# the file (shared/dict/sqlite_source.cpp): the same amalgamation the desktop builds.
include(FetchContent)
FetchContent_Declare(furoshiki_sqlite_src
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA512=71e9d81ee7e1160b9da6da0d390e3bd7dc8b434b157db6ba0e0a95446ae50c95488af23b93ecb02ea8b5c305cb16d9e20b174829e41dc17ec747efcb6a7e64d1)
FetchContent_MakeAvailable(furoshiki_sqlite_src)
add_library(furoshiki_sqlite STATIC ${furoshiki_sqlite_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(furoshiki_sqlite SYSTEM PUBLIC ${furoshiki_sqlite_src_SOURCE_DIR})
target_compile_definitions(furoshiki_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0)
target_compile_options(furoshiki_sqlite PRIVATE -w -fvisibility=hidden $<$<CONFIG:Debug>:-O2>)
target_sources(furoshiki PRIVATE ${READER_SHARED}/dict/sqlite_source.cpp)
set_source_files_properties(${READER_SHARED}/dict/sqlite_source.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
target_link_libraries(furoshiki PRIVATE furoshiki_sqlite z)
target_link_options(furoshiki PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384")
