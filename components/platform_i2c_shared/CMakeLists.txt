if(COMMAND idf_component_register)
    set(SHARED_BUS_BOARD_DEPS)
    idf_build_get_property(P4_ACTIVE_BOARD_PROFILE P4_BOARD_PROFILE)
    if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")
        set(SHARED_BUS_BOARD_DEPS platform_tab5)
    endif()
    idf_component_register(
        SRCS "src/bus.c"
        INCLUDE_DIRS "include"
        REQUIRES esp_driver_i2c esp_driver_gpio platform_board ${SHARED_BUS_BOARD_DEPS}
    )
    target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wconversion -Wshadow -Werror
    )
else()
    cmake_minimum_required(VERSION 3.16)
    project(platform_i2c_shared VERSION 0.1.0 LANGUAGES C)
    enable_testing()
    add_subdirectory(tests)
endif()
