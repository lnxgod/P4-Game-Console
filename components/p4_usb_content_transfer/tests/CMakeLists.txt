add_executable(test_h1_usb_drive_control
    test_h1_usb_drive_control.c
)
target_link_libraries(test_h1_usb_drive_control PRIVATE p4_h1_usb_drive_control)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_h1_usb_drive_control PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_h1_usb_drive_control PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME h1_usb_drive_control COMMAND test_h1_usb_drive_control)

add_executable(test_clock_control test_clock_control.c)
target_link_libraries(test_clock_control PRIVATE p4_h1_usb_drive_control)
target_compile_options(test_clock_control PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(test_clock_control PRIVATE -fsanitize=address,undefined)
add_test(NAME usb_clock_control COMMAND test_clock_control)

add_executable(test_debug_control test_debug_control.c)
target_link_libraries(test_debug_control PRIVATE p4_h1_usb_drive_control)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_debug_control PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_debug_control PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME usb_debug_control COMMAND test_debug_control)

add_executable(test_debug_snapshot test_debug_snapshot.c)
target_link_libraries(test_debug_snapshot PRIVATE p4_h1_usb_drive_control)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_debug_snapshot PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_debug_snapshot PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME usb_debug_snapshot COMMAND test_debug_snapshot)

# Exercise the actual device parser with fake SDK services and real SHA-256.
add_executable(test_file_transfer_native_only
    test_file_transfer_native_only.c
    ../src/file_transfer.c
    ../../p4_game_package/src/game_package.c
    ../../p4_game_package/src/game_resource.c
    ../../p4_game_api/src/game_runtime.c
    stubs/sha256.c)
target_include_directories(test_file_transfer_native_only PRIVATE
    stubs ../include ../../p4_game_package/include ../../p4_game_api/include)
target_compile_features(test_file_transfer_native_only PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_file_transfer_native_only PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_file_transfer_native_only PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME file_transfer_native_only COMMAND test_file_transfer_native_only)

add_executable(test_file_transfer_failures
    test_file_transfer_failures.c
    ../../p4_game_package/src/game_package.c
    ../../p4_game_package/src/game_resource.c
    ../../p4_game_api/src/game_runtime.c
    stubs/sha256.c)
target_include_directories(test_file_transfer_failures PRIVATE
    stubs ../include ../../p4_game_package/include ../../p4_game_api/include)
target_compile_features(test_file_transfer_failures PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_file_transfer_failures PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_file_transfer_failures PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME file_transfer_sync_failure COMMAND test_file_transfer_failures sync)
add_test(NAME file_transfer_transaction_namespace COMMAND test_file_transfer_failures namespace)

add_executable(test_content_transfer_batch test_content_transfer_batch.c
    ../src/content_transfer.c stubs/sha256.c)
target_include_directories(test_content_transfer_batch PRIVATE
    stubs ../include ../../platform_game_storage/include)
target_compile_features(test_content_transfer_batch PRIVATE c_std_11)
target_compile_definitions(test_content_transfer_batch PRIVATE
    PACK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../../game-data/pure-hades/v0.6"
    ARENA_NOTICE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../game-data/arena-compact-v1/NOTICE.txt")
target_compile_options(test_content_transfer_batch PRIVATE
    -Wall -Wextra -Wconversion -Wshadow -Werror
    -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(test_content_transfer_batch PRIVATE -fsanitize=address,undefined)
add_test(NAME content_transfer_batch COMMAND test_content_transfer_batch)
