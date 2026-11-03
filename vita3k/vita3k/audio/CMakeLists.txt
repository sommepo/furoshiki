add_library(
    audio
    STATIC
    src/audio.cpp
    src/impl/sdl_audio.cpp
    src/impl/cubeb_audio.cpp)

target_include_directories(audio PUBLIC include)
target_include_directories(audio PRIVATE ${CMAKE_SOURCE_DIR}) # Reader fork: reader/core/emulator.h
target_link_libraries(audio PRIVATE util cubeb SDL3::SDL3)
