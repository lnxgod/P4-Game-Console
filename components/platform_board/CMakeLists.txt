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
    project(platform_board VERSION 0.1.0 LANGUAGES C)

    add_library(platform_board STATIC src/platform_board.c)
    target_include_directories(platform_board PUBLIC include tests/include)
    target_compile_features(platform_board PUBLIC c_std_11)
    target_compile_definitions(platform_board PRIVATE
        CONFIG_P4_BOARD_ELECROW_CROWPANEL_ADVANCED_10=1
        CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B=0
    )
    add_library(platform_board_olimex STATIC src/platform_board.c)
    target_include_directories(platform_board_olimex PUBLIC include tests/include)
    target_compile_features(platform_board_olimex PUBLIC c_std_11)
    target_compile_definitions(platform_board_olimex PRIVATE
        CONFIG_P4_BOARD_ELECROW_CROWPANEL_ADVANCED_10=0
        CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B=1
    )
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        foreach(target platform_board platform_board_olimex)
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            )
        endforeach()
    endif()

    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
