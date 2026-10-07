set(PLATFORM_TOUCH_PURE_SOURCES src/platform_touch_frame.c)

if(COMMAND idf_component_register)
    set(TOUCH_SOURCE src/platform_touch.c)
    set(TOUCH_BOARD_DEPS)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")
        set(TOUCH_SOURCE
            src/platform_touch_tab5.c
            src/platform_touch_tab5_io.c
            src/platform_touch_sampler.c
            src/platform_touch_sample_queue.c)
        set(TOUCH_BOARD_DEPS platform_tab5 espressif__esp_lcd_touch_st7123 p4_frame_scheduler)
    endif()
    idf_component_register(
        SRCS ${TOUCH_SOURCE} ${PLATFORM_TOUCH_PURE_SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES
            esp_driver_gpio
            esp_driver_i2c
            esp_lcd
            esp_timer
            espressif__esp_lcd_touch
            espressif__esp_lcd_touch_gt911
            platform_board ${TOUCH_BOARD_DEPS}
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror -Wno-sign-conversion
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_touch_frame VERSION 0.1.0 LANGUAGES C)
    add_library(platform_touch_frame STATIC ${PLATFORM_TOUCH_PURE_SOURCES})
    target_include_directories(platform_touch_frame
        PUBLIC
            include
            host/include
            "${CMAKE_CURRENT_LIST_DIR}/../platform_board/include"
        PRIVATE src)
    target_compile_features(platform_touch_frame PUBLIC c_std_11)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(platform_touch_frame PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(platform_touch_frame PRIVATE
            -fsanitize=address,undefined)
    endif()
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
