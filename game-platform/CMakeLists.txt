cmake_minimum_required(VERSION 3.20)

project(p4_game_platform_host LANGUAGES C)

include(CTest)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/p4_content_catalog"
    "${CMAKE_CURRENT_BINARY_DIR}/p4_content_catalog"
)
add_subdirectory(firmware/components/p4_game_runtime_core)
add_subdirectory(firmware/components/p4_cartridge_transfer)
add_subdirectory(firmware/components/p4_lua_runtime)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/p4_script_renderer"
    "${CMAKE_CURRENT_BINARY_DIR}/p4_script_renderer"
)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/p4_script_audio"
    "${CMAKE_CURRENT_BINARY_DIR}/p4_script_audio"
)
