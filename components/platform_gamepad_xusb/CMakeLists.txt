if(COMMAND idf_component_register)
    idf_component_register(SRCS src/xusb_host.c INCLUDE_DIRS include
        REQUIRES gamepad_core platform_gamepad platform_usb_host usb
                 freertos esp_timer mbedtls log)
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow -Werror)
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_gamepad_xusb LANGUAGES C)
    enable_testing()
    add_subdirectory(tests)
endif()
