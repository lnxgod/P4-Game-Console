# SPDX-License-Identifier: MIT

add_executable(p4_content_catalog_tests test_content_catalog.c)
target_include_directories(p4_content_catalog_tests PRIVATE ../src)
target_link_libraries(p4_content_catalog_tests PRIVATE p4_content_catalog)
target_compile_features(p4_content_catalog_tests PRIVATE c_std_11)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(p4_content_catalog_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(p4_content_catalog_tests PRIVATE
        -fsanitize=address,undefined
    )
endif()
add_test(NAME p4_content_catalog_tests COMMAND p4_content_catalog_tests)
