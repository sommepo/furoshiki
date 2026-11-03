# Azahar Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of
# the repository, Reader Home's docs/one-program.md): Azahar's libretro build
# (ENABLE_LIBRETRO), with the libretro host every libretro-made core shares (cores/libretro)
# and this fork's glue (azahar_core.cpp, in core/reader/emulator_engine.cpp's place), as
# reader_core_azahar.so (.dll). Only reader_core_get leaves it. Built by
# tools/reader/build-core.sh (READER_CORE).

set(READER_CORES_DIR "${PROJECT_SOURCE_DIR}/../cores")
# The host names the CRT header as the other forks have it (reader/core/crt.h).
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shim/reader/core/crt.h" "#pragma once\n#include \"core/reader/crt.h\"\n")
target_sources(citra_libretro PRIVATE
    azahar_core.cpp
    ${READER_CORES_DIR}/libretro/libretro_core.cpp
    ${READER_CORES_DIR}/libretro/libretro_core.h)
target_include_directories(citra_libretro PRIVATE ${PROJECT_SOURCE_DIR}/src ${READER_CORES_DIR}
    ${READER_CORES_DIR}/libretro ${CMAKE_CURRENT_BINARY_DIR}/shim)
target_link_libraries(citra_libretro PRIVATE video_core)
set_target_properties(citra_libretro PROPERTIES OUTPUT_NAME "reader_core_azahar")
if (ANDROID)
    # The Android app's: a "lib" name, as an APK carries only such, and the picture drawn with
    # OpenGL ES on the app's Surface.
    set_target_properties(citra_libretro PROPERTIES PREFIX "lib")
    target_link_libraries(citra_libretro PRIVATE EGL GLESv3 android log)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
    target_link_options(citra_libretro PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
        "-Wl,-z,max-page-size=16384")
elseif (NOT WIN32)
    # The picture is drawn into the program's X11 window with EGL.
    find_package(X11 REQUIRED)
    find_library(READER_EGL_LIBRARY EGL REQUIRED)
    target_include_directories(citra_libretro PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(citra_libretro PRIVATE ${READER_EGL_LIBRARY} ${X11_LIBRARIES})
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
    target_link_options(citra_libretro PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map")
else()
    target_link_libraries(citra_libretro PRIVATE opengl32 gdi32 ole32 uuid shell32)
endif()
