# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS src/script_audio.c
        INCLUDE_DIRS include
        REQUIRES p4_lua_runtime
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_script_audio VERSION 0.1.0 LANGUAGES C)
    add_library(p4_script_audio STATIC src/script_audio.c)
    target_include_directories(p4_script_audio PUBLIC include)
    target_link_libraries(p4_script_audio PUBLIC p4_lua_runtime)
    target_compile_features(p4_script_audio PUBLIC c_std_11)
    target_compile_options(p4_script_audio PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Werror
    )
endif()
