# SPDX-FileCopyrightText: PCSX2 Reader fork contributors (from the Eden Reader fork)
# SPDX-License-Identifier: GPL-3.0+

# Reader fork: what the desktop app's reader layer is made of apart from its windows:
# dictionaries, lookups, cards, statistics, the backlog. It is the Android app's reader layer
# in C++, kept free of the emulator so that it can be tested alone
# (tools/reader/test-desktop.sh). The windows are in reader/qt.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

# What the desktop reading logic is made with besides Qt, fetched at configure time: SQLite
# (dictionaries, backlog, statistics), QuaZip (dictionary files are zip archives) and Opus
# (the audio of a card).
include(FetchContent)
FetchContent_Declare(reader_sqlite_src
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA512=71e9d81ee7e1160b9da6da0d390e3bd7dc8b434b157db6ba0e0a95446ae50c95488af23b93ecb02ea8b5c305cb16d9e20b174829e41dc17ec747efcb6a7e64d1)
FetchContent_MakeAvailable(reader_sqlite_src)
add_library(reader_sqlite STATIC EXCLUDE_FROM_ALL ${reader_sqlite_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(reader_sqlite SYSTEM PUBLIC ${reader_sqlite_src_SOURCE_DIR})
target_compile_definitions(reader_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0)
if(MSVC)
    target_compile_options(reader_sqlite PRIVATE /w)
else()
    target_compile_options(reader_sqlite PRIVATE -w)
endif()

set(QUAZIP_QT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
set(QUAZIP_INSTALL OFF CACHE BOOL "" FORCE)
set(QUAZIP_ENABLE_QTEXTCODEC OFF CACHE BOOL "" FORCE)
set(QUAZIP_BZIP2 OFF CACHE BOOL "" FORCE)
set(QUAZIP_USE_QT_ZLIB OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(reader_quazip
    URL https://github.com/stachenov/quazip/archive/2e95c9001b.tar.gz)
FetchContent_Declare(reader_opus
    URL https://github.com/xiph/opus/archive/a3f0ec02b3.tar.gz)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(reader_quazip reader_opus)
set(BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS_SAVED}")
if(NOT TARGET Opus::opus)
    add_library(Opus::opus ALIAS opus)
endif()

add_library(reader_desktop STATIC
    ${READER_SHARED}/reader/desktop/anki.cpp
    ${READER_SHARED}/reader/desktop/anki.h
    ${READER_SHARED}/reader/desktop/anki_index.cpp
    ${READER_SHARED}/reader/desktop/anki_index.h
    ${READER_SHARED}/reader/desktop/popup_model.cpp
    ${READER_SHARED}/reader/desktop/popup_model.h
    ${READER_SHARED}/reader/desktop/card_builder.cpp
    ${READER_SHARED}/reader/desktop/card_builder.h
    ${READER_SHARED}/reader/desktop/catalog.cpp
    ${READER_SHARED}/reader/desktop/catalog.h
    ${READER_SHARED}/reader/desktop/deinflector.cpp
    ${READER_SHARED}/reader/desktop/deinflector.h
    ${READER_SHARED}/reader/desktop/dictionary.cpp
    ${READER_SHARED}/reader/desktop/dictionary.h
    ${READER_SHARED}/reader/desktop/discord_presence.cpp
    ${READER_SHARED}/reader/desktop/discord_presence.h
    ${READER_SHARED}/reader/desktop/dictionary_importer.cpp
    ${READER_SHARED}/reader/desktop/glossary.cpp
    ${READER_SHARED}/reader/desktop/glossary.h
    ${READER_SHARED}/reader/desktop/japanese_text.cpp
    ${READER_SHARED}/reader/desktop/japanese_text.h
    ${READER_SHARED}/reader/desktop/media.cpp
    ${READER_SHARED}/reader/desktop/media.h
    ${READER_SHARED}/reader/desktop/reader_database.cpp
    ${READER_SHARED}/reader/desktop/reader_database.h
    ${READER_SHARED}/reader/desktop/screen_text.cpp
    ${READER_SHARED}/reader/desktop/screen_text.h
    ${READER_SHARED}/reader/desktop/script_index.cpp
    ${READER_SHARED}/reader/desktop/script_index.h
    ${READER_SHARED}/reader/desktop/settings.cpp
    ${READER_SHARED}/reader/desktop/settings.h
    ${READER_SHARED}/reader/desktop/sqlite_db.cpp
    ${READER_SHARED}/reader/desktop/sqlite_db.h
    ${READER_SHARED}/reader/desktop/stats.cpp
    ${READER_SHARED}/reader/desktop/steam_bridge.cpp
    ${READER_SHARED}/reader/desktop/steam_bridge.h
    ${READER_SHARED}/reader/desktop/stats.h
    ${READER_TEXT_SERVER_SOURCES}
    ${READER_SHARED}/reader/desktop/text_line.cpp
    ${READER_SHARED}/reader/desktop/text_line.h
    ${READER_SHARED}/reader/desktop/word_audio.cpp
    ${READER_SHARED}/reader/desktop/word_audio.h
)

target_link_libraries(reader_desktop PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
target_link_libraries(reader_desktop PRIVATE reader_sqlite QuaZip::QuaZip Opus::opus)
target_include_directories(reader_desktop PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(reader_desktop PRIVATE ZLIB::ZLIB)
set_target_properties(reader_desktop PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(reader_desktop PRIVATE /EHsc)
else()
    target_compile_options(reader_desktop PRIVATE -fexceptions)
endif()
target_compile_definitions(reader_desktop PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
if (NOT MSVC)
    target_compile_options(reader_desktop PRIVATE -Wno-shadow -Wno-conversion)
endif()

if (READER_DESKTOP_TESTS)
    add_subdirectory(tests)
endif()
