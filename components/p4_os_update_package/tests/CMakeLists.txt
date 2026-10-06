add_executable(test_p4_os_update_package test_os_update_package.c)
target_link_libraries(test_p4_os_update_package PRIVATE p4_os_update_package)
target_compile_features(test_p4_os_update_package PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_p4_os_update_package PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(test_p4_os_update_package PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_os_update_package COMMAND test_p4_os_update_package)

add_executable(test_p4_os_update_tab5 test_os_update_package.c ../src/os_update_package.c)
target_include_directories(test_p4_os_update_tab5 PRIVATE ../include)
target_compile_features(test_p4_os_update_tab5 PRIVATE c_std_11)
target_compile_definitions(test_p4_os_update_tab5 PRIVATE P4_OS_UPDATE_ACCEPTED_TARGET="esp32p4-tab5" P4_OS_UPDATE_SLOT_BYTES=0x7f0000)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_p4_os_update_tab5 PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_p4_os_update_tab5 PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME p4_os_update_tab5_board_binding COMMAND test_p4_os_update_tab5)
