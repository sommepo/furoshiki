# Copyright Azahar Reader fork contributors
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

# Reader fork: what the desktop app's reader layer is made of apart from its windows:
# dictionaries, lookups, cards, statistics, the backlog. It is the Android app's reader layer
# (src/android/.../reader) in C++, kept free of the emulator so that it can be tested alone
# (tools/reader/test-desktop.sh). The windows are in src/reader/qt and the repository's shared/reader/qt.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

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
target_link_libraries(reader_desktop PRIVATE reader_sqlite ZLIB::ZLIB QuaZip::QuaZip Opus::opus)
target_compile_definitions(reader_desktop PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
if (NOT MSVC)
    target_compile_options(reader_desktop PRIVATE -Wno-shadow -Wno-conversion)
endif()

if (ENABLE_READER_DESKTOP_TESTS)
    add_subdirectory(tests)
endif()
