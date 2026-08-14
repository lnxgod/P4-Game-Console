if(COMMAND idf_component_register)
    idf_component_register(
        SRCS
            "src/platform_display.c"
            "src/platform_display_layout.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src"
        REQUIRES
            esp_driver_gpio
            esp_driver_ledc
            esp_hw_support
            esp_lcd
            esp_lcd_ek79007
            freertos
            heap
    )

    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wshadow
        -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_display_layout VERSION 0.1.0 LANGUAGES C)

    add_library(platform_display_layout STATIC src/platform_display_layout.c)
    target_include_directories(platform_display_layout PUBLIC src)
    target_compile_features(platform_display_layout PUBLIC c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_display_layout PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Werror
        )
    endif()

    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
