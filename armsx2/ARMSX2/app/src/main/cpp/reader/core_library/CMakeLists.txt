# SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
# SPDX-License-Identifier: GPL-3.0+

# ARMSX2 as a core of Furoshiki for Android, the one app (android/ at the top of the repository,
# cores/reader_core.h): libreader_core_armsx2.so. ARMSX2's emulator (the PCSX2 library) with
# armsx2_core.cpp in reader/core/emulator.cpp's place and in native-lib.cpp's: PCSX2's Host,
# without a Java front, a settings file or input of its own. Built by
# tools/reader/build-core-android.sh (READER_CORE).

add_library(reader_core_armsx2 MODULE armsx2_core.cpp)
set_target_properties(reader_core_armsx2 PROPERTIES PREFIX "lib" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(reader_core_armsx2 PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/pcsx2
	${CMAKE_BINARY_DIR}/common/include ${READER_SHARED}/.. ${READER_SHARED}/../cores ${READER_SHARED}/../pcsx2)
target_link_libraries(reader_core_armsx2 PRIVATE PCSX2_FLAGS PCSX2 common imgui libzip::zip SDL3::SDL3 shaderc
	android log vixl adrenotools)

# Only reader_core_get leaves the module.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
target_link_options(reader_core_armsx2 PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map"
	-Wl,--no-undefined -Wl,-Bsymbolic "-Wl,-z,max-page-size=16384")
