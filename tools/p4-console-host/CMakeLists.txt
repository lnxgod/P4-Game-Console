# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.19)
project(p4_console_host VERSION 0.1.0 LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../components/console_shell"
    "${CMAKE_CURRENT_BINARY_DIR}/console_shell"
)
target_compile_definitions(console_shell PUBLIC
    CONSOLE_SHELL_TARGET_WIDTH=768U
    CONSOLE_SHELL_TARGET_HEIGHT=480U
)

add_executable(p4_console_host console_host.c)
set_target_properties(p4_console_host PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.p4console.console-host"
    MACOSX_BUNDLE_BUNDLE_NAME "P4 Console OS"
)
target_link_libraries(p4_console_host PRIVATE console_shell PkgConfig::SDL3)
target_compile_features(p4_console_host PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_console_host PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_console_host PRIVATE
        -fsanitize=address,undefined
    )
endif()

include(CTest)
add_test(NAME p4_console_host_smoke COMMAND p4_console_host --frames 8)
set_tests_properties(p4_console_host_smoke PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy"
)
