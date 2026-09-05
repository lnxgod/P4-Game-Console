# SPDX-License-Identifier: MIT

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "src/platform_battery_math.c" "src/platform_battery_adc.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_adc esp_driver_gpio log
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_battery VERSION 0.1.0 LANGUAGES C)
    add_library(platform_battery_math STATIC src/platform_battery_math.c)
    target_include_directories(platform_battery_math PUBLIC include)
    target_compile_features(platform_battery_math PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_battery_math PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
