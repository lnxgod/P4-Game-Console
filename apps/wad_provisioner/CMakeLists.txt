cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

set(P4_LOCAL_DOOM_WAD
    "${CMAKE_CURRENT_LIST_DIR}/../../local-data/doom/doom1.wad"
    CACHE FILEPATH
    "Local Doom 1.9 shareware IWAD to place in the non-redistributable provisioner"
)
get_filename_component(P4_LOCAL_DOOM_WAD "${P4_LOCAL_DOOM_WAD}" ABSOLUTE)

find_program(P4_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
execute_process(
    COMMAND
        "${P4_PYTHON3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/verify-wad-provisioner.py"
        --configure
        "${P4_LOCAL_DOOM_WAD}"
        "${CMAKE_CURRENT_LIST_DIR}/partitions.csv"
    RESULT_VARIABLE P4_PROVISIONER_INPUT_RESULT
    OUTPUT_VARIABLE P4_PROVISIONER_INPUT_OUTPUT
    ERROR_VARIABLE P4_PROVISIONER_INPUT_ERROR
)
if(NOT P4_PROVISIONER_INPUT_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Local WAD provisioner gate failed:\n${P4_PROVISIONER_INPUT_OUTPUT}${P4_PROVISIONER_INPUT_ERROR}"
    )
endif()
message(STATUS "${P4_PROVISIONER_INPUT_OUTPUT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(p4_wad_provisioner)

# The ignored local WAD enters only this local build artifact. No WAD bytes
# are copied into the source tree or used by routine Doom firmware.
target_add_binary_data(
    ${CMAKE_PROJECT_NAME}.elf
    "${P4_LOCAL_DOOM_WAD}"
    BINARY
    RENAME_TO p4_provision_wad
)
