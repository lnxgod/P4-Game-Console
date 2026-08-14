if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            src/model.c
            src/platform_gamepad_usb.c
            src/usb_host_open_guard.c
            src/usb_host_open_wrapper.c
        INCLUDE_DIRS include
        REQUIRES
            esp_timer
            freertos
            gamepad_core
            log
            mbedtls
            platform_usb_host
            usb_host_hid
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Werror
    )
    # usb_host_hid 1.2.0 can close an indeterminate local handle when a
    # NEW_DEV/open race fails. Route every open through the pinned-version guard.
    target_link_options(${COMPONENT_LIB} INTERFACE
        "LINKER:--wrap=usb_host_device_open"
        "LINKER:--undefined=__wrap_usb_host_device_open"
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_gamepad_usb VERSION 0.1.0 LANGUAGES C)

    add_library(platform_gamepad_model STATIC
        src/model.c
        src/usb_host_open_guard.c
        ../gamepad_core/src/gamepad.c
    )
    target_include_directories(platform_gamepad_model PUBLIC
        include
        src
        ../gamepad_core/include
    )
    target_compile_features(platform_gamepad_model PUBLIC c_std_11)

    add_executable(platform_gamepad_model_tests
        tests/test_platform_gamepad_model.c
    )
    target_link_libraries(platform_gamepad_model_tests PRIVATE
        platform_gamepad_model
    )
    target_compile_features(platform_gamepad_model_tests PRIVATE c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        foreach(target platform_gamepad_model platform_gamepad_model_tests)
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()

    include(CTest)
    add_test(NAME platform_gamepad_model_tests
             COMMAND platform_gamepad_model_tests)
endif()
