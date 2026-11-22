# SPDX-FileCopyrightText: melonDS Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# melonDS Reader: the Nintendo DS as a core of Furoshiki (cores/reader_core.h at the top of the
# repository), and the headless harness profiles are written with. Its own project, apart from
# melonDS's (whose top CMakeLists.txt is its Qt app's): melonDS's libretro build, the sources
# its Makefile.common lists, without OpenGL (melonDS's software renderer: the libretro host draws
# the picture) and without a JIT (the reader's hooks are in the interpreter).
#
#   melonds_libretro       melonDS and its libretro frontend (src/libretro), a static library
#   reader_core_melonds    the core: that, the shared libretro host (cores/libretro) and
#                          core_library/melonds_core.cpp (READER_CORE, the default)
#   melonds-reader-harness the harness (READER_HARNESS, Linux): that, the shared engine and Lua
#
# Built by tools/reader/build-core.sh, build-core-android.sh, build-core-windows.sh and
# build-harness.sh.

cmake_minimum_required(VERSION 3.20)
project(melonds_reader C CXX)

option(READER_CORE "Build the core of Furoshiki" ON)
option(READER_HARNESS "Build the headless harness (Linux)" OFF)

set(READER_FORK "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(READER_SHARED "${READER_FORK}/../shared")
set(READER_CORES_DIR "${READER_FORK}/../cores")
include("${READER_SHARED}/reader.cmake")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

set(MELON_DIR "${READER_FORK}/src")
set(CORE_DIR "${MELON_DIR}/libretro")
set(LRC "${CORE_DIR}/libretro-common")

set(MELON_C
	${MELON_DIR}/xxhash/xxhash.c
	${MELON_DIR}/tiny-AES-c/aes.c
	${MELON_DIR}/fatfs/diskio.c
	${MELON_DIR}/fatfs/ff.c
	${MELON_DIR}/fatfs/ffsystem.c
	${MELON_DIR}/fatfs/ffunicode.c
	${MELON_DIR}/sha1/sha1.c
	${LRC}/streams/memory_stream.c
	${LRC}/compat/compat_strl.c
	${LRC}/compat/fopen_utf8.c
	${LRC}/compat/compat_posix_string.c
	${LRC}/compat/compat_strcasestr.c
	${LRC}/encodings/encoding_utf.c
	${LRC}/file/file_path.c
	${LRC}/streams/file_stream.c
	${LRC}/streams/file_stream_transforms.c
	${LRC}/string/stdstring.c
	${LRC}/vfs/vfs_implementation.c
	${LRC}/rthreads/rthreads.c
	${LRC}/rthreads/rsemaphore.c
	${LRC}/time/rtime.c
)
set(MELON_CXX)
foreach(name NDS AREngine ARCodeFile ARM ARMInterpreter ARMInterpreter_ALU ARMInterpreter_Branch
		ARMInterpreter_LoadStore CP15 CRC32 DMA DSi DSi_AES DSi_Camera DSi_DSP DSi_I2C DSi_NAND
		DSi_NDMA DSi_NWifi DSi_SD DSi_SPI_TSC DSiCrypto GBACart GPU GPU2D GPU2D_Soft GPU3D
		GPU3D_Soft NDSCart NDSCart_SRAMManager RTC Savestate SPI SPU Wifi WifiAP frontend/Util_ROM)
	list(APPEND MELON_CXX ${MELON_DIR}/${name}.cpp)
endforeach()
foreach(name config input libretro platform screenlayout utils)
	list(APPEND MELON_CXX ${CORE_DIR}/${name}.cpp)
endforeach()
foreach(name ahbm apbp btdmp disassembler_c disassembler dma memory_interface mmio parser processor
		teakra_c teakra timer)
	list(APPEND MELON_CXX ${MELON_DIR}/teakra/src/${name}.cpp)
endforeach()
list(APPEND MELON_CXX ${READER_FORK}/reader/core/emulator.cpp)
# Only those of libretro-common's files that exist in this copy (rtime.c is newer than some).
set(_c)
foreach(file ${MELON_C})
	if(EXISTS "${file}")
		list(APPEND _c "${file}")
	endif()
endforeach()
set(MELON_C ${_c})

add_library(melonds_libretro STATIC ${MELON_C} ${MELON_CXX})
target_include_directories(melonds_libretro PUBLIC ${LRC}/include ${CORE_DIR} ${MELON_DIR}
	${MELON_DIR}/teakra/include ${MELON_DIR}/teakra/src ${READER_FORK})
target_compile_definitions(melonds_libretro PUBLIC MELONDS_VERSION="0.9.3" HAVE_THREADS
	__LIBRETRO__ READER_CORE)
target_compile_features(melonds_libretro PUBLIC cxx_std_20)
if(WIN32)
	target_link_libraries(melonds_libretro PUBLIC ws2_32 iphlpapi)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(melonds_libretro PUBLIC Threads::Threads)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(melonds_libretro PRIVATE -w -fno-strict-aliasing)
endif()

if(READER_CORE)
	add_library(reader_core_melonds MODULE
		core_library/melonds_core.cpp
		${READER_CORES_DIR}/libretro/libretro_core.cpp
		${READER_SHARED}/reader/core/crt.cpp)
	target_include_directories(reader_core_melonds PRIVATE ${READER_FORK} ${READER_CORES_DIR}
		${READER_CORES_DIR}/libretro)
	target_link_libraries(reader_core_melonds PRIVATE melonds_libretro)
	set_target_properties(reader_core_melonds PROPERTIES OUTPUT_NAME "reader_core_melonds" PREFIX ""
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
	if(ANDROID)
		# The Android app's: a "lib" name, as an APK carries only such, and the picture drawn with
		# OpenGL ES on the app's Surface.
		set_target_properties(reader_core_melonds PROPERTIES PREFIX "lib")
		target_link_libraries(reader_core_melonds PRIVATE EGL GLESv3 android log)
		file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
		target_link_options(reader_core_melonds PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
			-Wl,--no-undefined -Wl,-Bsymbolic "-Wl,-z,max-page-size=16384")
	elseif(NOT WIN32)
		# The picture is drawn into the program's X11 window with EGL.
		find_package(X11 REQUIRED)
		find_library(READER_EGL_LIBRARY EGL REQUIRED)
		target_include_directories(reader_core_melonds PRIVATE ${X11_INCLUDE_DIR})
		target_link_libraries(reader_core_melonds PRIVATE ${READER_EGL_LIBRARY} ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
		file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
		target_link_options(reader_core_melonds PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
			-Wl,--no-undefined -Wl,-Bsymbolic)
	else()
		set_target_properties(reader_core_melonds PROPERTIES SUFFIX ".dll")
		target_link_libraries(reader_core_melonds PRIVATE opengl32 gdi32)
	endif()
endif()

if(READER_HARNESS)
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
	set_target_properties(reader_lua PROPERTIES CXX_STANDARD 20)

	add_library(reader_engine STATIC
		${READER_SHARED}/reader/core/audio_tap.cpp
		${READER_SHARED}/reader/core/engine.cpp
		${READER_SHARED}/reader/core/input_filter.cpp
		${READER_SHARED}/reader/core/lua_api.cpp
		${READER_SHARED}/reader/core/next_choice.cpp
		${READER_SHARED}/reader/core/text_codec.cpp)
	target_include_directories(reader_engine PUBLIC ${READER_FORK})
	target_link_libraries(reader_engine PUBLIC reader_lua)
	target_compile_options(reader_engine PRIVATE -fexceptions)
	set_target_properties(reader_engine PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

	add_executable(melonds-reader-harness harness/harness.cpp)
	target_link_libraries(melonds-reader-harness PRIVATE melonds_libretro reader_engine)
	set_target_properties(melonds-reader-harness PROPERTIES CXX_STANDARD 20
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
