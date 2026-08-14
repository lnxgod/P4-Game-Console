add_executable(platform_readonly_blob_tests test_readonly_blob_core.c)
target_link_libraries(platform_readonly_blob_tests PRIVATE platform_readonly_blob)
target_include_directories(platform_readonly_blob_tests PRIVATE ../src)
target_compile_features(platform_readonly_blob_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_readonly_blob_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(PLATFORM_READONLY_BLOB_ENABLE_SANITIZERS
           "Enable ASan and UBSan for native blob tests" ON)
    if(PLATFORM_READONLY_BLOB_ENABLE_SANITIZERS)
        target_compile_options(platform_readonly_blob PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_readonly_blob PRIVATE
            -fsanitize=address,undefined)
        target_compile_options(platform_readonly_blob_tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_readonly_blob_tests PRIVATE
            -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME platform_readonly_blob_tests COMMAND platform_readonly_blob_tests)
