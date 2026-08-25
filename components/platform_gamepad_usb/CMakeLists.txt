if(COMMAND idf_component_register)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
        set(PLATFORM_GAMEPAD_USB_TRANSPORT
            src/platform_usb_input_olimex.c)
        set(PLATFORM_GAMEPAD_USB_GUARDS
            src/usb_host_open_guard.c
            src/usb_host_open_wrapper.c)
        set(PLATFORM_GAMEPAD_USB_DRIVER_REQUIREMENTS usb_host_hid)
    elseif(P4_ACTIVE_BOARD_PROFILE STREQUAL
           "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        set(PLATFORM_GAMEPAD_USB_TRANSPORT
            src/platform_gamepad_usb_tinyusb.c)
        set(PLATFORM_GAMEPAD_USB_GUARDS)
        set(PLATFORM_GAMEPAD_USB_DRIVER_REQUIREMENTS
            esp_tinyusb
            espressif__tinyusb
            p4_tinyusb_dual)
    else()
        set(PLATFORM_GAMEPAD_USB_TRANSPORT
            src/platform_gamepad_usb.c)
        set(PLATFORM_GAMEPAD_USB_GUARDS
            src/usb_host_open_guard.c
            src/usb_host_open_wrapper.c)
        set(PLATFORM_GAMEPAD_USB_DRIVER_REQUIREMENTS usb_host_hid)
    endif()
    idf_component_register(
        SRCS
            src/input_model.c
            ${PLATFORM_GAMEPAD_USB_TRANSPORT}
            ${PLATFORM_GAMEPAD_USB_GUARDS}
        INCLUDE_DIRS include
        REQUIRES
            esp_timer
            freertos
            gamepad_core
            log
            mbedtls
            platform_gamepad
            platform_usb_host
            ${PLATFORM_GAMEPAD_USB_DRIVER_REQUIREMENTS}
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Werror
    )
    if(NOT P4_ACTIVE_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        # usb_host_hid 1.2.0 can close an indeterminate local handle when a
        # NEW_DEV/open race fails. Route every open through the pinned guard.
        target_link_options(${COMPONENT_LIB} INTERFACE
            "LINKER:--wrap=usb_host_device_open"
            "LINKER:--undefined=__wrap_usb_host_device_open"
        )
    endif()
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_gamepad_usb VERSION 0.1.0 LANGUAGES C)

    add_library(platform_gamepad_model STATIC
        src/input_model.c
        src/usb_host_open_guard.c
        ../gamepad_core/src/gamepad.c
        ../gamepad_core/src/snapshot.c
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
