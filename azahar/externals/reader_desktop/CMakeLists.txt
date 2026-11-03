# Copyright Azahar Reader fork contributors
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

# Reader fork: what the desktop app's reader layer (src/reader_desktop) needs besides Qt, each
# fetched at configure time at a fixed version and checked against its hash: SQLite (the
# dictionaries, the backlog, the statistics), zlib and QuaZip (dictionary files are zip
# archives), Opus (a card's audio). The Android app has the system's own of each.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(reader_sqlite_source
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA512=71e9d81ee7e1160b9da6da0d390e3bd7dc8b434b157db6ba0e0a95446ae50c95488af23b93ecb02ea8b5c305cb16d9e20b174829e41dc17ec747efcb6a7e64d1)
FetchContent_MakeAvailable(reader_sqlite_source)
add_library(reader_sqlite STATIC EXCLUDE_FROM_ALL ${reader_sqlite_source_SOURCE_DIR}/sqlite3.c)
target_include_directories(reader_sqlite SYSTEM PUBLIC ${reader_sqlite_source_SOURCE_DIR})
target_compile_definitions(reader_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0)
if (NOT MSVC)
    target_compile_options(reader_sqlite PRIVATE -w)
endif()

set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ZLIB
    URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz
    URL_HASH SHA512=16fea4df307a68cf0035858abe2fd550250618a97590e202037acd18a666f57afc10f8836cbbd472d54a0e76539d0e558cb26f059d53de52ff90634bbf4f47d4
    EXCLUDE_FROM_ALL
    OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(ZLIB)
if (NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

set(QUAZIP_QT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
set(QUAZIP_INSTALL OFF CACHE BOOL "" FORCE)
set(QUAZIP_ENABLE_QTEXTCODEC OFF CACHE BOOL "" FORCE)
set(QUAZIP_BZIP2 OFF CACHE BOOL "" FORCE)
set(QUAZIP_FETCH_LIBS OFF CACHE BOOL "" FORCE)
set(QUAZIP_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(_reader_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(reader_quazip
    URL https://github.com/stachenov/quazip/archive/2e95c9001b.tar.gz
    URL_HASH SHA512=609c240c7f029ac26a37d8fbab51bc16284e05e128b78b9b9c0e95d083538c36047a67d682759ac990e4adb0eeb90f04f1ea7fe2253bbda7e7e3bcce32e53dd8
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(reader_quazip)

set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(reader_opus
    URL https://github.com/xiph/opus/archive/a3f0ec02b3.tar.gz
    URL_HASH SHA512=9506147b0de35befda8633ff272981cc2575c860874791bd455b752f797fd7dbd1079f0ba42ccdd7bb1fe6773fa5e84b3d75667c2883dd1fb2d0e4a5fa4f8387
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(reader_opus)
set(BUILD_SHARED_LIBS ${_reader_shared})
if (NOT TARGET Opus::opus)
    add_library(Opus::opus ALIAS opus)
endif()
foreach(_target zlibstatic opus QuaZip)
    if (TARGET ${_target} AND NOT MSVC)
        target_compile_options(${_target} PRIVATE -w)
    endif()
endforeach()

# The tests of src/reader_desktop want Catch2, which the emulator's own tests bring otherwise.
if (ENABLE_READER_DESKTOP_TESTS AND NOT TARGET Catch2::Catch2WithMain)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../catch2 ${CMAKE_CURRENT_BINARY_DIR}/catch2 EXCLUDE_FROM_ALL)
endif()
