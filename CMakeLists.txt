cmake_minimum_required(VERSION 3.16)
project(panicdump VERSION 1.0.0 LANGUAGES C ASM)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(PANICDUMP_HALT_ON_FAULT  "Halt (BKPT) instead of reset after dump" OFF)
option(PANICDUMP_BUILD_EXAMPLES "Build example projects"                   OFF)
option(PANICDUMP_BUILD_TESTS    "Build host unit tests"                    OFF)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

add_library(panicdump STATIC
    src/panicdump.c
    src/panicdump_port_cortexm.c
    ports/cortexm/panicdump_fault_entry.S
)

target_include_directories(panicdump PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(PANICDUMP_HALT_ON_FAULT)
    target_compile_definitions(panicdump PUBLIC PANICDUMP_HALT_ON_FAULT)
endif()

# Cortex-M specific flags (set by toolchain file or parent project)
# Expected: -mcpu=cortex-m4 -mthumb etc.

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(PANICDUMP_BUILD_EXAMPLES)
    add_subdirectory(examples/stm32f4_demo)
    add_subdirectory(examples/generic_cortexm_demo)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS panicdump
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(FILES
    docs/FORMAT.md
    docs/LIMITATIONS.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
