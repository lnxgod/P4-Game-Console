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

add_executable(test_verified_reader test_verified_reader.c
    ../../p4_usb_content_transfer/tests/stubs/sha256.c)
target_include_directories(test_verified_reader PRIVATE ../../p4_usb_content_transfer/tests/stubs)
target_link_libraries(test_verified_reader PRIVATE platform_game_storage_model)
target_compile_options(test_verified_reader PRIVATE -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined)
target_link_options(test_verified_reader PRIVATE -fsanitize=address,undefined)
add_test(NAME verified_reader COMMAND test_verified_reader)

add_executable(test_verified_wad test_verified_wad.c
    ../../p4_usb_content_transfer/tests/stubs/sha256.c)
target_include_directories(test_verified_wad PRIVATE ../../p4_usb_content_transfer/tests/stubs)
target_link_libraries(test_verified_wad PRIVATE platform_game_storage_model)
target_compile_options(test_verified_wad PRIVATE -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined)
target_link_options(test_verified_wad PRIVATE -fsanitize=address,undefined)
add_test(NAME verified_wad COMMAND test_verified_wad)

add_executable(test_arena_vfs test_arena_vfs.c
    ../../../apps/console_os/components/platform_readonly_blob/platform_game_storage_blob.c)
target_include_directories(test_arena_vfs PRIVATE mocks ../include ../../platform_readonly_blob/include)
target_compile_definitions(test_arena_vfs PRIVATE CONFIG_VFS_SUPPORT_DIR=1)
target_compile_options(test_arena_vfs PRIVATE -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined)
target_link_options(test_arena_vfs PRIVATE -fsanitize=address,undefined)
add_test(NAME arena_vfs COMMAND test_arena_vfs)
