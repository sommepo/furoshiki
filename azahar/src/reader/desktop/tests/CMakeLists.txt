# Copyright Azahar Reader fork contributors
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

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
    # The text engine with made-up memory: progress set by hand (reader.progress), the
    # player's actions (reader.action), the screens of a layout. It is the reader layer every
    # reader fork shares and needs nothing of the emulator but its Lua.
    ${READER_SHARED}/reader/desktop/tests/progress_test.cpp
    ${READER_SHARED}/reader/core/audio_tap.cpp
    ${READER_SHARED}/reader/core/engine.cpp
    ${READER_SHARED}/reader/core/lua_api.cpp
    ${READER_SHARED}/reader/core/text_codec.cpp
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

set(READER_BUNDLED_PROFILE_DIR "${PROJECT_SOURCE_DIR}/src/android/app/src/main/assets/reader/profiles")
target_compile_definitions(reader_desktop_tests PRIVATE
    READER_BUNDLED_PROFILE_DIR="${READER_BUNDLED_PROFILE_DIR}")
target_link_libraries(reader_desktop_tests PRIVATE reader_desktop reader_lua QuaZip::QuaZip
                                                   Catch2::Catch2WithMain Threads::Threads)
if (NOT MSVC)
    target_compile_options(reader_desktop_tests PRIVATE
        -Wno-shadow -Wno-conversion -Wno-unused-variable -Wno-unused-parameter
        -Wno-missing-field-initializers)
endif()
