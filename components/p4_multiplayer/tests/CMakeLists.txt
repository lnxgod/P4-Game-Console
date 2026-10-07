# SPDX-License-Identifier: MIT

add_executable(p4_multiplayer_tests test_p4_multiplayer.c)
target_link_libraries(p4_multiplayer_tests PRIVATE p4_multiplayer)
target_compile_features(p4_multiplayer_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_multiplayer_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_multiplayer_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_multiplayer_tests COMMAND p4_multiplayer_tests)

add_executable(p4_dice_tests test_dice_accessory.c)
target_link_libraries(p4_dice_tests PRIVATE p4_multiplayer)
target_compile_options(p4_dice_tests PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
target_link_options(p4_dice_tests PRIVATE -fsanitize=address,undefined)
add_test(NAME p4_dice_tests COMMAND p4_dice_tests)
add_executable(test_group test_group.c)
target_link_libraries(test_group PRIVATE p4_multiplayer)
target_compile_options(test_group PRIVATE -fsanitize=address,undefined)
target_link_options(test_group PRIVATE -fsanitize=address,undefined)
add_test(NAME multiplayer_group COMMAND test_group)

add_executable(test_join_retry test_join_retry.c)
target_link_libraries(test_join_retry PRIVATE p4_multiplayer)
target_compile_options(test_join_retry PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined)
target_link_options(test_join_retry PRIVATE -fsanitize=address,undefined)
add_test(NAME multiplayer_join_retry COMMAND test_join_retry)
