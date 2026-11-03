# Reader fork: headless development and test harness (Linux only).
add_executable(reader_harness
    harness.cpp
)

set_target_properties(reader_harness PROPERTIES OUTPUT_NAME "azahar-reader-harness")

target_link_libraries(reader_harness PRIVATE citra_common citra_core video_core audio_core
                                             input_common network fmt lodepng)
