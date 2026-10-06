add_executable(maze_chase_tests test_maze_chase.c)
target_include_directories(maze_chase_tests PRIVATE ../src)
target_link_libraries(maze_chase_tests PRIVATE maze_chase p4_game_api)
target_compile_features(maze_chase_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(maze_chase_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(maze_chase_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME maze_chase_tests COMMAND maze_chase_tests)

add_executable(maze_chase_touch_pipeline_tests
    test_touch_pipeline.c
    ../../../tools/p4-game-host/host_service.c)
target_include_directories(maze_chase_touch_pipeline_tests PRIVATE
    ../src ../../../tools/p4-game-host)
target_link_libraries(maze_chase_touch_pipeline_tests PRIVATE maze_chase p4_game_api)
target_compile_features(maze_chase_touch_pipeline_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(maze_chase_touch_pipeline_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(maze_chase_touch_pipeline_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME maze_chase_touch_pipeline_tests COMMAND maze_chase_touch_pipeline_tests)

add_executable(maze_chase_backdrop_tests test_backdrop.c)
target_include_directories(maze_chase_backdrop_tests PRIVATE ../include ../src)
target_link_libraries(maze_chase_backdrop_tests PRIVATE p4_game_api)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(maze_chase_backdrop_tests PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
    target_link_options(maze_chase_backdrop_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME maze_chase_backdrop_tests COMMAND maze_chase_backdrop_tests)
