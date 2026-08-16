if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/platform_board.c"
        INCLUDE_DIRS "include"
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_board_contract VERSION 0.1.0 LANGUAGES C)
    add_library(platform_board_contract STATIC src/platform_board.c)
    target_include_directories(platform_board_contract PUBLIC include)
    target_compile_features(platform_board_contract PUBLIC c_std_11)
endif()
