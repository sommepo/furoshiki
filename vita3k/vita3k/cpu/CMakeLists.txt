set(SOURCE_LIST
include/cpu/state.h
include/cpu/common.h
include/cpu/functions.h
include/cpu/impl/dynarmic_cpu.h
include/cpu/impl/dynarmic_reader.h
include/cpu/impl/interface.h
include/cpu/disasm/functions.h
include/cpu/disasm/state.h

src/disasm.cpp
src/cpu.cpp
src/dynarmic_cpu.cpp
src/dynarmic_reader.cpp
)

add_library(
cpu
STATIC
${SOURCE_LIST}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

target_include_directories(cpu PUBLIC include)
# Reader fork: reader/core/emulator.h.
target_include_directories(cpu PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(cpu PUBLIC mem util)
target_link_libraries(cpu PRIVATE dynarmic capstone merry::mcl)
