# The reader in the Android app: the shared engine (shared/reader/core), the Lua it runs scripts
# in, this fork's emulator.cpp, and the bridge to Kotlin. A core builds none of this (the program
# has the engine); only tools/reader/build-android.sh asks for it (READER_ANDROID).
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
	core/emulator.cpp
	${READER_SHARED}/reader/core/audio_tap.cpp
	${READER_SHARED}/reader/core/crt.cpp
	${READER_SHARED}/reader/core/engine.cpp
	${READER_SHARED}/reader/core/input_filter.cpp
	${READER_SHARED}/reader/core/lua_api.cpp
	${READER_SHARED}/reader/core/next_choice.cpp
	${READER_SHARED}/reader/core/text_codec.cpp
)
target_include_directories(reader_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(reader_core PUBLIC reader_lua PRIVATE reader_save_sync app audio config ctrl display emuenv io kernel mem renderer util)
target_compile_options(reader_core PRIVATE -fexceptions)
set_target_properties(reader_core PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)

# Save sync: the shared engine with the PS Vita's naming. httplib, nlohmann's JSON and the SHA-256
# are the Flycast fork's. Plain HTTP: the service is on the maintainer's network.
set(READER_EXT ${READER_SHARED}/../flycast/reader/ext)
add_library(reader_save_sync STATIC
	${READER_SHARED}/reader/save_sync/archive.cpp
	${READER_SHARED}/reader/save_sync/console_host.cpp
	${READER_SHARED}/reader/save_sync/engine.cpp
	${READER_SHARED}/reader/save_sync/naming_vita.cpp
	${READER_SHARED}/reader/save_sync/naming_switch.cpp
	${READER_SHARED}/reader/save_sync/remote.cpp
	${READER_SHARED}/reader/save_sync/store.cpp
	${READER_EXT}/sha256/sha256.cpp
	save_sync/host.cpp
)
target_include_directories(reader_save_sync PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(reader_save_sync PRIVATE ${READER_SHARED}/../flycast)
target_include_directories(reader_save_sync SYSTEM PRIVATE ${READER_EXT}/reader-httplib ${READER_EXT}/reader-json)
target_link_libraries(reader_save_sync PRIVATE z)
target_compile_options(reader_save_sync PRIVATE -fexceptions)
set_target_properties(reader_save_sync PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)

add_library(reader_android OBJECT android/reader_jni.cpp ${READER_SHARED}/reader/android/reader_jni_common.cpp)
reader_jni_package(org.vita3k.emulator)
target_include_directories(reader_android PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(reader_android PRIVATE reader_core reader_save_sync util)
target_compile_options(reader_android PRIVATE -fexceptions)
set_target_properties(reader_android PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON
	POSITION_INDEPENDENT_CODE ON)
