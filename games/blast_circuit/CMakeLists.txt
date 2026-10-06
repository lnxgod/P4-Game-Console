# SPDX-License-Identifier: MIT

set(P4_BLAST_CIRCUIT_SOURCES
    "src/blast_circuit.c"
    "src/blast_circuit_audio.c"
    "src/blast_circuit_logic.c"
    "src/blast_circuit_levels.c"
    "src/blast_circuit_network.c"
    "src/blast_circuit_render.c"
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${P4_BLAST_CIRCUIT_SOURCES}
        REQUIRES p4_game_api
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_blast_circuit VERSION 0.2.7 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../components/p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_library(blast_circuit STATIC ${P4_BLAST_CIRCUIT_SOURCES})
    target_link_libraries(blast_circuit PUBLIC p4_game_api)
    target_compile_features(blast_circuit PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(blast_circuit PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
