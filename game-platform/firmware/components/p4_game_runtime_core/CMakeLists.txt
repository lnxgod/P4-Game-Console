if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            "src/input_latch.c"
            "src/lifecycle.c"
            "src/render_list.c"
            "src/scheduler.c"
        INCLUDE_DIRS "include"
        REQUIRES p4_game_api
    )

    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Werror -Wconversion -Wshadow -std=c11
    )
else()
    if(NOT TARGET p4_game_api)
        add_subdirectory(
            "${CMAKE_CURRENT_SOURCE_DIR}/../p4_game_api"
            "${CMAKE_CURRENT_BINARY_DIR}/p4_game_api"
        )
    endif()

    add_library(p4_game_runtime_core STATIC
        src/input_latch.c
        src/lifecycle.c
        src/render_list.c
        src/scheduler.c
    )
    target_include_directories(p4_game_runtime_core PUBLIC include)
    target_link_libraries(p4_game_runtime_core PUBLIC p4_game_api)
    target_compile_features(p4_game_runtime_core PUBLIC c_std_11)
    target_compile_options(p4_game_runtime_core PRIVATE
        -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    )

    option(P4_ENABLE_SANITIZERS "Enable host sanitizers" ON)
    if(P4_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(p4_game_runtime_core PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
        target_link_options(p4_game_runtime_core PUBLIC
            -fno-omit-frame-pointer -fsanitize=address,undefined
        )
    endif()

    if(BUILD_TESTING)
        add_executable(p4_game_runtime_core_tests tests/test_runtime_core.c)
        target_link_libraries(p4_game_runtime_core_tests PRIVATE p4_game_runtime_core)
        target_compile_features(p4_game_runtime_core_tests PRIVATE c_std_11)
        target_compile_options(p4_game_runtime_core_tests PRIVATE
            -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        )
        add_test(NAME p4_game_runtime_core COMMAND p4_game_runtime_core_tests)
    endif()
endif()
