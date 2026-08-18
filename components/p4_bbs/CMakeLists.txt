if(COMMAND idf_component_register)
    idf_component_register(
        SRCS src/bbs_ui.c
        INCLUDE_DIRS include
        REQUIRES p4_ansi
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(p4_bbs VERSION 0.1.0 LANGUAGES C)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../p4_ansi"
        "${CMAKE_CURRENT_BINARY_DIR}/p4_ansi"
    )
    add_library(p4_bbs STATIC src/bbs_ui.c)
    target_include_directories(p4_bbs PUBLIC include)
    target_link_libraries(p4_bbs PUBLIC p4_ansi)
    target_compile_features(p4_bbs PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_bbs PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(p4_bbs PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    include(CTest)
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
