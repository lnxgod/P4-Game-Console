add_executable(platform_storage_wad_tests test_platform_storage_wad.c)
target_link_libraries(platform_storage_wad_tests PRIVATE platform_storage_wad)
target_include_directories(platform_storage_wad_tests PRIVATE ../src)
target_compile_features(platform_storage_wad_tests PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(platform_storage_wad_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
    )

    option(PLATFORM_STORAGE_ENABLE_SANITIZERS "Enable ASan and UBSan" ON)
    if(PLATFORM_STORAGE_ENABLE_SANITIZERS)
        target_compile_options(platform_storage_wad PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(platform_storage_wad PRIVATE -fsanitize=address,undefined)
        target_compile_options(platform_storage_wad_tests PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(platform_storage_wad_tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()

add_test(NAME platform_storage_wad_tests COMMAND platform_storage_wad_tests)
