set(PLATFORM_USB_HOST_PORTABLE_SOURCES
    src/policy.c
    src/model.c
)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            ${PLATFORM_USB_HOST_PORTABLE_SOURCES}
            src/platform_usb_host.c
        INCLUDE_DIRS include
        REQUIRES
            freertos
            log
            usb
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
