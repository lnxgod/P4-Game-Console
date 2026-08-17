if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/console_shell.c"
        INCLUDE_DIRS "include"
        REQUIRES platform_board p4_desktop p4_game_api
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(console_shell VERSION 0.1.0 LANGUAGES C)

    add_library(console_shell STATIC src/console_shell.c)
    target_include_directories(console_shell PUBLIC include)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../p4_game_api"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
    )
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../p4_desktop"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_desktop"
    )
    target_link_libraries(console_shell PUBLIC p4_desktop p4_game_api)
    target_compile_features(console_shell PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(console_shell PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(console_shell PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    include(CTest)
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
