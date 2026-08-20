if(COMMAND idf_component_register)
    idf_component_register(
        SRCS src/ansi.c
        INCLUDE_DIRS include
        REQUIRES p4_cp437
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_ansi VERSION 0.1.0 LANGUAGES C)

    if(NOT TARGET p4_cp437)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../p4_cp437"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_cp437"
        )
    endif()

    add_library(p4_ansi STATIC src/ansi.c)
    target_include_directories(p4_ansi PUBLIC include)
    target_link_libraries(p4_ansi PUBLIC p4_cp437)
    target_compile_features(p4_ansi PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_ansi PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_ansi PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
endif()
