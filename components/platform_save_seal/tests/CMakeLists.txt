# SPDX-License-Identifier: MIT

add_executable(test_platform_save_seal
    test_platform_save_seal.c
    fake_esp.c
)
target_include_directories(test_platform_save_seal PRIVATE
    stubs
    "${CMAKE_CURRENT_LIST_DIR}/../../p4_game_save/src"
)
target_link_libraries(test_platform_save_seal PRIVATE
    platform_save_seal
    p4_game_save
)
target_compile_features(test_platform_save_seal PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_platform_save_seal PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_platform_save_seal PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME platform_save_seal COMMAND test_platform_save_seal)
