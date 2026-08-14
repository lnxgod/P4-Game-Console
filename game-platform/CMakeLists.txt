cmake_minimum_required(VERSION 3.20)

project(p4_game_platform_host LANGUAGES C)

include(CTest)

add_subdirectory(firmware/components/p4_game_runtime_core)
add_subdirectory(firmware/components/p4_cartridge_transfer)

