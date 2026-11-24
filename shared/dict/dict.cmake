# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Furoshiki's dictionary engine (shared/dict, docs/dictionary-engine.md) and zstd, which it
# compresses definitions with. A fork's list includes this file after its zlib is known (QuaZip
# finds one) and links `reader_dict`. Plain C and C++: no Qt.

include(FetchContent)
FetchContent_Declare(reader_zstd_src
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Its own CMake lists are in build/cmake: this only fetches the sources, built below.
FetchContent_MakeAvailable(reader_zstd_src)
set(READER_ZSTD_LIB "${reader_zstd_src_SOURCE_DIR}/lib")
file(GLOB READER_ZSTD_SOURCES
    "${READER_ZSTD_LIB}/common/*.c"
    "${READER_ZSTD_LIB}/compress/*.c"
    "${READER_ZSTD_LIB}/decompress/*.c"
    "${READER_ZSTD_LIB}/dictBuilder/*.c")
add_library(reader_zstd STATIC EXCLUDE_FROM_ALL ${READER_ZSTD_SOURCES})
target_include_directories(reader_zstd SYSTEM PUBLIC "${READER_ZSTD_LIB}")
# The assembly decoder is left out (it is only for x86-64 outside Windows); threads are the
# engine's own business.
target_compile_definitions(reader_zstd PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD_SUPPORT_DEFAULT=0
    ZDICT_QSORT=ZDICT_QSORT_MIN)
if(NOT MSVC)
    target_compile_options(reader_zstd PRIVATE -w)
endif()

# Optimised in every build: an Android debug build compiles without it, and the engine took
# Jitendex in 4 s there on the Y700 against 1.2 s optimised (2026-11-23).
if(NOT MSVC)
    target_compile_options(reader_zstd PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()

add_library(reader_dict STATIC EXCLUDE_FROM_ALL
    ${READER_SHARED}/dict/format.h
    ${READER_SHARED}/dict/importer.cpp
    ${READER_SHARED}/dict/importer.h
    ${READER_SHARED}/dict/json.cpp
    ${READER_SHARED}/dict/json.h
    ${READER_SHARED}/dict/library.cpp
    ${READER_SHARED}/dict/library.h
    ${READER_SHARED}/dict/mapped_file.cpp
    ${READER_SHARED}/dict/mapped_file.h
    ${READER_SHARED}/dict/zip_reader.cpp
    ${READER_SHARED}/dict/zip_reader.h)
target_include_directories(reader_dict PUBLIC "${READER_SHARED}")
set_target_properties(reader_dict PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(NOT MSVC)
    target_compile_options(reader_dict PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
find_package(Threads REQUIRED)
target_link_libraries(reader_dict PUBLIC reader_zstd Threads::Threads)
if(TARGET ZLIB::ZLIB)
    target_link_libraries(reader_dict PRIVATE ZLIB::ZLIB)
elseif(TARGET zlibstatic)
    target_link_libraries(reader_dict PRIVATE zlibstatic)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(reader_dict PRIVATE ZLIB::ZLIB)
endif()
