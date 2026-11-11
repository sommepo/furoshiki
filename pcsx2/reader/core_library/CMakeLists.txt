# SPDX-FileCopyrightText: PCSX2 Reader fork contributors
# SPDX-License-Identifier: GPL-3.0+

# PCSX2 Reader as a core of Reader Home, the one program (cores/reader_core.h at the top of the
# repository, Reader Home's docs/one-program.md): reader_core_pcsx2.so (.dll), a module Reader
# Home loads. PCSX2's emulator (the PCSX2 library, with its GS renderers and the CRT picture)
# with pcsx2_core.cpp in reader/core/emulator.cpp's place: what pcsx2-gsrunner and the Qt
# frontend define of PCSX2's Host, without a window, settings file or input of its own. No Qt,
# no reader engine: those are the program's. Built by tools/reader/build-core.sh (READER_CORE).

add_library(reader_core_pcsx2 MODULE pcsx2_core.cpp wide_patches.cpp)
set_target_properties(reader_core_pcsx2 PROPERTIES PREFIX "" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(reader_core_pcsx2 PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/pcsx2
	${CMAKE_BINARY_DIR}/common/include ${CMAKE_SOURCE_DIR}/../cores)
target_link_libraries(reader_core_pcsx2 PRIVATE PCSX2_FLAGS PCSX2)
if(X11_API)
	target_link_libraries(reader_core_pcsx2 PRIVATE X11::X11)
endif()

# Only reader_core_get leaves the module: two cores' copies of zlib, fmt or imgui never meet
# in one process, and nothing of the module is taken for the program's own.
if(NOT WIN32)
	file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: reader_core_get; local: *; };\n")
	target_link_options(reader_core_pcsx2 PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map" -Wl,--no-undefined -Wl,-Bsymbolic
		-static-libstdc++ -static-libgcc)
endif()
