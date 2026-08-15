add_executable(test_platform_game_storage_model
    test_game_storage_model.c
)
target_link_libraries(test_platform_game_storage_model
    PRIVATE platform_game_storage_model
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_platform_game_storage_model PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_platform_game_storage_model PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME platform_game_storage_model
         COMMAND test_platform_game_storage_model)

add_executable(test_platform_game_storage_files
    test_game_storage_files.c
)
target_link_libraries(test_platform_game_storage_files
    PRIVATE platform_game_storage_model
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_platform_game_storage_files PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_platform_game_storage_files PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME platform_game_storage_files
         COMMAND test_platform_game_storage_files)

add_executable(test_platform_game_storage_msc_write_policy
    test_msc_write_policy.c
)
target_link_libraries(test_platform_game_storage_msc_write_policy
    PRIVATE platform_game_storage_model
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_platform_game_storage_msc_write_policy PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_platform_game_storage_msc_write_policy PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME platform_game_storage_msc_write_policy
         COMMAND test_platform_game_storage_msc_write_policy)
