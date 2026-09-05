if(COMMAND idf_component_register)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "olimex-esp32-p4-pc")
        set(PLATFORM_DISPLAY_DRIVER_SOURCE "src/platform_display_olimex.c")
        set(PLATFORM_DISPLAY_DRIVER_REQUIREMENTS
            esp_driver_i2c
            espressif__esp_lcd_lt8912b
        )
    elseif(P4_ACTIVE_BOARD_PROFILE STREQUAL
           "waveshare-esp32-p4-wifi6-touch-lcd-4.3")
        set(PLATFORM_DISPLAY_DRIVER_SOURCE "src/platform_display.c")
        set(PLATFORM_DISPLAY_DRIVER_REQUIREMENTS
            esp_driver_gpio
            esp_driver_ledc
            esp_driver_ppa
            espressif__esp_lcd_st7701
        )
    else()
        set(PLATFORM_DISPLAY_DRIVER_SOURCE "src/platform_display.c")
        set(PLATFORM_DISPLAY_DRIVER_REQUIREMENTS
            esp_driver_gpio
            esp_driver_ledc
            espressif__esp_lcd_ek79007
        )
    endif()
    idf_component_register(
        SRCS
            "${PLATFORM_DISPLAY_DRIVER_SOURCE}"
            "src/platform_display_layout.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "src"
        REQUIRES
            esp_hw_support
            esp_lcd
            esp_timer
            freertos
            heap
            platform_board
        PRIV_REQUIRES
            ${PLATFORM_DISPLAY_DRIVER_REQUIREMENTS}
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
    target_include_directories(platform_display_layout PUBLIC
        src
        include
        "${CMAKE_CURRENT_LIST_DIR}/../platform_board/include"
    )
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
