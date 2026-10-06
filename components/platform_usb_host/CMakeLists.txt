set(PLATFORM_USB_HOST_PORTABLE_SOURCES
    src/policy.c
    src/model.c
)

if(COMMAND idf_component_register)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL
       "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        set(PLATFORM_USB_HOST_TRANSPORT src/platform_usb_host_tinyusb.c)
        set(PLATFORM_USB_HOST_DRIVER_REQUIREMENTS
            esp_tinyusb
            espressif__tinyusb
            p4_tinyusb_dual
            usb)
    else()
        set(PLATFORM_USB_HOST_TRANSPORT src/platform_usb_host.c)
        set(PLATFORM_USB_HOST_DRIVER_REQUIREMENTS usb)
        if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")
            list(APPEND PLATFORM_USB_HOST_DRIVER_REQUIREMENTS platform_tab5)
        endif()
    endif()
    idf_component_register(
        SRCS
            ${PLATFORM_USB_HOST_PORTABLE_SOURCES}
            ${PLATFORM_USB_HOST_TRANSPORT}
        INCLUDE_DIRS include
        REQUIRES
            esp_driver_gpio
            freertos
            log
            platform_board
            ${PLATFORM_USB_HOST_DRIVER_REQUIREMENTS}
        PRIV_REQUIRES
            hal
            soc
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_usb_host VERSION 0.1.0 LANGUAGES C)

    add_library(platform_usb_host_model STATIC ${PLATFORM_USB_HOST_PORTABLE_SOURCES})
    target_include_directories(platform_usb_host_model PUBLIC include)
    target_compile_features(platform_usb_host_model PUBLIC c_std_11)

    add_executable(platform_usb_host_tests tests/test_platform_usb_host.c)
    target_link_libraries(platform_usb_host_tests PRIVATE platform_usb_host_model)
    target_compile_features(platform_usb_host_tests PRIVATE c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        foreach(target platform_usb_host_model platform_usb_host_tests)
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()

    include(CTest)
    add_test(NAME platform_usb_host_tests COMMAND platform_usb_host_tests)
endif()
