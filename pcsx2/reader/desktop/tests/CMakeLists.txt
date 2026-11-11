# SPDX-FileCopyrightText: PCSX2 Reader fork contributors (from the PPSSPP Reader fork)
# SPDX-License-Identifier: GPL-3.0+

# Reader fork: tests of the desktop reader layer's logic, with made-up text and dictionaries
# written for the test (tools/reader/test-desktop.sh). They are the Android app's JVM tests of
# the same parts, as far as those parts are the same.
add_executable(reader_desktop_tests
    ${READER_SHARED}/reader/desktop/tests/anki.cpp
    ${READER_SHARED}/reader/desktop/tests/anki_index.cpp
    ${READER_SHARED}/reader/desktop/tests/popup_model.cpp
    ${READER_SHARED}/reader/desktop/tests/card_builder.cpp
    ${READER_SHARED}/reader/desktop/tests/word_audio.cpp
    ${READER_SHARED}/reader/desktop/tests/explain.cpp
    ${READER_SHARED}/reader/desktop/tests/catalog.cpp
    console.cpp
    # The CRT picture's values and what the shader is handed (crt.h in the reader's core),
    # which has nothing of the emulator in it and is built into the tests as it is.
    ${READER_SHARED}/reader/desktop/tests/crt_test.cpp
    ${READER_SHARED}/reader/core/crt.cpp
    # Going on to the next choice (next_choice.h in the reader's core), likewise.
    ${READER_SHARED}/reader/desktop/tests/next_choice_test.cpp
    ${READER_SHARED}/reader/core/next_choice.cpp
    # Progress set by hand (reader.progress), through the engine with made-up memory.
    ${READER_SHARED}/reader/desktop/tests/progress_test.cpp
    # Touch play: reader.cursor, and each visual novel's profile against made-up memory.
    touch_play_test.cpp
    ${READER_SHARED}/reader/desktop/tests/deinflector.cpp
    ${READER_SHARED}/reader/desktop/tests/dictionary.cpp
    ${READER_SHARED}/reader/desktop/tests/discord_presence.cpp
    ${READER_SHARED}/reader/desktop/tests/glossary.cpp
    ${READER_SHARED}/reader/desktop/tests/media.cpp
    ${READER_SHARED}/reader/desktop/tests/screen_text.cpp
    ${READER_SHARED}/reader/desktop/tests/script_index.cpp
    ${READER_SHARED}/reader/desktop/tests/stats.cpp
    ${READER_SHARED}/reader/desktop/tests/steam_bridge.cpp
    ${READER_SHARED}/reader/desktop/tests/test_util.h
    ${READER_SHARED}/reader/desktop/tests/text.cpp
    ${READER_TEXT_SERVER_TESTS}
)

FetchContent_Declare(reader_catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz)
FetchContent_MakeAvailable(reader_catch2)
find_package(Threads REQUIRED)
set_target_properties(reader_desktop_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(reader_desktop_tests PRIVATE -fexceptions)
set(READER_BUNDLED_PROFILE_DIR "${CMAKE_SOURCE_DIR}/reader/profiles")
target_compile_definitions(reader_desktop_tests PRIVATE
    READER_BUNDLED_PROFILE_DIR="${READER_BUNDLED_PROFILE_DIR}")
target_link_libraries(reader_desktop_tests PRIVATE reader_desktop reader_core QuaZip::QuaZip
                                                   Catch2::Catch2WithMain Threads::Threads)
if (NOT MSVC)
    target_compile_options(reader_desktop_tests PRIVATE
        -Wno-shadow -Wno-conversion -Wno-unused-variable -Wno-unused-parameter
        -Wno-missing-field-initializers)
endif()
