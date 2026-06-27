cmake_minimum_required(VERSION 3.16)
project(panicdump VERSION 2.0.0 LANGUAGES C ASM)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(PANICDUMP_BUILD_TESTS "Build host tests" OFF)
option(PANICDUMP_BUILD_EXAMPLES "Reserved; examples are not built in this revision" OFF)
set(PANICDUMP_PORT "auto" CACHE STRING "panicdump port selection: auto, host, cortexm")
set_property(CACHE PANICDUMP_PORT PROPERTY STRINGS auto host cortexm)
set(PANICDUMP_RAM_BASE "0x20000000" CACHE STRING "Cortex-M retained RAM base")
set(PANICDUMP_RAM_END "0x20020000" CACHE STRING "Cortex-M retained RAM end")

if(PANICDUMP_BUILD_EXAMPLES)
    message(FATAL_ERROR "PANICDUMP_BUILD_EXAMPLES is not available in this revision")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PANICDUMP_SYSTEM_PROCESSOR)
if(PANICDUMP_SYSTEM_PROCESSOR MATCHES "arm|thumb|cortex")
    set(PANICDUMP_SYSTEM_IS_ARM TRUE)
else()
    set(PANICDUMP_SYSTEM_IS_ARM FALSE)
endif()

if(PANICDUMP_PORT STREQUAL "auto")
    if(PANICDUMP_BUILD_TESTS)
        set(PANICDUMP_PORT host)
    elseif(PANICDUMP_SYSTEM_IS_ARM)
        set(PANICDUMP_PORT cortexm)
    else()
        set(PANICDUMP_PORT host)
    endif()
elseif(NOT PANICDUMP_PORT STREQUAL "host" AND NOT PANICDUMP_PORT STREQUAL "cortexm")
    message(FATAL_ERROR "PANICDUMP_PORT must be auto, host, or cortexm")
endif()

if(PANICDUMP_BUILD_TESTS AND NOT PANICDUMP_PORT STREQUAL "host")
    message(STATUS "PANICDUMP_BUILD_TESTS forces the host port in this revision")
    set(PANICDUMP_PORT host)
endif()

function(panicdump_apply_common target)
    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${target} PRIVATE c_std_99)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
endfunction()

add_library(panicdump_core_obj OBJECT src/panicdump.c)
panicdump_apply_common(panicdump_core_obj)

if(PANICDUMP_PORT STREQUAL "host")
    target_compile_definitions(panicdump_core_obj PRIVATE PANICDUMP_HOST_BUILD)
    add_library(panicdump_port_host_obj OBJECT src/panicdump_port_host.c)
    panicdump_apply_common(panicdump_port_host_obj)
    target_compile_definitions(panicdump_port_host_obj PRIVATE PANICDUMP_HOST_BUILD)
else()
    target_compile_definitions(panicdump_core_obj PRIVATE
        PANICDUMP_RAM_BASE=${PANICDUMP_RAM_BASE}
        PANICDUMP_RAM_END=${PANICDUMP_RAM_END}
    )
    add_library(panicdump_port_cortexm_obj OBJECT
        src/panicdump_port_cortexm.c
        ports/cortexm/panicdump_fault_entry.S
    )
    panicdump_apply_common(panicdump_port_cortexm_obj)
    target_compile_definitions(panicdump_port_cortexm_obj PRIVATE
        PANICDUMP_RAM_BASE=${PANICDUMP_RAM_BASE}
        PANICDUMP_RAM_END=${PANICDUMP_RAM_END}
    )
endif()

add_library(panicdump STATIC)
panicdump_apply_common(panicdump)
target_sources(panicdump PRIVATE
    $<TARGET_OBJECTS:panicdump_core_obj>
)
add_library(panicdump::panicdump ALIAS panicdump)

if(PANICDUMP_PORT STREQUAL "cortexm")
    target_sources(panicdump PRIVATE $<TARGET_OBJECTS:panicdump_port_cortexm_obj>)
else()
    target_sources(panicdump PRIVATE $<TARGET_OBJECTS:panicdump_port_host_obj>)
    target_compile_definitions(panicdump PUBLIC PANICDUMP_HOST_BUILD)
endif()

install(TARGETS panicdump
    EXPORT panicdumpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

set(PANICDUMP_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/panicdump)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/panicdumpConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/panicdumpConfig.cmake
    INSTALL_DESTINATION ${PANICDUMP_CMAKE_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/panicdumpConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT panicdumpTargets
    NAMESPACE panicdump::
    DESTINATION ${PANICDUMP_CMAKE_DIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/panicdumpConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/panicdumpConfigVersion.cmake
    DESTINATION ${PANICDUMP_CMAKE_DIR}
)

if(PANICDUMP_BUILD_TESTS)
    enable_testing()

    add_executable(panicdump_core_test tests/panicdump_core_test.c)
    panicdump_apply_common(panicdump_core_test)
    target_link_libraries(panicdump_core_test PRIVATE panicdump)
    target_compile_definitions(panicdump_core_test PRIVATE PANICDUMP_HOST_BUILD)
    add_test(NAME panicdump_core_test COMMAND panicdump_core_test)
endif()
